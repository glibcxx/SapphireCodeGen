#include "HeaderGenerator.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <llvm/ADT/StringRef.h>

namespace fs = std::filesystem;

void sapphire::codegen::HeaderGenerator::generate(
    const std::vector<std::string> &srcDirs,
    const std::vector<std::string> &srcFilePaths,
    const std::string              &outputDir
) {
    if (srcDirs.empty()) {
        return;
    }
    fs::path commonPath = fs::path(srcDirs[0]).parent_path();
    for (size_t i = 1; i < srcDirs.size(); ++i) {
        fs::path currentPath = fs::path(srcDirs[i]).parent_path();
        while (true) {
            auto [mismatch_iter, _] = std::mismatch(
                commonPath.begin(), commonPath.end(), currentPath.begin(), currentPath.end()
            );

            if (mismatch_iter != commonPath.begin()) {
                fs::path newCommonPath;
                for (auto it = commonPath.begin(); it != mismatch_iter; ++it) {
                    newCommonPath /= *it;
                }
                commonPath = newCommonPath;
                break;
            }

            if (commonPath.has_parent_path()) {
                commonPath = commonPath.parent_path();
            } else {
                break;
            }
        }
    }
    if (srcDirs.size() == 1) {
        commonPath = fs::path(srcDirs[0]).parent_path();
    }

    auto outInclude = fs::path(outputDir) / "SDK" / "api";
    for (auto &inputFile : srcFilePaths) {
        fs::path relativePath = fs::relative(inputFile, commonPath);
        fs::path outputPath = outInclude / relativePath;

        if (!fs::exists(outputPath.parent_path())) {
            fs::create_directories(outputPath.parent_path());
        }

        std::ifstream inputFileStream(inputFile);
        std::ofstream outputFileStream(outputPath);
        std::string   line;

        while (std::getline(inputFileStream, line)) {
            llvm::StringRef lineRef(line);
            size_t          macroPos = lineRef.find("SPHR_DECL_API");

            if (macroPos == llvm::StringRef::npos) {
                outputFileStream << line << "\n";
                continue;
            }

            size_t openParen = lineRef.find('(', macroPos);
            if (openParen == llvm::StringRef::npos) {
                outputFileStream << line << "\n";
                continue;
            }

            int    parenCount = 1;
            size_t closeParen = openParen;
            while (parenCount > 0 && ++closeParen < lineRef.size()) {
                if (lineRef[closeParen] == '(')
                    parenCount++;
                else if (lineRef[closeParen] == ')')
                    parenCount--;
            }

            if (parenCount != 0) {
                outputFileStream << line << "\n";
                continue;
            }

            llvm::StringRef preMacro = lineRef.substr(0, macroPos).trim();
            llvm::StringRef postMacro = lineRef.substr(closeParen + 1).ltrim();

            if (preMacro.empty()) {
                llvm::StringRef postMacroTrimmed = postMacro.trim();
                if (postMacroTrimmed.empty() || postMacroTrimmed.starts_with("//")) {
                    continue;
                }
            }

            outputFileStream << line.substr(0, macroPos) << line.substr(closeParen + 1) << "\n";
        }
    }
}