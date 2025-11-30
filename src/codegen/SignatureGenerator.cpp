#include "SignatureGenerator.h"
#include "../util/StringUtil.h"
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/raw_ostream.h>
#include <fstream>

namespace sapphire::codegen {

    namespace fs = std::filesystem;

    void SignatureGenerator::generate(const ExportMap &exports, const std::string &outputDir) {
        fs::path outputDirPath = fs::absolute(outputDir).lexically_normal();
        fs::create_directories(outputDirPath);

        for (auto &&[ver, sigDatabase] : exports) {
            auto verStr = util::mcVersionToString(ver);
            // Generate .sig.db file
            std::ofstream sigFile(
                outputDirPath / llvm::formatv("bedrock_sigs.{0}.sig.db", verStr).str(),
                std::ios::binary
            );
            if (sigFile.is_open()) {
                const_cast<SigDatabase &>(sigDatabase).save(sigFile);
            } else {
                llvm::errs() << "Failed to open .sig.db file for writing.\n";
            }

            // Generate .def file
            generateDefFile(
                (outputDirPath / llvm::formatv("bedrock_def.{0}.def", verStr).str()).string(),
                sigDatabase.getSigEntries()
            );
        }
    }

    void SignatureGenerator::generateDefFile(
        const std::string                        &outputPath,
        const std::vector<SigDatabase::SigEntry> &entries
    ) {
        std::ofstream file(outputPath);
        if (!file.is_open()) {
            llvm::errs() << llvm::formatv("[Error] Cannot write to {0}\n", outputPath);
            return;
        }
        file << "LIBRARY \"Minecraft.Windows.exe\"\n";
        file << "EXPORTS\n";
        for (const auto &entry : entries) {
            file << "    " << entry.mSymbol << "\n";
        }
        llvm::outs() << llvm::formatv(
            "[Success] Generated DEF file: {0} ({1} exports)\n", outputPath, entries.size()
        );
    }

} // namespace sapphire::codegen