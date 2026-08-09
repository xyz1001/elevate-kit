#include "elevate_kit/elevate_kit.h"

#include <fmt/format.h>
#include <fstream>
#include <sole.hpp>

#include "example_module.h"

REGISTER_ELEVATED_TASK(example_task, [](const nlohmann::json &params) {
    auto filename = params.at("filename").get<std::string>();
    // create a temp file
    std::filesystem::path temp_file =
            std::filesystem::temp_directory_path() / filename;
    std::ofstream ofs(temp_file);
    ofs << "This is a test file created by the elevated task." << std::endl;
    ofs.close();
    return nlohmann::json{{"filesize", std::filesystem::file_size(temp_file)}};
});

int main(int argc, char *argv[]) {
    elevate_kit::Process(argc, argv);

    auto filename = fmt::format("elevatekit_{}.txt", sole::uuid4().str());
    auto result = elevate_kit::CallTask("example_task",
                                        nlohmann::json{{"filename", filename}});

    if (result.is_null()) {
        fmt::println(stderr, "Failed to call elevated task");
        return 1;
    }
    fmt::println("Elevated task returned: {}", result.dump(4));
    auto filesize = result.at("filesize").get<std::uintmax_t>();
    fmt::println("Elevated task created file '{}' with size {} bytes", filename,
                 filesize);

    filesize = CalcFileSize(
            (std::filesystem::temp_directory_path() / filename).string());
    if (filesize == 0) {
        fmt::println(stderr, "Fail to cacl file size");
        return 1;
    }
    fmt::println(stderr, "Obtain file size: {}", filesize);
    return 0;
}
