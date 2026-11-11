// Runs a libFuzzer entry once per file in the given seed directories, without libFuzzer, so the
// GCC build (LUDIARS CI) replays the seeds under ctest. Exit 0 when every input returns.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

int main(int argc, char** argv) {
    std::size_t inputs = 0;
    for (int i = 1; i < argc; ++i) {
        for (const auto& entry : std::filesystem::directory_iterator(argv[i])) {
            if (!entry.is_regular_file()) continue;
            std::ifstream file(entry.path(), std::ios::binary);
            const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
            ++inputs;
        }
    }
    std::cout << inputs << " inputs\n";
    return inputs > 0 ? 0 : 1;
}
