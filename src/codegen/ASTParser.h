#pragma once

#include <map>
#include <string>
#include "SigDatabase.h"

// Forward declarations
namespace clang::tooling {
    class CompilationDatabase;
}
namespace sapphire::codegen {
    class CommandLine;
}

namespace sapphire::codegen {

    // A map from MC version to its signature database.
    using ExportMap = std::map<uint64_t, SigDatabase>;

    class ASTParser {
    public:
        ASTParser(clang::tooling::CompilationDatabase &compilations, const CommandLine &cmd) :
            mCompilations(compilations), mCmd(cmd) {}

        // Runs the AST parsing process on the given source files.
        // Returns 0 on success.
        int run(
            const std::vector<std::string> &sourceFiles, const std::string &pchPath, const std::string &targetMCVersion
        );

        // Provides access to the parsed export data.
        const ExportMap &getExports() const;

    private:
        clang::tooling::CompilationDatabase &mCompilations;
        const CommandLine                   &mCmd;
    };

} // namespace sapphire::codegen