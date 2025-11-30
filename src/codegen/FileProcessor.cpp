#include "FileProcessor.h"

#include <fstream>
#include <llvm/Support/ThreadPool.h>

namespace sapphire::codegen {

    namespace fs = std::filesystem;

    inline bool isIdentChar(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }

    FileProcessor::FileProcessor(const std::vector<std::string> &sourcePaths) {
        for (const auto &path : sourcePaths) {
            scanHeaderFiles(path);
        }
    }

    const std::vector<std::string> &FileProcessor::getAllHeaderFiles() const {
        return mAllHeaderFiles;
    }

    void FileProcessor::scanHeaderFiles(const std::string &rootDir) {
        if (!fs::exists(rootDir)) return;

        for (const auto &entry : fs::recursive_directory_iterator(rootDir)) {
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension().string();
                if (ext == ".h" || ext == ".hpp") {
                    mAllHeaderFiles.emplace_back(fs::absolute(entry.path()).string());
                }
            }
        }
    }

    bool FileProcessor::fastCheckToken(const std::string &filePath, const std::string &token) {
        std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
        if (!ifs.is_open()) return false;

        auto size = ifs.tellg();
        if (size == 0) return false;

        std::string content(size, '\0');
        ifs.seekg(0);
        ifs.read(&content[0], size);

        enum State { CODE,
                     LINE_COMMENT,
                     BLOCK_COMMENT,
                     STRING_LITERAL,
                     CHAR_LITERAL };
        State state = CODE;

        size_t n = content.size();
        size_t tLen = token.size();

        for (size_t i = 0; i < n; ++i) {
            char c = content[i];

            switch (state) {
            case CODE:
                if (c == '/' && i + 1 < n) {
                    if (content[i + 1] == '/') {
                        state = LINE_COMMENT;
                        ++i;
                        continue;
                    }
                    if (content[i + 1] == '*') {
                        state = BLOCK_COMMENT;
                        ++i;
                        continue;
                    }
                }
                if (c == '"') {
                    state = STRING_LITERAL;
                    continue;
                }
                if (c == '\'') {
                    state = CHAR_LITERAL;
                    continue;
                }

                if (c == token[0]) {
                    if (i + tLen <= n) {
                        if (content.compare(i, tLen, token) == 0) {
                            bool prevOk = (i == 0) || !isIdentChar(content[i - 1]);
                            bool nextOk = (i + tLen == n) || !isIdentChar(content[i + tLen]);

                            if (prevOk && nextOk) {
                                return true;
                            }
                        }
                    }
                }
                break;

            case LINE_COMMENT:
                if (c == '\n') state = CODE;
                break;

            case BLOCK_COMMENT:
                if (c == '*' && i + 1 < n && content[i + 1] == '/') {
                    state = CODE;
                    ++i;
                }
                break;

            case STRING_LITERAL:
                if (c == '\\') {
                    ++i;
                } else if (c == '"') {
                    state = CODE;
                }
                break;

            case CHAR_LITERAL:
                if (c == '\\') {
                    ++i;
                } else if (c == '\'') {
                    state = CODE;
                }
                break;
            }
        }

        return false;
    }

    std::vector<std::string> FileProcessor::filterFilesByToken(const std::string &token) {
        llvm::DefaultThreadPool Pool(llvm::hardware_concurrency());

        std::vector<std::string> filteredFiles;
        std::mutex               resultMutex;

        for (const auto &file : mAllHeaderFiles) {
            Pool.async([&]() {
                if (fastCheckToken(file, token)) {
                    std::lock_guard<std::mutex> lock(resultMutex);
                    filteredFiles.push_back(file);
                }
            });
        }

        Pool.wait();
        return filteredFiles;
    }

} // namespace sapphire::codegen