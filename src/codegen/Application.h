#pragma once

#include <llvm/Support/CommandLine.h>

namespace sapphire::codegen {

    class Application {
    public:
        Application(int argc, const char **argv);
        ~Application();

        // Runs the main application logic.
        // Returns the exit code.
        int run();

    private:
        int mArgc;
        const char **mArgv;
        llvm::cl::OptionCategory mCategory;
    };

} // namespace sapphire::codegen