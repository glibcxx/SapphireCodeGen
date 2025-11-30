#pragma once

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/FormatVariadic.h>
#include <set>

namespace sapphire::codegen::util {

    // 1'21'050 -> "v1_21_50"
    inline std::string mcVersionToString(uint64_t verNum) {
        return llvm::formatv("v1_{0}_{1}", verNum / 1'000 - 1'00, verNum % 1'000);
    }

    // v1_21_50/v1.21.50/1_21_50/1.21.50 -> 1'21'050
    inline uint64_t parseMCVersion(llvm::StringRef verStr) {
        verStr = verStr.trim(' ');
        if (!verStr.size()) return 0;
        if (verStr[0] == 'v' || verStr[0] == 'V') {
            verStr = verStr.substr(1);
        }
        llvm::SmallVector<llvm::StringRef> split;
        if (verStr.starts_with("1_"))
            verStr.split(split, '_');
        else if (verStr.starts_with("1."))
            verStr.split(split, '.');
        if (split.size() != 3) return 0;
        uint32_t v1 = 1'00'000;
        uint32_t v2;
        if (split[1].consumeInteger(10, v2) || v2 >= 100 || v2 < 0) return 0;
        uint32_t v3;
        if (split[2].consumeInteger(10, v3) || v3 >= 1000 || v3 < 0) return 0;
        return v1 + v2 * 1'000 + v3;
    }

    static bool parseMCVersions(std::set<uint64_t> &result, llvm::StringRef versStr) {
        llvm::SmallVector<llvm::StringRef> split;
        versStr.split(split, ',');
        for (auto &&verStr : split) {
            auto ver = util::parseMCVersion(verStr);
            if (!ver)
                return false;
            result.emplace(ver);
        }
        return true;
    }

    static bool parseMCVersions(std::set<std::string> &result, llvm::StringRef versStr) {
        llvm::SmallVector<llvm::StringRef> split;
        versStr.split(split, ',');
        for (auto &&verStr : split) {
            result.emplace(verStr.trim(' '));
        }
        return true;
    }

} // namespace sapphire::codegen::util