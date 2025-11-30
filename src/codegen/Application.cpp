#include "Application.h"
#include "CommandLine.h"
#include "FileProcessor.h"
#include "PCHGenerator.h"
#include "ASTParser.h"
#include "SignatureGenerator.h"
#include "HeaderGenerator.h"
#include "../util/StringUtil.h"

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FormatVariadic.h>

namespace fs = std::filesystem;

namespace sapphire::codegen {

    Application::Application(int argc, const char **argv) :
        mArgc(argc), mArgv(argv), mCategory("Sapphire CodeGen Options") {}

    Application::~Application() = default;

    int Application::run() {
        // SapphireCodeGen.exe [options] <source dir>
        // [options]:
        //     -o <path>               output path
        //     -p <path>               build path that contains compile commands
        //     -resource-dir <path>    clang resource headers path
        //     -mc-versions <ver-list> mc version macro names, seperated by ','

        CommandLine cmd(mArgc, mArgv, mCategory);
        if (!cmd.isValid()) {
            llvm::errs() << "Failed to parse command line arguments.\n";
            return 1;
        }

        auto outputPath = fs::absolute(cmd.getOutputDirectory()).lexically_normal();

        std::set<std::string> targetMCVersions;
        if (!util::parseMCVersions(targetMCVersions, cmd.getTargetMCVersions())) {
            llvm::errs() << llvm::formatv("[Error] Invalid mc version: '{0}'.\n", cmd.getTargetMCVersions());
        }

        std::filesystem::create_directories(outputPath);

        llvm::outs() << "[Scan] Scanning directories...\n";
        FileProcessor fileProcessor(cmd.getSourcePaths());

        const auto &allSources = fileProcessor.getAllHeaderFiles();
        if (allSources.empty()) {
            llvm::errs() << "[Error] No header files found.\n";
            return 1;
        }
        llvm::outs() << llvm::formatv("[Scan] Found {0} header files.\n", allSources.size());

        auto beginFilter = std::chrono::steady_clock::now();
        auto activeSources = fileProcessor.filterFilesByToken("SPHR_DECL_API");
        auto endFilter = std::chrono::steady_clock::now();
        llvm::outs() << llvm::formatv(
            "[Filter] Retained {0} / {1} files (Took {2}s)\n",
            activeSources.size(),
            allSources.size(),
            std::chrono::duration<double>(endFilter - beginFilter).count()
        );

        if (activeSources.empty()) {
            llvm::outs() << "[Info] No files contain SPHR_DECL_API. Nothing to do.\n";
            return 0;
        }

        ASTParser astParser(cmd.getCompilations(), cmd);
        for (auto &&version : targetMCVersions) {
            llvm::outs() << llvm::formatv("[Info] Processing for version: {}.\n", version);

            auto pchPath = (outputPath / llvm::formatv("sapphire_codegen.{0}.pch", version).str()).string();
            if (!PCHGenerator::generate(cmd.getCompilations(), cmd, pchPath, version)) {
                llvm::errs() << "[PCH] Warning: Generation failed. Performance will be impacted.\n";
                pchPath.clear();
            } else {
                llvm::outs() << llvm::formatv("[PCH] Ready: {0}\n", pchPath);
            }

            auto beginT = std::chrono::steady_clock::now();
            int  result = astParser.run(activeSources, pchPath, version);
            auto endT = std::chrono::steady_clock::now();
            llvm::outs() << llvm::formatv("[ASTParser] Time: {0}ms.\n", (endT - beginT).count() / 1'000'000.0);
        }
        SignatureGenerator::generate(astParser.getExports(), outputPath.string());
        HeaderGenerator::generate(cmd.getSourcePaths(), allSources, outputPath.string());
        return 0;
    }

} // namespace sapphire::codegen