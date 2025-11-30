#pragma once

#include <string>

// Forward declarations
namespace clang::tooling {
    class CompilationDatabase;
}

namespace sapphire::codegen {

    class CommandLine;

    class PCHGenerator {
    public:
        // Generates a PCH file. Returns true on success.
        static bool generate(
            const clang::tooling::CompilationDatabase &db,
            const CommandLine                         &cmd,
            const std::string                         &outputPchPath,
            const std::string                         &targetMCVersion
        );
    };

} // namespace sapphire::codegen