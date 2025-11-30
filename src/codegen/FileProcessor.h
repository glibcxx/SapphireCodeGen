#pragma once

#include <string>
#include <vector>

namespace sapphire::codegen {

    class FileProcessor {
    public:
        explicit FileProcessor(const std::vector<std::string>& sourcePaths);

        const std::vector<std::string>& getAllHeaderFiles() const;

        std::vector<std::string> filterFilesByToken(const std::string& token);

    private:
        void scanHeaderFiles(const std::string& rootDir);
        static bool fastCheckToken(const std::string& filePath, const std::string& token);

        std::vector<std::string> mAllHeaderFiles;
    };

} // namespace sapphire::codegen