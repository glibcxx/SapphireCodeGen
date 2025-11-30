#pragma once

#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <string>
#include <vector>
#include <optional>

namespace sapphire::codegen {

    class CommandLine {
    public:
        CommandLine(int argc, const char **argv, llvm::cl::OptionCategory &category);

        bool               isValid() const { return mOptionsParser.has_value(); }
        const std::string &getOutputDirectory() const;
        const std::string &getTargetMCVersions() const;
        const std::string &getClangResourceDir() const;

        const std::vector<std::string>      &getSourcePaths() const;
        clang::tooling::CompilationDatabase &getCompilations();

    private:
        std::optional<clang::tooling::CommonOptionsParser> mOptionsParser;
    };

} // namespace sapphire::codegen