#pragma once
// In-process HTTP server for the runner tests (stands in for Cernere, an in-app / Unity bridge).
// Nothing leaves 127.0.0.1.
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace runner_test {
struct FakeResponse {
    unsigned status = 200;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::chrono::milliseconds delay{0};
};

class FakeHttpServer {
public:
    using Request = boost::beast::http::request<boost::beast::http::string_body>;
    using Handler = std::function<FakeResponse(const Request&)>;
    FakeHttpServer(boost::asio::io_context& io, Handler handler)
        : io_(io), acceptor_(io, {boost::asio::ip::make_address("127.0.0.1"), 0}), handler_(std::move(handler)) { accept(); }
    unsigned short port() const { return acceptor_.local_endpoint().port(); }
    std::vector<std::string> seen;  // "METHOD target"
    std::vector<std::string> bodies;
    void close() { boost::system::error_code e; acceptor_.close(e); }
private:
    boost::asio::io_context& io_;
    boost::asio::ip::tcp::acceptor acceptor_;
    Handler handler_;
    void accept() {
        acceptor_.async_accept([this](boost::system::error_code error, boost::asio::ip::tcp::socket socket) {
            if (error) return;
            serve(std::make_shared<boost::beast::tcp_stream>(std::move(socket)));
            accept();
        });
    }
    void serve(std::shared_ptr<boost::beast::tcp_stream> stream) {
        auto buffer = std::make_shared<boost::beast::flat_buffer>();
        auto request = std::make_shared<Request>();
        boost::beast::http::async_read(*stream, *buffer, *request, [this, stream, buffer, request](boost::system::error_code error, std::size_t) {
            if (error) return;
            seen.push_back(std::string(request->method_string()) + " " + std::string(request->target()));
            bodies.push_back(request->body());
            auto reply = handler_(*request);
            auto timer = std::make_shared<boost::asio::steady_timer>(io_, reply.delay);
            timer->async_wait([stream, timer, reply](boost::system::error_code) {
                auto response = std::make_shared<boost::beast::http::response<boost::beast::http::string_body>>(static_cast<boost::beast::http::status>(reply.status), 11);
                for (const auto& [k, v] : reply.headers) response->set(k, v);
                response->body() = reply.body;
                response->prepare_payload();
                response->keep_alive(false);
                boost::beast::http::async_write(*stream, *response, [stream, response](boost::system::error_code, std::size_t) {
                    boost::system::error_code ignored;
                    stream->socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
                });
            });
        });
    }
};

// Runs the io_context until `done()` or the deadline.
template<class Done> void run_until(boost::asio::io_context& io, Done done, std::chrono::milliseconds deadline = std::chrono::milliseconds(5000)) {
    const auto end = std::chrono::steady_clock::now() + deadline;
    io.restart();
    while (!done() && std::chrono::steady_clock::now() < end) io.run_for(std::chrono::milliseconds(10));
}
}
