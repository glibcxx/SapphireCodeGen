#include "ASTParser.h"
#include "CommandLine.h"
#include "../util/StringUtil.h"

#include <clang/AST/Mangle.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/VTableBuilder.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/ThreadPool.h>
#include <llvm/Support/raw_ostream.h>

using namespace clang;
using namespace clang::tooling;

// these mangle functions are copied from LLVM
namespace {

    void mangleBits(llvm::raw_ostream &Out, llvm::APInt Value) {
        if (Value == 0)
            Out << "A@";
        else if (Value.uge(1) && Value.ule(10))
            Out << (Value - 1);
        else {
            llvm::SmallString<32> EncodedNumberBuffer;
            for (; Value != 0; Value.lshrInPlace(4))
                EncodedNumberBuffer.push_back('A' + (Value & 0xf).getZExtValue());
            std::reverse(EncodedNumberBuffer.begin(), EncodedNumberBuffer.end());
            Out.write(EncodedNumberBuffer.data(), EncodedNumberBuffer.size());
            Out << '@';
        }
    }

    void mangleNumber(llvm::raw_ostream &Out, llvm::APSInt Number) {
        unsigned    Width = std::max(Number.getBitWidth(), 64U);
        llvm::APInt Value = Number.extend(Width);
        if (Value.isNegative()) {
            Value = -Value;
            Out << '?';
        }
        mangleBits(Out, Value);
    }

    void mangleNumber(llvm::raw_ostream &Out, int64_t Number) {
        mangleNumber(Out, llvm::APSInt(llvm::APInt(64, Number), /*IsUnsigned*/ false));
    }

    void mangleVirtualFunctionThunk(
        llvm::raw_ostream &Out, MicrosoftMangleContext &MC, const CXXRecordDecl *RD, const CXXMethodDecl *MD
    ) {
        MSInheritanceModel IM = RD->calculateInheritanceModel();
        char               Code = '\0';
        switch (IM) {
        case MSInheritanceModel::Single: Code = '1'; break;
        case MSInheritanceModel::Multiple: Code = 'H'; break;
        case MSInheritanceModel::Virtual: Code = 'I'; break;
        case MSInheritanceModel::Unspecified: Code = 'J'; break;
        }
        uint64_t                       NVOffset = 0;
        uint64_t                       VBTableOffset = 0;
        uint64_t                       VBPtrOffset = 0;
        clang::MicrosoftVTableContext *VTContext = cast<MicrosoftVTableContext>(MC.getASTContext().getVTableContext());
        MethodVFTableLocation          ML = VTContext->getMethodVFTableLocation(GlobalDecl(MD));
        MC.mangleVirtualMemPtrThunk(MD, ML, Out);
        NVOffset = ML.VFPtrOffset.getQuantity();
        VBTableOffset = ML.VBTableIndex * 4;
        if (ML.VBase) {
            const ASTRecordLayout &Layout = MC.getASTContext().getASTRecordLayout(RD);
            VBPtrOffset = Layout.getVBPtrOffset().getQuantity();
        }
        if (VBTableOffset == 0 && IM == MSInheritanceModel::Virtual)
            NVOffset -= MC.getASTContext().getOffsetOfBaseWithVBPtr(RD).getQuantity();
        if (inheritanceModelHasNVOffsetField(/*IsMemberFunction=*/true, IM))
            mangleNumber(Out, static_cast<uint32_t>(NVOffset));
        if (inheritanceModelHasVBPtrOffsetField(IM))
            mangleNumber(Out, VBPtrOffset);
        if (inheritanceModelHasVBTableOffsetField(IM))
            mangleNumber(Out, VBTableOffset);
    }

} // namespace

namespace sapphire::codegen {

    static ExportMap  gExports;
    static std::mutex gExportsMutex;
    static std::mutex gLogMutex;

    class SapphireASTVisitor : public RecursiveASTVisitor<SapphireASTVisitor> {
    public:
        explicit SapphireASTVisitor(uint64_t mcVer, ASTContext &Context) : mTargetMCVersion(mcVer), mContext(Context) {
            // MSVC ABI
            mMangleCtx.reset(MicrosoftMangleContext::create(mContext, mContext.getDiagnostics()));
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
            llvm::SmallVector<llvm::StringRef> split;
            opsStr.split(split, ',');
            for (auto &&opStr : split) {
                auto ver = readSigOp(opStr);
                if (ver.opType == SigDatabase::SigOpType::_invalid)
                    return false;
                result.emplace_back(ver);
            }
            return true;
        }

        bool VisitDataDecl(VarDecl *Val) {
            if (!Val->hasExternalFormalLinkage() || !Val->hasAttrs()) return true;
            for (const auto *attr : Val->getAttrs()) {
                const auto *ann = dyn_cast<AnnotateAttr>(attr);
                if (!ann) continue;
                llvm::StringRef annotation = ann->getAnnotation();
                if (annotation != "sapphire::bind") continue;

                std::set<uint64_t>    supportVersion;
                SigDatabase::SigEntry sigEntry;
                sigEntry.mType = SigDatabase::SigEntry::Type::Data;

                auto argCount = ann->args_size();
                if (argCount) {
                    if (auto *versionListLiteral = getStringFromExpr(ann->args_begin()[0])) {
                        auto versStr = versionListLiteral->getString();
                        if (!util::parseMCVersions(supportVersion, versStr)) {
                            llvm::errs() << llvm::formatv("[Warning] Invalid version string: \"{0}\"\n", versStr);
                            continue;
                        }
                    }
                }
                if (!supportVersion.count(mTargetMCVersion))
                    continue;
                if (argCount == 2) { // SPHR_DECL_API("Versions", "Sig")
                    auto args = ann->args_begin();
                    if (auto *SigLiteral = getStringFromExpr(args[1])) {
                        sigEntry.mSig = SigLiteral->getString().str();
                    }
                } else if (argCount == 3) { // SPHR_DECL_API("Versions", "Ops", "Sig")
                    auto args = ann->args_begin();
                    if (auto *OpsLiteral = getStringFromExpr(args[1])) {
                        readSigOps(sigEntry.mOperations, OpsLiteral->getString());
                    }
                    if (auto *SigLiteral = getStringFromExpr(args[2])) {
                        sigEntry.mSig = SigLiteral->getString().str();
                    }
                } else {
                    continue;
                }

                if (sigEntry.mSig.empty()) {
                    llvm::errs() << llvm::formatv(
                        "[Warning] Empty signature detected for data: {0}\n",
                        Val->getQualifiedNameAsString()
                    );
                    continue;
                }

                llvm::raw_string_ostream Out(sigEntry.mSymbol);
                if (mMangleCtx->shouldMangleDeclName(Val)) {
                    mMangleCtx->mangleName(Val, Out);
                } else {
                    Out << Val->getQualifiedNameAsString();
                }

                if (!sigEntry.mSymbol.empty()) {
                    std::lock_guard<std::mutex> lk(gExportsMutex);
                    auto                        found = gExports.find(mTargetMCVersion);
                    if (found == gExports.end()) {
                        found = gExports.try_emplace(mTargetMCVersion, mTargetMCVersion).first;
                    }
                    found->second.addSigEntry(std::move(sigEntry));
                }
            }
            return true;
        }

        bool VisitFunctionDecl(FunctionDecl *Func) {
            if (!Func->hasAttrs()) return true;
            for (const auto *attr : Func->getAttrs()) {
                const auto *ann = dyn_cast<AnnotateAttr>(attr);
                if (!ann) continue;
                llvm::StringRef annotation = ann->getAnnotation();

                if (annotation != "sapphire::bind") continue;

                std::set<uint64_t>    supportVersion;
                SigDatabase::SigEntry sigEntry;
                sigEntry.mType = SigDatabase::SigEntry::Type::Function;

                auto argCount = ann->args_size();
                if (argCount) {
                    if (auto *versionListLiteral = getStringFromExpr(ann->args_begin()[0])) {
                        auto versStr = versionListLiteral->getString();
                        if (!util::parseMCVersions(supportVersion, versStr)) {
                            llvm::errs() << llvm::formatv("[Warning] Invalid version string: \"{0}\"\n", versStr);
                            continue;
                        }
                    }
                }
                if (!supportVersion.count(mTargetMCVersion))
                    continue;
                if (argCount == 2) { // SPHR_DECL_API("Versions", "Sig")
                    auto args = ann->args_begin();
                    if (auto *SigLiteral = getStringFromExpr(args[1])) {
                        sigEntry.mSig = SigLiteral->getString().str();
                    }
                } else if (argCount == 3) { // SPHR_DECL_API("Versions", "Ops", "Sig")
                    auto args = ann->args_begin();
                    if (auto *OpsLiteral = getStringFromExpr(args[1])) {
                        readSigOps(sigEntry.mOperations, OpsLiteral->getString());
                    }
                    if (auto *SigLiteral = getStringFromExpr(args[2])) {
                        sigEntry.mSig = SigLiteral->getString().str();
                    }
                } else {
                    continue;
                }

                if (sigEntry.mSig.empty()) {
                    llvm::errs() << llvm::formatv(
                        "[Warning] Empty signature detected for function: {0}\n",
                        Func->getNameInfo().getName().getAsString()
                    );
                    continue;
                }

                llvm::raw_string_ostream Out(sigEntry.mSymbol);
                if (mMangleCtx->shouldMangleDeclName(Func)) {
                    const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(Func);
                    if (MD && MD->isVirtual() && MD->isInstance()) {
                        sigEntry.mType = SigDatabase::SigEntry::Type::VirtualThunk;
                        llvm::raw_string_ostream OutEx(sigEntry.mExtraSymbol);
                        mangleVirtualFunctionThunk(
                            OutEx, *mMangleCtx, MD->getParent()->getMostRecentNonInjectedDecl(), MD
                        );
                    }
                    mMangleCtx->mangleName(Func, Out);
                } else {
                    Out << Func->getNameInfo().getName().getAsString();
                }

                if (!sigEntry.mSymbol.empty()) {
                    std::lock_guard<std::mutex> lk(gExportsMutex);
                    auto                        found = gExports.find(mTargetMCVersion);
                    if (found == gExports.end()) {
                        found = gExports.try_emplace(mTargetMCVersion, mTargetMCVersion).first;
                    }
                    found->second.addSigEntry(std::move(sigEntry));
                }
            }
            return true;
        }

        static const clang::StringLiteral *getStringFromExpr(const Expr *E) {
            if (!E) return nullptr;
            const Expr *Unwrapped = E->IgnoreParenImpCasts();
            return dyn_cast<clang::StringLiteral>(Unwrapped);
        }

    private:
        uint64_t                                mTargetMCVersion;
        ASTContext                             &mContext;
        std::unique_ptr<MicrosoftMangleContext> mMangleCtx;
    };

    class SapphireASTConsumer : public ASTConsumer {
    public:
        explicit SapphireASTConsumer(uint64_t mcVer, ASTContext &Context) :
            mVisitor(mcVer, Context), mSM(Context.getSourceManager()) {}

        void visitDeclContext(DeclContext *DC) {
            if (!DC) return;
            for (auto *D : DC->decls()) {
                if (isa<NamespaceDecl>(D))
                    visitDeclContext(cast<NamespaceDecl>(D));
                else if (isa<TagDecl>(D))
                    visitDeclContext(cast<TagDecl>(D));
                else if (FunctionDecl *FD = D->getAsFunction())
                    mVisitor.VisitFunctionDecl(FD);
                else if (isa<VarDecl>(D))
                    mVisitor.VisitDataDecl(cast<VarDecl>(D));
            }
        }

        bool HandleTopLevelDecl(DeclGroupRef DG) override {
            for (auto *D : DG) {
                clang::SourceLocation Loc = D->getLocation();
                if (mSM.isWrittenInMainFile(Loc)) {
                    if (isa<NamespaceDecl>(D))
                        visitDeclContext(cast<NamespaceDecl>(D));
                    else if (isa<TagDecl>(D))
                        visitDeclContext(cast<TagDecl>(D));
                    else if (FunctionDecl *FD = D->getAsFunction())
                        mVisitor.VisitFunctionDecl(FD);
                    else if (isa<VarDecl>(D))
                        mVisitor.VisitDataDecl(cast<VarDecl>(D));
                }
            }
            return true;
        }

    private:
        clang::SourceManager &mSM;
        SapphireASTVisitor    mVisitor;
    };

    class SapphireGenAction : public ASTFrontendAction {
        uint64_t mTargetMCVersion;

    public:
        SapphireGenAction(uint64_t mcVer) : mTargetMCVersion(mcVer) {}

        std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, llvm::StringRef file) override {
            return std::make_unique<SapphireASTConsumer>(mTargetMCVersion, CI.getASTContext());
        }
    };

    class SapphireGenActionFactory : public clang::tooling::FrontendActionFactory {
        uint64_t mTargetMCVersion;

    public:
        explicit SapphireGenActionFactory(uint64_t mcVer) : mTargetMCVersion(mcVer) {}

        std::unique_ptr<clang::FrontendAction> create() override {
            return std::make_unique<SapphireGenAction>(mTargetMCVersion);
        }
    };

    static ArgumentsAdjuster getSapphireArgumentsAdjuster(
        const CommandLine &cmd, const std::string &pchPath, const std::string &targetMCVersion
    ) {
        return [&cmd, &pchPath, &targetMCVersion](const CommandLineArguments &Args, llvm::StringRef /*filename*/) {
            CommandLineArguments newArgs;

            newArgs.push_back(Args[0]);

            newArgs.push_back("--target=x86_64-pc-windows-msvc");
            newArgs.push_back("-Wno-everything");
            newArgs.push_back("/DSAPPHIRE_CODEGEN_PASS");

            if (!targetMCVersion.empty()) {
                newArgs.push_back("/DMC_VERSION=" + targetMCVersion);
            }
            auto clangResourceDir = cmd.getClangResourceDir();
            if (!clangResourceDir.empty()) {
                newArgs.push_back("-resource-dir");
                newArgs.push_back(clangResourceDir);
            }
            if (!pchPath.empty()) {
                newArgs.push_back("-Xclang");
                newArgs.push_back("-include-pch");
                newArgs.push_back("-Xclang");
                newArgs.push_back(pchPath);
            }

            for (size_t i = 1; i < Args.size(); ++i) {
                llvm::StringRef Arg = Args[i];

                if (Arg.starts_with("/Yu") || Arg.starts_with("/Yc")
                    || Arg.starts_with("/Fp") || (Arg.starts_with("/FI") && !pchPath.empty())
                    || Arg.starts_with("-DMC_VERSION=") || Arg.starts_with("/DMC_VERSION=")) {
                    continue;
                }

                if (Arg == "-include-pch") {
                    i++; // Skip the next argument, which is the PCH file path
                    continue;
                }

                newArgs.push_back(std::string(Arg));
            }

            return newArgs;
        };
    }

    const ExportMap &ASTParser::getExports() const {
        return gExports;
    }

    int ASTParser::run(
        const std::vector<std::string> &sourceFiles, const std::string &pchPath, const std::string &targetMCVersion
    ) {
        auto versionNum = util::parseMCVersion(targetMCVersion);
        if (!versionNum) {
            llvm::outs() << llvm::formatv("[ASTParser] Invalid target mc version string: {}\n", targetMCVersion);
            return 1;
        }
        SapphireGenActionFactory actionFactory(versionNum);

        llvm::ThreadPoolStrategy strategy = llvm::hardware_concurrency();
        llvm::DefaultThreadPool  pool(strategy);

        llvm::outs() << llvm::formatv(
            "[Perf] Running on {0} threads (LLVM ThreadPool)...\n", strategy.compute_thread_count()
        );

        std::atomic<int> errorCount{0};
        for (auto &&header : sourceFiles) {
            pool.async([&]() {
                ClangTool tool(mCompilations, header, std::make_shared<PCHContainerOperations>());

                tool.appendArgumentsAdjuster(getSapphireArgumentsAdjuster(mCmd, pchPath, targetMCVersion));

                std::string              diagOutput;
                llvm::raw_string_ostream diagStream(diagOutput);

                llvm::IntrusiveRefCntPtr diagOpts = llvm::makeIntrusiveRefCnt<DiagnosticOptions>();
                TextDiagnosticPrinter    diagnosticPrinter(diagStream, diagOpts.get());

                tool.setDiagnosticConsumer(&diagnosticPrinter);

                int ret = tool.run(&actionFactory);
                if (ret != 0) {
                    ++errorCount;
                }

                diagStream.flush();
                if (!diagOutput.empty()) {
                    std::lock_guard<std::mutex> lock(gLogMutex);
                    llvm::errs() << diagOutput;
                }
            });
        }

        pool.wait();

        return errorCount;
    }

} // namespace sapphire::codegen