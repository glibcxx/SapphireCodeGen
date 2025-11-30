#pragma once

#include <string>
#include <vector>

namespace sapphire::codegen {

    class HeaderGenerator {
    public:
        static void generate(
            const std::vector<std::string> &srcDirs,
            const std::vector<std::string> &srcFilePaths,
            const std::string              &outputDir
        );
    };

} // namespace sapphire::codegen