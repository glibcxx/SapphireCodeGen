#include "PCHGenerator.h"
#include "CommandLine.h"

#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/FormatVariadic.h>

using namespace clang;
using namespace clang::tooling;

namespace sapphire::codegen {

    bool PCHGenerator::generate(
        const CompilationDatabase &db,
        const CommandLine         &cmd,
        const std::string         &outputPchPath,
        const std::string         &targetMCVersion
    ) {
        std::vector<std::string> baseArgs;
        std::string              sourceFilename;
        std::string              pchHeader;
        bool                     foundCandidate = false;

        auto allFiles = db.getAllFiles();
        for (const auto &file : allFiles) {
            auto cmds = db.getCompileCommands(file);
            if (cmds.empty()) continue;

            const auto &args = cmds[0].CommandLine;

            for (const auto &arg : args) {
                if (arg.rfind("/FI", 0) == 0) { // Starts with /FI
                    pchHeader = arg.substr(3);
                    if (pchHeader.size() >= 2 && pchHeader.front() == '"')
                        pchHeader = pchHeader.substr(1, pchHeader.size() - 2);

                    foundCandidate = true;
                    break;
                }
            }

            if (foundCandidate) {
                baseArgs = args;
                sourceFilename = cmds[0].Filename;
                llvm::outs() << llvm::formatv("[PCH] Found PCH template from: {0}\n", sourceFilename);
                break;
            }
        }

        if (!foundCandidate || pchHeader.empty()) {
            llvm::outs() << "[PCH] No /FI found in ANY compile commands. Skipping PCH generation.\n";
            return false;
        }

        std::vector<std::string> sources = {pchHeader};
        ClangTool                PCHTool(db, sources);

        auto &clangResourceDir = cmd.getClangResourceDir();

        PCHTool.appendArgumentsAdjuster(
            [&](const CommandLineArguments &arg, StringRef) {
                CommandLineArguments newArgs;

                newArgs.push_back(arg[0]);
                newArgs.push_back("--target=x86_64-pc-windows-msvc");
                newArgs.push_back("-Wno-everything");

                if (!clangResourceDir.empty()) {
                    newArgs.push_back("-resource-dir");
                    newArgs.push_back(clangResourceDir);
                }

                if (!targetMCVersion.empty()) {
                    newArgs.push_back("/DMC_VERSION=" + targetMCVersion);
                }
                newArgs.push_back("/DSAPPHIRE_CODEGEN_PASS");
                newArgs.push_back("-Xclang");
                newArgs.push_back("-skip-function-bodies");

                for (size_t i = 1; i < baseArgs.size(); ++i) {
                    StringRef arg = baseArgs[i];
                    if (arg.starts_with("/Yu") || arg.starts_with("/Yc") || arg.starts_with("/Fp") || arg.starts_with("/FI")
                        || arg.starts_with("/Fo") || arg.starts_with("/Fa") || arg.starts_with("/Fe")
                        || arg.starts_with("-DMC_VERSION=") || arg.starts_with("/DMC_VERSION=") || arg == sourceFilename) {
                        continue;
                    }
                    newArgs.push_back(std::string(arg));
                }

                newArgs.push_back("-o");
                newArgs.push_back(outputPchPath);
                newArgs.push_back(pchHeader);

                return newArgs;
            }
        );

        llvm::outs() << llvm::formatv("[PCH] Generating: {0} from {1}\n", outputPchPath, pchHeader);

        return PCHTool.run(newFrontendActionFactory<GeneratePCHAction>().get()) == 0;
    }

} // namespace sapphire::codegen