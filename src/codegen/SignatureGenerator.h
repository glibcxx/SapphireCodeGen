#pragma once

#include "ASTParser.h" // For ExportMap
#include <string>

namespace sapphire::codegen {

    class SignatureGenerator {
    public:
        // Generates .sig.db and .def files for each version in the export map.
        static void generate(
            const ExportMap   &exports,
            const std::string &outputDir
        );

    private:
        static void generateDefFile(
            const std::string                        &outputPath,
            const std::vector<SigDatabase::SigEntry> &entries
        );
    };

} // namespace sapphire::codegen