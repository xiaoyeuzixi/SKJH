#pragma once

#include "Mem.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

struct SKJH_MemoryExportStats {
    std::string module;
    DWORD64 base = 0;
    DWORD64 imageSize = 0;
    DWORD64 bytesRead = 0;
    DWORD64 bytesMissing = 0;
    size_t readablePages = 0;
    size_t missingPages = 0;
    size_t exportCount = 0;
    std::filesystem::path imagePath;
    std::filesystem::path manifestPath;
    std::filesystem::path exportsPath;
};

namespace SKJH_MemoryExportDetail {

inline std::string JsonEscape(const std::string& value) {
    std::ostringstream output;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '\\': output << "\\\\"; break;
            case '"': output << "\\\""; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (ch < 0x20) {
                    output << "\\u" << std::hex << std::setw(4)
                           << std::setfill('0') << static_cast<unsigned>(ch)
                           << std::dec << std::setfill(' ');
                } else {
                    output << static_cast<char>(ch);
                }
                break;
        }
    }
    return output.str();
}

inline std::string Hex(DWORD64 value) {
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase << value;
    return output.str();
}

inline std::string SafeFileStem(std::string name) {
    for (char& ch : name) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (!std::isalnum(value) && ch != '-' && ch != '_' && ch != '.') ch = '_';
    }
    while (!name.empty() && name.back() == '.') name.pop_back();
    return name.empty() ? "module" : name;
}

inline bool EnsureOutputDirectory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return !error && std::filesystem::is_directory(path, error);
}

inline bool ExportEat(const std::string& module,
                      const std::filesystem::path& outputPath,
                      SKJH_MemoryExportStats& stats) {
    if (!mem.hVMM || !mem.pid || module.empty()) return false;

    PVMMDLL_MAP_EAT eat = nullptr;
    if (!VMMDLL_Map_GetEATU(mem.hVMM, mem.pid,
                            const_cast<LPSTR>(module.c_str()), &eat) || !eat ||
        !eat->vaModuleBase || !eat->cMap) {
        if (eat) VMMDLL_MemFree(eat);
        return false;
    }

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        VMMDLL_MemFree(eat);
        return false;
    }

    output << "{\n"
           << "  \"module\": \"" << JsonEscape(module) << "\",\n"
           << "  \"moduleBase\": \"" << Hex(eat->vaModuleBase) << "\",\n"
           << "  \"ordinalBase\": " << eat->dwOrdinalBase << ",\n"
           << "  \"numberOfNames\": " << eat->cNumberOfNames << ",\n"
           << "  \"numberOfFunctions\": " << eat->cNumberOfFunctions << ",\n"
           << "  \"functions\": [\n";

    for (DWORD index = 0; index < eat->cMap; ++index) {
        const VMMDLL_MAP_EATENTRY& entry = eat->pMap[index];
        const std::string name = entry.uszFunction ? entry.uszFunction : "";
        const std::string forwarded = entry.uszForwardedFunction
            ? entry.uszForwardedFunction : "";
        output << "    {\"name\": \"" << JsonEscape(name)
               << "\", \"ordinal\": " << entry.dwOrdinal
               << ", \"address\": \"" << Hex(entry.vaFunction)
               << "\", \"rva\": \""
               << Hex(entry.vaFunction >= eat->vaModuleBase
                          ? entry.vaFunction - eat->vaModuleBase : 0)
               << "\", \"forwarded\": \"" << JsonEscape(forwarded)
               << "\"}" << (index + 1 == eat->cMap ? "\n" : ",\n");
    }
    output << "  ]\n}\n";

    stats.exportCount = eat->cMap;
    stats.exportsPath = outputPath;
    VMMDLL_MemFree(eat);
    return static_cast<bool>(output);
}

} // namespace SKJH_MemoryExportDetail

inline bool SKJH_ExportModuleFunctions(
        const std::string& module,
        const std::filesystem::path& outputDirectory,
        SKJH_MemoryExportStats& stats) {
    using namespace SKJH_MemoryExportDetail;
    if (!EnsureOutputDirectory(outputDirectory)) return false;
    stats.module = module;
    stats.base = mem.GetBase(module.c_str());
    stats.imageSize = mem.GetBaseSize(module.c_str());
    const std::string stem = SafeFileStem(module);
    return ExportEat(module, outputDirectory / (stem + ".exports.json"), stats);
}

inline bool SKJH_ExportModuleMemory(
        const std::string& module,
        const std::filesystem::path& outputDirectory,
        SKJH_MemoryExportStats& stats) {
    using namespace SKJH_MemoryExportDetail;
    stats = {};
    stats.module = module;
    if (!EnsureOutputDirectory(outputDirectory)) return false;

    stats.base = mem.GetBase(module.c_str());
    stats.imageSize = mem.GetBaseSize(module.c_str());
    constexpr DWORD64 kMaximumImageSize = 1024ull * 1024ull * 1024ull;
    if (!stats.base || !stats.imageSize || stats.imageSize > kMaximumImageSize)
        return false;

    const std::string stem = SafeFileStem(module);
    stats.imagePath = outputDirectory / (stem + ".mem.bin");
    stats.manifestPath = outputDirectory / (stem + ".memory.json");

    std::ofstream image(stats.imagePath,
                        std::ios::binary | std::ios::trunc);
    if (!image) return false;

    constexpr DWORD kChunkSize = 1024 * 1024;
    constexpr DWORD kPageSize = 4096;
    std::vector<unsigned char> chunk(kChunkSize);
    std::vector<DWORD64> missingOffsets;

    for (DWORD64 offset = 0; offset < stats.imageSize;) {
        const DWORD remaining = static_cast<DWORD>((std::min<DWORD64>)(
            kChunkSize, stats.imageSize - offset));
        std::fill(chunk.begin(), chunk.begin() + remaining, 0);

        if (mem.ReadMeta(stats.base + offset, chunk.data(), remaining)) {
            image.write(reinterpret_cast<const char*>(chunk.data()), remaining);
            stats.bytesRead += remaining;
            stats.readablePages += (remaining + kPageSize - 1) / kPageSize;
            offset += remaining;
            continue;
        }

        for (DWORD pageOffset = 0; pageOffset < remaining;
             pageOffset += kPageSize) {
            const DWORD pageBytes = (std::min)(kPageSize, remaining - pageOffset);
            unsigned char* destination = chunk.data() + pageOffset;
            if (mem.ReadMeta(stats.base + offset + pageOffset,
                             destination, pageBytes)) {
                stats.bytesRead += pageBytes;
                ++stats.readablePages;
            } else {
                std::fill(destination, destination + pageBytes, 0);
                missingOffsets.push_back(offset + pageOffset);
                stats.bytesMissing += pageBytes;
                ++stats.missingPages;
            }
        }
        image.write(reinterpret_cast<const char*>(chunk.data()), remaining);
        offset += remaining;
    }
    image.close();
    if (!image) return false;

    std::ofstream manifest(stats.manifestPath,
                           std::ios::binary | std::ios::trunc);
    if (!manifest) return false;
    manifest << "{\n"
             << "  \"format\": \"SKJH_DMA_MODULE_IMAGE_V1\",\n"
             << "  \"process\": \"" << JsonEscape(mem.process) << "\",\n"
             << "  \"pid\": " << mem.pid << ",\n"
             << "  \"module\": \"" << JsonEscape(module) << "\",\n"
             << "  \"base\": \"" << Hex(stats.base) << "\",\n"
             << "  \"imageSize\": " << stats.imageSize << ",\n"
             << "  \"bytesRead\": " << stats.bytesRead << ",\n"
             << "  \"bytesMissing\": " << stats.bytesMissing << ",\n"
             << "  \"pageSize\": " << kPageSize << ",\n"
             << "  \"missingPageOffsets\": [";
    for (size_t index = 0; index < missingOffsets.size(); ++index) {
        if (index) manifest << ", ";
        manifest << "\"" << Hex(missingOffsets[index]) << "\"";
    }
    manifest << "]\n}\n";
    manifest.close();

    // EAT export is best-effort: IL2CPP managed methods are normally reached
    // through dump RVAs, while native GameAssembly exports provide stable
    // anchors such as il2cpp_* entry points across game updates.
    ExportEat(module, outputDirectory / (stem + ".exports.json"), stats);
    return true;
}
