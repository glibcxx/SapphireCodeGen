#include "CommandLine.h"
#include <clang/Tooling/CommonOptionsParser.h>
#include <llvm/Support/Error.h>

using namespace clang::tooling;
using namespace llvm;

namespace sapphire::codegen {

    static cl::OptionCategory gSapphireToolCategory("Sapphire CodeGen Options");

    static cl::opt<std::string> optOutputDir(
        "o",
        cl::desc("Output directory"),
        cl::Required,
        cl::cat(gSapphireToolCategory)
    );

    static cl::opt<std::string> optTargetMCVersions(
        "mc-versions",
        cl::desc("Target Minecraft Versions (e.g. v1_21_50,v1_21_60)"),
        cl::Required,
        cl::cat(gSapphireToolCategory)
    );

    static cl::opt<std::string> optClangResourceDir(
        "resource-dir",
        cl::desc("Override Clang resource dir (path to lib/clang/<version>)"),
        cl::Optional,
        cl::cat(gSapphireToolCategory)
    );

    CommandLine::CommandLine(int argc, const char **argv, cl::OptionCategory &category) {
        auto expectedParser = CommonOptionsParser::create(argc, argv, category);
        if (!expectedParser) {
            errs() << expectedParser.takeError();
        } else {
            mOptionsParser.emplace(std::move(expectedParser.get()));
        }
    }

    const std::string &CommandLine::getOutputDirectory() const {
        return optOutputDir.getValue();
    }

    const std::string &CommandLine::getTargetMCVersions() const {
        return optTargetMCVersions.getValue();
    }

    const std::string &CommandLine::getClangResourceDir() const {
        return optClangResourceDir.getValue();
    }

    const std::vector<std::string> &CommandLine::getSourcePaths() const {
        return mOptionsParser->getSourcePathList();
    }

    CompilationDatabase &CommandLine::getCompilations() {
        return mOptionsParser->getCompilations();
    }

} // namespace sapphire::codegen