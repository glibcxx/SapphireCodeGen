#pragma once

#include <iostream>
#include <fstream>

struct ExportEntry {
    std::string symbol;
    std::string sig;
    std::string ops;
};

inline void generateSymbolDB(const std::string &outputPath, const std::vector<ExportEntry> &exports) {
    std::ofstream file(outputPath);
    if (!file.is_open()) return;

    file << "{\n";
    file << "  \"version\": 1,\n";
    file << "  \"symbols\": [\n";

    for (size_t i = 0; i < exports.size(); ++i) {
        const auto &entry = exports[i];

        file << "    {\n";
        file << "      \"symbol\": \"" << entry.symbol << "\",\n";
        file << "      \"sig\": \"" << entry.sig << "\",\n";
        file << "      \"ops\": \"" << entry.ops << "\"\n";

        if (i < exports.size() - 1)
            file << "    },\n";
        else
            file << "    }\n";
    }

    file << "  ]\n";
    file << "}\n";

    std::cout << "[Success] Generated DB file: " << outputPath << std::endl;
}

inline void generateLibFile(const std::string &defPath, const std::string &libPath) {
    std::string cmd = "llvm-dlltool -m i386:x86-64 -d \"" + defPath + "\" -l \"" + libPath + "\"";

    std::cout << "[Exec] " << cmd << std::endl;
    int ret = system(cmd.c_str());

    if (ret == 0) {
        std::cout << "[Success] Generated LIB file: " << libPath << std::endl;
    } else {
        std::cerr << "[Error] Failed to generate LIB file. Exit code: " << ret << std::endl;
    }
}

inline void generateDefFile(const std::string &outputPath, const std::vector<ExportEntry> &exports) {
    std::ofstream file(outputPath);
    if (!file.is_open()) {
        std::cerr << "[Error] Cannot write to " << outputPath << std::endl;
        return;
    }

    file << "LIBRARY \"Minecraft.Windows.exe\"\n";
    file << "EXPORTS\n";

    for (const auto &entry : exports) {
        file << "    " << entry.symbol << "\n";
    }

    std::cout << "[Success] Generated DEF file: " << outputPath << " (" << exports.size() << " exports)" << std::endl;
}
