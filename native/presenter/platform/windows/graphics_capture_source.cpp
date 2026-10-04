#include "graphics_capture_source.hpp"
#include "windows_text.hpp"
#include <windows.h>
#include <dwmapi.h>
#include <d3d11_4.h>
#include <roapi.h>
#include <windows.foundation.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <algorithm>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>

// WRL over the ABI interfaces rather than C++/WinRT, so any Windows 10 SDK builds this file.
namespace odeum::presenter::windows {
namespace capture = ABI::Windows::Graphics::Capture;
using ABI::Windows::Foundation::IClosable;
using ABI::Windows::Foundation::ITypedEventHandler;
using ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
using ABI::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface;
using ABI::Windows::Graphics::SizeInt32;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;

namespace {
constexpr std::string_view display_prefix = "display:", window_prefix = "window:";
constexpr auto pixel_format = ABI::Windows::Graphics::DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized;
using FrameArrivedHandler = ITypedEventHandler<capture::Direct3D11CaptureFramePool*, IInspectable*>;
using ItemClosedHandler = ITypedEventHandler<capture::GraphicsCaptureItem*, IInspectable*>;

void check(HRESULT result, const char* what) {
    if (SUCCEEDED(result)) return;
    std::ostringstream message;
    message << what << " failed (0x" << std::hex << std::setw(8) << std::setfill('0') << static_cast<unsigned long>(result) << ")";
    throw std::runtime_error(message.str());
}

template<class Factory> ComPtr<Factory> factory(const wchar_t* runtime_class) {
    ComPtr<Factory> result;
    check(RoGetActivationFactory(HStringReference(runtime_class).Get(), IID_PPV_ARGS(&result)), "RoGetActivationFactory");
    return result;
}

void close(IUnknown* object) {
    ComPtr<IClosable> closable;
    if (object && SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&closable)))) closable->Close();
}

bool supported() {
    try {
        boolean result = false;
        auto statics = factory<capture::IGraphicsCaptureSessionStatics>(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureSession);
        return SUCCEEDED(statics->IsSupported(&result)) && result;
    } catch (const std::runtime_error&) {
        return false;
    }
}

std::string hex_id(std::string_view prefix, std::uintptr_t value) {
    std::ostringstream id;
    id << prefix << std::hex << value;
    return id.str();
}

std::uintptr_t parse_id(const std::string& id, std::string_view prefix) {
    return static_cast<std::uintptr_t>(std::stoull(id.substr(prefix.size()), nullptr, 16));
}

struct Display { HMONITOR monitor; bool primary; int width, height; };

BOOL CALLBACK collect_display(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info))
        reinterpret_cast<std::vector<Display>*>(data)->push_back({monitor, (info.dwFlags & MONITORINFOF_PRIMARY) != 0,
            info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top});
    return TRUE;
}

bool capturable(HWND window) {
    if (!IsWindowVisible(window) || GetAncestor(window, GA_ROOT) != window || IsIconic(window)) return false;
    if (GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId()) return false;
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked || GetWindowTextLengthW(window) == 0) return false;
    RECT bounds{};
    return GetWindowRect(window, &bounds) && bounds.right - bounds.left >= 64 && bounds.bottom - bounds.top >= 64;
}

BOOL CALLBACK collect_window(HWND window, LPARAM data) {
    if (!capturable(window)) return TRUE;
    std::wstring title(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    title.resize(static_cast<std::size_t>(GetWindowTextW(window, title.data(), static_cast<int>(title.size()))));
    RECT bounds{};
    GetWindowRect(window, &bounds);
    reinterpret_cast<std::vector<CaptureTarget>*>(data)->push_back({hex_id(window_prefix, reinterpret_cast<std::uintptr_t>(window)),
        CaptureKind::window, narrow(title), bounds.right - bounds.left, bounds.bottom - bounds.top});
    return TRUE;
}

ComPtr<capture::IGraphicsCaptureItem> item_for(const CaptureTarget& target) {
    auto interop = factory<IGraphicsCaptureItemInterop>(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem);
    ComPtr<capture::IGraphicsCaptureItem> item;
    if (target.id.starts_with(display_prefix))
        check(interop->CreateForMonitor(reinterpret_cast<HMONITOR>(parse_id(target.id, display_prefix)), IID_PPV_ARGS(&item)), "CreateForMonitor");
    else if (target.id.starts_with(window_prefix))
        check(interop->CreateForWindow(reinterpret_cast<HWND>(parse_id(target.id, window_prefix)), IID_PPV_ARGS(&item)), "CreateForWindow");
    else throw std::invalid_argument("Unknown capture target");
    return item;
}

// Agile so the free-threaded pool can call it from its own threads.
template<class Handler, class Function> ComPtr<Handler> agile(Function function) {
    return Microsoft::WRL::Callback<Microsoft::WRL::Implements<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
        Handler, Microsoft::WRL::FtmBase>>(std::move(function));
}
}

struct GraphicsCaptureSource::Session {
    ComPtr<ID3D11Device> d3d;
    ComPtr<IDirect3DDevice> device;
    ComPtr<capture::IGraphicsCaptureItem> item;
    ComPtr<capture::IDirect3D11CaptureFramePool> pool;
    ComPtr<capture::IGraphicsCaptureSession> capture;
    EventRegistrationToken arrived{}, closed{};
    SizeInt32 size{};
    std::mutex mutex;
    bool running = true;
    FrameSink frames;
    ErrorSink errors;

    void arrive(capture::IDirect3D11CaptureFramePool* sender) {
        std::lock_guard lock(mutex);
        if (!running) return;
        ComPtr<capture::IDirect3D11CaptureFrame> frame;
        if (FAILED(sender->TryGetNextFrame(&frame)) || !frame) return;
        try {
            SizeInt32 content{};
            check(frame->get_ContentSize(&content), "ContentSize");
            if (content.Width != size.Width || content.Height != size.Height) {
                // The window was resized: later frames come from a pool of the new size.
                size = content;
                check(sender->Recreate(device.Get(), pixel_format, 2, content), "Recreate");
            }
            if (content.Width >= 2 && content.Height >= 2) {
                ComPtr<IDirect3DSurface> surface;
                check(frame->get_Surface(&surface), "Surface");
                ComPtr<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
                check(surface.As(&access), "IDirect3DDxgiInterfaceAccess");
                ID3D11Texture2D* texture = nullptr;
                check(access->GetInterface(IID_PPV_ARGS(&texture)), "GetInterface");
                ABI::Windows::Foundation::TimeSpan time{};
                frame->get_SystemRelativeTime(&time);
                VideoFrame video{content.Width, content.Height, time.Duration / 10,
                                 std::shared_ptr<void>(texture, [](void* p) { static_cast<ID3D11Texture2D*>(p)->Release(); })};
                frames(video);
            }
        } catch (const std::exception& error) {
            running = false;
            errors(error.what());
        }
        close(frame.Get());
    }
};

GraphicsCaptureSource::GraphicsCaptureSource() = default;
GraphicsCaptureSource::~GraphicsCaptureSource() { stop(); }

std::vector<CaptureTarget> GraphicsCaptureSource::targets() {
    std::vector<Display> displays;
    EnumDisplayMonitors(nullptr, nullptr, collect_display, reinterpret_cast<LPARAM>(&displays));
    std::stable_partition(displays.begin(), displays.end(), [](const Display& d) { return d.primary; });
    std::vector<CaptureTarget> result;
    for (std::size_t i = 0; i < displays.size(); ++i)
        result.push_back({hex_id(display_prefix, reinterpret_cast<std::uintptr_t>(displays[i].monitor)), CaptureKind::display,
                          std::to_string(i + 1) + (displays[i].primary ? " (メイン)" : ""), displays[i].width, displays[i].height});
    EnumWindows(collect_window, reinterpret_cast<LPARAM>(&result));
    return result;
}

void GraphicsCaptureSource::start(const CaptureTarget& target, const StreamSettings&, FrameSink frames, ErrorSink errors) {
    stop();
    if (!supported()) throw std::runtime_error("Windows Graphics Capture is not available (Windows 10 1903 or later is required)");
    auto session = std::make_shared<Session>();
    session->frames = std::move(frames);
    session->errors = std::move(errors);
    ComPtr<ID3D11DeviceContext> context;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                            D3D11_SDK_VERSION, &session->d3d, nullptr, &context), "D3D11CreateDevice");
    // The encoder copies frames on the pool's thread while the capture uses the same device.
    ComPtr<ID3D11Multithread> multithread;
    if (SUCCEEDED(session->d3d.As(&multithread))) multithread->SetMultithreadProtected(TRUE);
    ComPtr<IDXGIDevice> dxgi;
    check(session->d3d.As(&dxgi), "IDXGIDevice");
    ComPtr<IInspectable> inspectable;
    check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), &inspectable), "CreateDirect3D11DeviceFromDXGIDevice");
    check(inspectable.As(&session->device), "IDirect3DDevice");
    session->item = item_for(target);
    check(session->item->get_Size(&session->size), "Size");
    auto pools = factory<capture::IDirect3D11CaptureFramePoolStatics2>(RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool);
    check(pools->CreateFreeThreaded(session->device.Get(), pixel_format, 2, session->size, &session->pool), "CreateFreeThreaded");
    check(session->pool->CreateCaptureSession(session->item.Get(), &session->capture), "CreateCaptureSession");
    ComPtr<capture::IGraphicsCaptureSession2> cursor;
    if (SUCCEEDED(session->capture.As(&cursor))) cursor->put_IsCursorCaptureEnabled(true);
#ifdef ____x_ABI_CWindows_CGraphics_CCapture_CIGraphicsCaptureSession3_INTERFACE_DEFINED__
    ComPtr<capture::IGraphicsCaptureSession3> border;
    if (SUCCEEDED(session->capture.As(&border))) border->put_IsBorderRequired(false); // Windows 11 only
#endif
    std::weak_ptr<Session> weak = session;
    check(session->pool->add_FrameArrived(agile<FrameArrivedHandler>([weak](capture::IDirect3D11CaptureFramePool* sender, IInspectable*) {
        if (auto self = weak.lock()) self->arrive(sender);
        return S_OK;
    }).Get(), &session->arrived), "FrameArrived");
    check(session->item->add_Closed(agile<ItemClosedHandler>([weak](capture::IGraphicsCaptureItem*, IInspectable*) {
        if (auto self = weak.lock()) {
            std::lock_guard lock(self->mutex);
            if (self->running) {
                self->running = false;
                self->errors("取り込み対象のウインドウが閉じられました");
            }
        }
        return S_OK;
    }).Get(), &session->closed), "Closed");
    check(session->capture->StartCapture(), "StartCapture");
    session_ = std::move(session);
}

void GraphicsCaptureSource::stop() {
    if (!session_) return;
    {
        std::lock_guard lock(session_->mutex);
        session_->running = false;
    }
    session_->pool->remove_FrameArrived(session_->arrived);
    session_->item->remove_Closed(session_->closed);
    close(session_->capture.Get());
    close(session_->pool.Get());
    session_.reset();
}

CapturePermission GraphicsCaptureSource::permission() { return supported() ? CapturePermission::granted : CapturePermission::denied; }
}
