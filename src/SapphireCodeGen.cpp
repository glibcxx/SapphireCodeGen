#include <iostream>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Mangle.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/ThreadPool.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <llvm/Support/FormatVariadic.h>
#include <mutex>

#include "SigDatabase.h"

using namespace clang;
using namespace clang::tooling;

namespace fs = std::filesystem;

static llvm::cl::OptionCategory   gSapphireToolCategory("Sapphire CodeGen Options");
static llvm::cl::opt<std::string> optOutputDir(
    "o",
    llvm::cl::desc("Output directory"),
    llvm::cl::Required,
    llvm::cl::cat(gSapphireToolCategory)
);
static llvm::cl::opt<std::string> optTargetMCVersion(
    "mc-version",
    llvm::cl::desc("Target Minecraft Version (e.g. v1_21_50)"),
    llvm::cl::Optional,
    llvm::cl::init("v1_21_50"),
    llvm::cl::cat(gSapphireToolCategory)
);
static llvm::cl::opt<std::string> optClangResourceDir(
    "resource-dir",
    llvm::cl::desc("Override Clang resource dir (path to lib/clang/<version>)"),
    llvm::cl::Optional,
    llvm::cl::cat(gSapphireToolCategory)
);

static std::map<uint64_t, SigDatabase> gExports;
static std::mutex                      gExportsMutex;
static std::mutex                      gLogMutex;

class SapphireASTVisitor : public RecursiveASTVisitor<SapphireASTVisitor> {
public:
    explicit SapphireASTVisitor(ASTContext *Context) : Context(Context) {
        // MSVC ABI
        MangleCtx.reset(MicrosoftMangleContext::create(*Context, Context->getDiagnostics()));
    }

    // v1_21_50/v1.21.50/1_21_50/1.21.50 -> 1'21'050
    static uint64_t readMCVersion(llvm::StringRef verStr) {
        verStr = verStr.trim(' ');
        if (!verStr.size()) return 0;
        if (verStr[0] == 'v' || verStr[0] == 'V') {
            verStr = verStr.substr(1);
        }
        SmallVector<StringRef> split;
        if (verStr.starts_with("1_"))
            verStr.split(split, '_');
        else if (verStr.starts_with("1."))
            verStr.split(split, '.');
        if (split.size() != 3) return 0;
        uint32_t v1 = 1'00'000;
        uint32_t v2;
        if (split[1].consumeInteger(0, v2) && v2 < 99 && v2 >= 0) return 0;
        uint32_t v3;
        if (split[2].consumeInteger(0, v3) && v3 < 999 && v3 >= 0) return 0;
        return v1 + v2 * 1'000 + v3;
    }

    static bool readMCVersions(std::set<uint64_t> &result, llvm::StringRef versStr) {
        SmallVector<StringRef> split;
        versStr.split(split, ',');
        for (auto &&verStr : split) {
            auto ver = readMCVersion(verStr);
            if (!ver)
                return false;
            result.emplace(ver);
        }
        return true;
    }

    static SigDatabase::SigOp readSigOp(llvm::StringRef opTypeStr) {
        opTypeStr = opTypeStr.trim(' ');
        if (opTypeStr.empty() || opTypeStr == "none" || opTypeStr == "None") return {SigDatabase::SigOpType::None};
        if (opTypeStr == "deref" || opTypeStr == "Deref") return {SigDatabase::SigOpType::Deref};
        if (opTypeStr == "call" || opTypeStr == "Call") return {SigDatabase::SigOpType::Call};
        if (opTypeStr == "move" || opTypeStr == "Mov") return {SigDatabase::SigOpType::Mov};
        if (opTypeStr == "lea" || opTypeStr == "Lea") return {SigDatabase::SigOpType::Lea};
        if (opTypeStr.starts_with("disp:") || opTypeStr.starts_with("Disp:")) {
            opTypeStr = opTypeStr.substr(5).trim(' ');
            ptrdiff_t disp;
            if (opTypeStr.consumeInteger(0, disp))
                return {SigDatabase::SigOpType::_invalid};
            return {SigDatabase::SigOpType::Disp, disp};
        }
        return {SigDatabase::SigOpType::_invalid};
    }

    // "displ:6,call" -> {{displ, 6}, {call, X}}
    static bool readSigOps(std::vector<SigDatabase::SigOp> &result, llvm::StringRef opsStr) {
        SmallVector<StringRef> split;
        opsStr.split(split, ',');
        for (auto &&opStr : split) {
            auto ver = readSigOp(opStr);
            if (ver.opType == SigDatabase::SigOpType::_invalid)
                return false;
            result.emplace_back(ver);
        }
        return true;
    }

    bool VisitFunctionDecl(FunctionDecl *Func) {
        if (!Context->getSourceManager().isInMainFile(Func->getLocation())) return true;
        if (!Func->hasAttrs()) return true;
        for (const clang::Attr *attr : Func->getAttrs()) {
            const auto *ann = dyn_cast<AnnotateAttr>(attr);
            if (!ann) continue;
            StringRef annotation = ann->getAnnotation();

            if (annotation != "sapphire::bind") continue;

            std::set<uint64_t>    supportVersion;
            SigDatabase::SigEntry sigEntry;

            auto argCount = ann->args_size();
            if (argCount) {
                if (auto *versionListLiteral = getStringFromExpr(ann->args_begin()[0])) {
                    auto versStr = versionListLiteral->getString();
                    if (!readMCVersions(supportVersion, versStr)) {
                        llvm::errs() << "[Waring] Invalid version string: \"" << versStr << "\"\n";
                        continue;
                    }
                }
            }
            if (argCount == 2) { // SAPPHIRE_API("Versions", "Sig")
                auto args = ann->args_begin();
                if (auto *SigLiteral = getStringFromExpr(args[1])) {
                    sigEntry.mSig = SigLiteral->getString();
                }
            } else if (argCount == 3) { // SAPPHIRE_API("Versions", "Ops", "Sig")
                auto args = ann->args_begin();
                if (auto *OpsLiteral = getStringFromExpr(args[1])) {
                    readSigOps(sigEntry.mOperations, OpsLiteral->getString());
                }
                if (auto *SigLiteral = getStringFromExpr(args[2])) {
                    sigEntry.mSig = SigLiteral->getString();
                }
            } else {
                continue;
            }

            if (sigEntry.mSig.empty()) {
                std::cerr << "[Warning] Empty signature detected for function: "
                          << Func->getNameInfo().getName().getAsString() << '\n';
                continue;
            }

            std::string              mangledName;
            llvm::raw_string_ostream Out(sigEntry.mSymbol);
            if (MangleCtx->shouldMangleDeclName(Func)) {
                MangleCtx->mangleName(Func, Out);
            } else {
                Out << Func->getNameInfo().getName().getAsString();
            }

            if (!sigEntry.mSymbol.empty()) {
                std::lock_guard<std::mutex> lk(gExportsMutex);
                for (auto &&ver : supportVersion) {
                    auto found = gExports.find(ver);
                    if (found == gExports.end()) {
                        found = gExports.try_emplace(ver, ver).first;
                    }
                    found->second.addSigEntry(std::move(sigEntry));
                }
            }
        }
        return true;
    }

    static const StringLiteral *getStringFromExpr(const Expr *E) {
        if (!E) return nullptr;
        const Expr *Unwrapped = E->IgnoreParenImpCasts();
        return dyn_cast<StringLiteral>(Unwrapped);
    }

private:
    ASTContext                    *Context;
    std::unique_ptr<MangleContext> MangleCtx;
};

class SapphireASTConsumer : public ASTConsumer {
public:
    explicit SapphireASTConsumer(ASTContext *Context) : Visitor(Context) {}

    void HandleTranslationUnit(ASTContext &Context) override {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }

private:
    SapphireASTVisitor Visitor;
};

class SapphireGenAction : public ASTFrontendAction {
public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) override {
        return std::make_unique<SapphireASTConsumer>(&CI.getASTContext());
    }
};

void scanHeaderFiles(const std::string &rootDir, std::vector<std::string> &output) {
    if (!fs::exists(rootDir)) return;

    for (const auto &entry : fs::recursive_directory_iterator(rootDir)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            if (ext == ".h" || ext == ".hpp") {
                output.emplace_back(fs::absolute(entry.path()).string());
            }
        }
    }
}

ArgumentsAdjuster getSapphireArgumentsAdjuster(const std::string &pchPath) {
    return [&pchPath](const CommandLineArguments &Args, StringRef /*filename*/) {
        CommandLineArguments newArgs;
        newArgs.push_back(Args[0]);

        if (!optTargetMCVersion.empty()) {
            newArgs.push_back("/DMC_VERSION=" + optTargetMCVersion);
        }
        if (!optClangResourceDir.empty()) {
            newArgs.push_back("-resource-dir");
            newArgs.push_back(optClangResourceDir);
        }
        if (!pchPath.empty()) {
            newArgs.push_back("-Xclang");
            newArgs.push_back("-include-pch");
            newArgs.push_back("-Xclang");
            newArgs.push_back(pchPath);
        }

        for (size_t i = 1; i < Args.size(); ++i) {
            StringRef Arg = Args[i];

            if (Arg.starts_with("/Yu") || Arg.starts_with("/Yc") || Arg.starts_with("/Fp")
                || Arg.starts_with("/FI")) {
                continue;
            }
            if (Arg == "-include-pch") {
                i++;
                continue;
            }

            if (Arg.starts_with("-DMC_VERSION=") || Arg.starts_with("/DMC_VERSION=")) {
                continue;
            }

            newArgs.push_back(std::string(Arg));
        }

        return newArgs;
    };
}

// 让clang生成PCH
bool generatePCHInternal(const CompilationDatabase &db, const std::string &outputPchPath) {
    std::vector<std::string> baseArgs;
    std::string              sourceFilename;
    std::string              pchHeader;
    bool                     foundCandidate = false;

    auto allFiles = db.getAllFiles();
    for (const auto &file : allFiles) {
        auto cmds = db.getCompileCommands(file);
        if (cmds.empty()) continue;

        const auto &args = cmds[0].CommandLine;

        // 检查是否有 /FI
        for (const auto &arg : args) {
            if (arg.rfind("/FI", 0) == 0) { // Starts with /FI
                pchHeader = arg.substr(3);
                // 去除可能的引号
                if (pchHeader.size() >= 2 && pchHeader.front() == '"')
                    pchHeader = pchHeader.substr(1, pchHeader.size() - 2);

                foundCandidate = true;
                break;
            }
        }

        if (foundCandidate) {
            baseArgs = args;
            sourceFilename = cmds[0].Filename;
            std::cout << "[PCH] Found PCH template from: " << sourceFilename << '\n';
            break;
        }
    }

    if (!foundCandidate || pchHeader.empty()) {
        std::cout << "[PCH] No /FI found in ANY compile commands. Skipping PCH generation." << '\n';
        return false;
    }

    std::vector<std::string> sources = {pchHeader};
    ClangTool                PCHTool(db, sources);

    PCHTool.appendArgumentsAdjuster(
        [&](const CommandLineArguments &arg, StringRef) {
            CommandLineArguments newArgs;

            newArgs.push_back(arg[0]);

            if (!optClangResourceDir.empty()) {
                newArgs.push_back("-resource-dir");
                newArgs.push_back(optClangResourceDir);
            }

            if (!optTargetMCVersion.empty()) {
                newArgs.push_back("/DMC_VERSION=" + optTargetMCVersion);
            }

            for (size_t i = 1; i < baseArgs.size(); ++i) {
                StringRef Arg = baseArgs[i];

                if (Arg.starts_with("/Yu") || Arg.starts_with("/Yc") || Arg.starts_with("/Fp") || Arg.starts_with("/FI")) continue;

                if (Arg.starts_with("/Fo") || Arg.starts_with("/Fa") || Arg.starts_with("/Fe")) continue;
                if (Arg.starts_with("-DMC_VERSION=") || Arg.starts_with("/DMC_VERSION=")) {
                    continue;
                }

                if (Arg == sourceFilename) continue;

                newArgs.push_back(std::string(Arg));
            }

            newArgs.push_back("-o");
            newArgs.push_back(outputPchPath);

            newArgs.push_back(pchHeader);

            return newArgs;
        }
    );
    PCHTool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
        "-Wno-pragma-system-header-outside-header", ArgumentInsertPosition::BEGIN
    ));
    PCHTool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-Wno-everything", ArgumentInsertPosition::BEGIN));

    std::cout << "[PCH] Generating: " << outputPchPath << " from " << pchHeader << '\n';

    return PCHTool.run(newFrontendActionFactory<GeneratePCHAction>().get()) == 0;
}

int runParallel(CommonOptionsParser &optionsParser, const std::vector<std::string> &sources, const std::string &pchPath) {
    llvm::ThreadPoolStrategy strategy = llvm::hardware_concurrency();
    llvm::DefaultThreadPool  pool(strategy);

    std::cout << "[Perf] Running on " << strategy.compute_thread_count() << " threads (LLVM ThreadPool)..." << '\n';

    unsigned         threadCount = strategy.compute_thread_count();
    std::atomic<int> errorCount{0};

    for (auto &&header : sources) {
        pool.async([&]() {
            ClangTool tool(optionsParser.getCompilations(), header);

            tool.appendArgumentsAdjuster(getSapphireArgumentsAdjuster(pchPath));

            tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
                "--target=x86_64-pc-windows-msvc", ArgumentInsertPosition::BEGIN
            ));
            tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
                "/DSAPPHIRE_CODEGEN_PASS", ArgumentInsertPosition::BEGIN
            ));
            tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
                "-Wno-pragma-system-header-outside-header", ArgumentInsertPosition::BEGIN
            ));
            tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
                "-Wno-everything", ArgumentInsertPosition::BEGIN
            ));

            std::string              diagOutput;
            llvm::raw_string_ostream diagStream(diagOutput);

            llvm::IntrusiveRefCntPtr diagOpts = llvm::makeIntrusiveRefCnt<DiagnosticOptions>();
            TextDiagnosticPrinter    diagnosticPrinter(diagStream, diagOpts.get());

            tool.setDiagnosticConsumer(&diagnosticPrinter);

            int ret = tool.run(newFrontendActionFactory<SapphireGenAction>().get());
            if (ret != 0) {
                errorCount++;
            }

            diagStream.flush();
            if (!diagOutput.empty()) {
                std::lock_guard<std::mutex> lock(gLogMutex);
                std::cerr << diagOutput;
            }
        });
    }

    pool.wait();

    return errorCount > 0 ? 1 : 0;
}

inline bool isIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// 一个简易的token查找工具
bool fastCheckToken(const std::string &filePath, const std::string &token) {
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

std::vector<std::string> filterHeadersByToken(const std::vector<std::string> &files, const std::string &token) {
    llvm::DefaultThreadPool Pool(llvm::hardware_concurrency());

    std::vector<std::string> filteredFiles;
    std::mutex               resultMutex;
    std::atomic<int>         keptCount{0};

    for (const auto &file : files) {
        Pool.async([&, file]() {
            if (fastCheckToken(file, token)) {
                std::lock_guard<std::mutex> lock(resultMutex);
                filteredFiles.push_back(file);
                keptCount++;
            }
        });
    }

    Pool.wait();
    return filteredFiles;
}

void generateDefFile(const fs::path &outputPath, const std::vector<SigDatabase::SigEntry> &entries) {
    std::ofstream file(outputPath);
    if (!file.is_open()) {
        std::cerr << "[Error] Cannot write to " << outputPath << std::endl;
        return;
    }
    file << "LIBRARY \"Minecraft.Windows.exe\"\n";
    file << "EXPORTS\n";
    for (const auto &entry : entries) {
        file << "    " << entry.mSymbol << "\n";
    }
    std::cout << "[Success] Generated DEF file: " << outputPath << " (" << entries.size() << " exports)" << std::endl;
}

int main(int argc, const char **argv) {
    // SapphireCodeGen.exe [options] <source dir>
    // [options]:
    //     -o <path>              output path
    //     -p <path>              build path that contains compile commands
    //     -resource-dir <path>   clang resource headers path
    //     -mc-version <ver>      mc version macro name [optional]
    auto expectedParser = CommonOptionsParser::create(argc, argv, gSapphireToolCategory);
    if (!expectedParser) {
        llvm::errs() << expectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &optionsParser = expectedParser.get();
    const auto          &sourcePaths = optionsParser.getSourcePathList();
    if (sourcePaths.empty()) {
        std::cerr << "[Error] No source directory specified." << '\n';
        return 1;
    }

    fs::path outputDir = fs::absolute(optOutputDir.getValue());

    std::vector<std::string> allSources;
    for (auto &&srcPath : sourcePaths) {
        std::cout << "[Scan] Scanning directory: " << srcPath << '\n';
        scanHeaderFiles(srcPath, allSources);
    }
    if (allSources.empty()) {
        std::cerr << "[Error] No header files found." << '\n';
        return 1;
    }

    std::cout << "[Scan] Found " << allSources.size() << " header files." << '\n';

    auto beginFilter = std::chrono::steady_clock::now();
    auto activeSources = filterHeadersByToken(allSources, "SAPPHIRE_API");
    auto endFilter = std::chrono::steady_clock::now();
    std::cout << "[Filter] Retained " << activeSources.size() << " / " << allSources.size()
              << " files (Took " << std::chrono::duration<double>(endFilter - beginFilter).count() << "s)" << std::endl;

    if (activeSources.empty()) {
        std::cout << "[Info] No files contain SAPPHIRE_API. Nothing to do." << std::endl;
        return 0;
    }

    fs::create_directories(outputDir);
    std::string pchPath = (outputDir / "sapphire_codegen.pch").string();

    if (!generatePCHInternal(optionsParser.getCompilations(), pchPath)) {
        std::cerr << "[PCH] Warning: Generation failed. Performance will be impacted." << '\n';
        pchPath.clear();
    } else {
        std::cout << "[PCH] Ready: " << pchPath << '\n';
    }

    auto beginT = std::chrono::steady_clock::now();
    int  result = runParallel(optionsParser, activeSources, pchPath);
    auto endT = std::chrono::steady_clock::now();
    std::cout << "Time: " << (endT - beginT).count() / 1'000'000.0 << "ms." << '\n';

    if (result == 0) {
        fs::create_directories(outputDir);
        for (auto &&[ver, SigDatabase] : gExports) {
            std::ofstream file(outputDir / llvm::formatv("bedrock_sigs.{0}.sig.db", ver).str(), std::ios::binary);
            SigDatabase.save(file);
            generateDefFile(outputDir / llvm::formatv("bedrock_def.{0}.def", ver).str(), SigDatabase.getSigEntries());
        }
    }

    return result;
}