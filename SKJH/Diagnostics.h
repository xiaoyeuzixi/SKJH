#pragma once

#include "Mem.h"
#include "Offset.h"
#include "ESPUtils.h"
#include "SKJH_Entity.h"
#include "SKJH_Skeleton.h"
#include "TemplateCatalog.h"
#include "PlayerIntel.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

inline std::string SKJH_JsonEscape(const std::string& value) {
    std::ostringstream escaped;
    for (size_t index = 0; index < value.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(value[index]);
        switch (ch) {
            case '\\': escaped << "\\\\"; break;
            case '"':  escaped << "\\\""; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if (ch < 0x20) {
                    escaped << "\\u" << std::hex << std::setw(4)
                            << std::setfill('0') << static_cast<int>(ch) << std::dec;
                } else if (ch < 0x80) {
                    escaped << static_cast<char>(ch);
                } else {
                    size_t length = 0;
                    if (ch >= 0xC2 && ch <= 0xDF) length = 2;
                    else if (ch >= 0xE0 && ch <= 0xEF) length = 3;
                    else if (ch >= 0xF0 && ch <= 0xF4) length = 4;

                    bool valid = length != 0 && index + length <= value.size();
                    for (size_t offset = 1; valid && offset < length; ++offset) {
                        const unsigned char continuation =
                            static_cast<unsigned char>(value[index + offset]);
                        valid = (continuation & 0xC0) == 0x80;
                    }
                    if (valid && length == 3) {
                        const unsigned char second =
                            static_cast<unsigned char>(value[index + 1]);
                        valid = (ch != 0xE0 || second >= 0xA0) &&
                            (ch != 0xED || second <= 0x9F);
                    } else if (valid && length == 4) {
                        const unsigned char second =
                            static_cast<unsigned char>(value[index + 1]);
                        valid = (ch != 0xF0 || second >= 0x90) &&
                            (ch != 0xF4 || second <= 0x8F);
                    }
                    if (valid) {
                        escaped.write(value.data() + index,
                                      static_cast<std::streamsize>(length));
                        index += length - 1;
                    } else {
                        escaped << "\\u00" << std::hex << std::setw(2)
                                << std::setfill('0') << static_cast<int>(ch)
                                << std::dec;
                    }
                }
        }
    }
    return escaped.str();
}

inline std::string SKJH_Hex(DWORD64 value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << value;
    return out.str();
}

struct SKJH_TypeInfoProbe {
    DWORD64 slot = 0;
    DWORD64 klass = 0;
    DWORD64 staticFields = 0;
    std::string name;
    std::string nameSpace;
    bool valid = false;
};

inline SKJH_TypeInfoProbe SKJH_ProbeTypeInfo(DWORD64 moduleBase, DWORD64 rva,
                                              const char* expectedNamespace,
                                              const char* expectedName) {
    SKJH_TypeInfoProbe probe;
    probe.slot = moduleBase + rva;
    if (!mem.Read(probe.slot, &probe.klass, sizeof(probe.klass))) return probe;
    if (!Mem::IsUserAddress(probe.klass)) return probe;
    probe.name = SKJH_ReadKlassName(probe.klass);
    probe.nameSpace = SKJH_ReadKlassNamespace(probe.klass);
    if (!mem.Read(probe.klass + g_RuntimeOffsets.il2cppClassStaticFields,
                  &probe.staticFields, sizeof(probe.staticFields))) return probe;
    probe.valid = probe.name == expectedName && probe.nameSpace == expectedNamespace &&
                  Mem::IsUserAddress(probe.staticFields);
    return probe;
}

inline bool SKJH_FindRuntimeFieldOffset(DWORD64 klass, const char* fieldName,
                                        uint32_t& result) {
    result = 0;
    if (!Mem::IsUserAddress(klass) || !fieldName || !fieldName[0]) return false;
    for (int depth = 0; depth < 16 && Mem::IsUserAddress(klass); ++depth) {
        DWORD64 fields = 0;
        uint16_t fieldCount = 0;
        if (!mem.Read(klass + 0x80, &fields, sizeof(fields)) ||
            !mem.Read(klass + 0x120, &fieldCount, sizeof(fieldCount)) ||
            fieldCount > 4096) return false;
        if (fieldCount && !Mem::IsUserAddress(fields)) return false;
        for (uint16_t index = 0; index < fieldCount; ++index) {
            const DWORD64 field = fields + static_cast<DWORD64>(index) * 0x20;
            DWORD64 nameAddress = 0;
            if (!mem.Read(field, &nameAddress, sizeof(nameAddress))) return false;
            if (SKJH_ReadNativeString(nameAddress) != fieldName) continue;
            int32_t offset = 0;
            if (!mem.Read(field + 0x18, &offset, sizeof(offset)) || offset < 0)
                return false;
            result = static_cast<uint32_t>(offset);
            return true;
        }
        DWORD64 parent = 0;
        if (!mem.Read(klass + g_RuntimeOffsets.il2cppClassParent,
                      &parent, sizeof(parent)) || parent == klass) return false;
        klass = parent;
    }
    return false;
}

inline bool SKJH_RuntimeFieldMatches(DWORD64 klass, const char* fieldName,
                                     uint32_t expected) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint32_t actual = 0;
        if (SKJH_FindRuntimeFieldOffset(klass, fieldName, actual))
            return actual == expected;
        if (attempt != 2) Sleep(2);
    }
    return false;
}

struct SKJH_AobCandidate {
    uint64_t rva = 0;
    enum class Identity : uint8_t {
        Unknown,
        Match,
        Mismatch,
    } identity = Identity::Unknown;
};

inline int SKJH_ScoreCompiledTypeInfoProfile(
        DWORD64 moduleBase, const SKJH_TypeInfoRvaProfile& profile) {
    SKJH_ApplyTypeInfoRvaProfile(profile);
    if (!moduleBase) return 0;

    const auto readType = [moduleBase](uint64_t rva,
                                       DWORD64& klass,
                                       DWORD64& fields) {
        klass = 0;
        fields = 0;
        if (!mem.Read(moduleBase + rva, &klass, sizeof(klass)) ||
            !Mem::IsUserAddress(klass) ||
            !mem.Read(klass + g_RuntimeOffsets.il2cppClassStaticFields,
                      &fields, sizeof(fields)) ||
            !Mem::IsUserAddress(fields)) {
            return false;
        }
        return true;
    };

    int score = 0;
    DWORD64 entityKlass = 0;
    DWORD64 entityFields = 0;
    if (readType(profile.entityManager, entityKlass, entityFields)) {
        score += 2;
        DWORD64 instance = 0;
        DWORD64 dictionary = 0;
        if (mem.Read(entityFields + g_RuntimeOffsets.entityManagerInstance,
                     &instance, sizeof(instance)) &&
            Mem::IsUserAddress(instance)) {
            ++score;
            if (mem.Read(instance + g_RuntimeOffsets.entityManagerEntities,
                         &dictionary, sizeof(dictionary)) &&
                Mem::IsUserAddress(dictionary)) {
                score += 2;
                const DWORD64 entries = mem.Read<DWORD64>(dictionary + 0x18);
                const int32_t count = mem.Read<int32_t>(dictionary + 0x20);
                if (Mem::IsUserAddress(entries) && count > 0 &&
                    count <= 20000) ++score;
            }
        }
    }

    DWORD64 mcKlass = 0;
    DWORD64 mcFields = 0;
    if (readType(profile.mc, mcKlass, mcFields)) {
        score += 2;
        DWORD64 entityManager = 0;
        DWORD64 goManager = 0;
        if (mem.Read(mcFields + g_RuntimeOffsets.mcEntity,
                     &entityManager, sizeof(entityManager)) &&
            Mem::IsUserAddress(entityManager)) ++score;
        if (mem.Read(mcFields + g_RuntimeOffsets.mcEntityGo,
                     &goManager, sizeof(goManager)) &&
            Mem::IsUserAddress(goManager)) {
            ++score;
            const DWORD64 goDictionary = mem.Read<DWORD64>(
                goManager + g_RuntimeOffsets.mgrEntityGoGos);
            if (Mem::IsUserAddress(goDictionary)) score += 2;
        }
    }

    DWORD64 commonKlass = 0;
    DWORD64 commonFields = 0;
    if (readType(profile.mcCommon, commonKlass, commonFields)) {
        score += 2;
        const DWORD64 tables = mem.Read<DWORD64>(
            commonFields + g_RuntimeOffsets.mcCommonTables);
        if (Mem::IsUserAddress(tables)) ++score;
    }
    return score;
}

inline bool SKJH_SelectCompiledTypeInfoProfile(DWORD64 moduleBase) {
    const SKJH_TypeInfoRvaProfile previous{
        g_RuntimeOffsets.entityManagerTypeInfo,
        g_RuntimeOffsets.mcTypeInfo,
        g_RuntimeOffsets.mcCommonTypeInfo,
        g_RuntimeOffsets.languageManagerTypeInfo};
    const SKJH_TypeInfoRvaProfile* best = nullptr;
    int bestScore = 0;
    for (const auto& profile : SKJH_CompiledTypeInfoProfiles) {
        const int score = SKJH_ScoreCompiledTypeInfoProfile(moduleBase, profile);
        if (score > bestScore) {
            bestScore = score;
            best = &profile;
        }
    }
    if (!best || bestScore < 6) {
        SKJH_ApplyTypeInfoRvaProfile(previous);
        return false;
    }
    SKJH_ApplyTypeInfoRvaProfile(*best);
    return true;
}

inline SKJH_AobCandidate::Identity SKJH_TypeInfoSemanticIdentity(
        DWORD64 moduleBase, uint64_t rva, const char* nameSpace,
        const char* name) {
    DWORD64 klass = 0;
    if (!mem.Read(moduleBase + rva, &klass, sizeof(klass)) ||
        !Mem::IsUserAddress(klass)) return SKJH_AobCandidate::Identity::Unknown;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::string actualName = SKJH_ReadKlassName(klass);
        const std::string actualNamespace = SKJH_ReadKlassNamespace(klass);
        if ((!actualName.empty() && actualName != name) ||
            (!actualNamespace.empty() && actualNamespace != nameSpace))
            return SKJH_AobCandidate::Identity::Mismatch;
        if (!actualName.empty() && !actualNamespace.empty()) {
            return SKJH_AobCandidate::Identity::Match;
        }
        if (attempt != 2) Sleep(2);
    }
    return SKJH_AobCandidate::Identity::Unknown;
}

inline bool SKJH_TypeInfoSemanticMatch(DWORD64 moduleBase, uint64_t rva,
                                        size_t, const char* nameSpace,
                                        const char* name) {
    return SKJH_TypeInfoSemanticIdentity(moduleBase, rva, nameSpace, name) ==
        SKJH_AobCandidate::Identity::Match;
}

inline bool SKJH_ResolveRuntimeTypeInfoSignatures(bool force = false,
                                                   DWORD budgetMs = 15000) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point deadline = budgetMs == INFINITE
        ? (Clock::time_point::max)()
        : Clock::now() + std::chrono::milliseconds(budgetMs);
    const auto expired = [&deadline]() { return Clock::now() >= deadline; };
    const DWORD64 moduleBase = mem.GetBase("GameAssembly.dll");
    const DWORD64 moduleSize = mem.GetBaseSize("GameAssembly.dll");
    if (!moduleBase || !moduleSize) return false;

    struct SignatureTarget {
        const std::vector<int16_t>* pattern;
        const char* nameSpace;
        const char* name;
        uint64_t baseline;
        size_t ripInstructionOffset;
    };
    static const std::vector<int16_t> kEntityManager{
        0x48,0x8B,0x05,-1,-1,-1,-1,0x83,0xB8,0xE0,0,0,0,0,0x75,0x0F,
        0x48,0x8B,0xC8,0xE8,-1,-1,-1,-1,0x48,0x8B,0x05,-1,-1,-1,-1,
        0x48,0x8B,0x80,0xB8,0,0,0,0x48,0x8B,0x00};
    static const std::vector<int16_t> kMc{
        0x48,0x8B,0x05,-1,-1,-1,-1,0x83,0xB8,0xE0,0,0,0,0,0x75,0x0F,
        0x48,0x8B,0xC8,0xE8,-1,-1,-1,-1,0x48,0x8B,0x05,-1,-1,-1,-1,
        0x48,0x8B,0x80,0xB8,0,0,0,0x48,0x8B,0x88,0x48,0x05,0,0};
    static const std::vector<int16_t> kMcCommon{
        0x48,0x8B,0x05,-1,-1,-1,-1,0x83,0xB8,0xE0,0,0,0,0,0x75,0x0F,
        0x48,0x8B,0xC8,0xE8,-1,-1,-1,-1,0x48,0x8B,0x05,-1,-1,-1,-1,
        0x48,0x8B,0x80,0xB8,0,0,0,0x48,0x8B,0x48,0x20};
    static const std::vector<int16_t> kLanguageManager{
        0x48,0x8B,0x0D,-1,-1,-1,-1,0x83,0xB9,0xE0,0,0,0,0,0x75,0x05,
        0xE8,-1,-1,-1,-1,0x33,0xD2,0xB9,0xD8,0x47,0xB6,0x76};

    const std::array<SignatureTarget, 4> targets{{
        {&kEntityManager, "WizardGames.Soc.Share.Framework", "EntityManager",
         g_RuntimeOffsets.entityManagerTypeInfo, 24},
        {&kMc, "WizardGames.Soc.SocClient.Manager", "Mc",
         g_RuntimeOffsets.mcTypeInfo, 24},
        {&kMcCommon, "WizardGames.Soc.Common.Manager", "McCommon",
         g_RuntimeOffsets.mcCommonTypeInfo, 24},
        {&kLanguageManager, "WizardGames.Soc.SocClient.Data", "LanguageManager",
         g_RuntimeOffsets.languageManagerTypeInfo, 0},
    }};

    const auto profileMatches = [&](const SKJH_TypeInfoRvaProfile& profile) {
        const std::array<uint64_t, 3> rvas{{
            profile.entityManager, profile.mc, profile.mcCommon}};
        for (size_t index = 0; index < rvas.size(); ++index) {
            if (expired() ||
                !SKJH_TypeInfoSemanticMatch(
                    moduleBase, rvas[index], index,
                    targets[index].nameSpace, targets[index].name)) {
                return false;
            }
        }
        return true;
    };
    const SKJH_TypeInfoRvaProfile activeProfile{
        g_RuntimeOffsets.entityManagerTypeInfo,
        g_RuntimeOffsets.mcTypeInfo,
        g_RuntimeOffsets.mcCommonTypeInfo,
        g_RuntimeOffsets.languageManagerTypeInfo,
    };
    if (!force && SKJH_SelectCompiledTypeInfoProfile(moduleBase))
        return true;
    if (profileMatches(activeProfile)) return true;
    for (const auto& profile : SKJH_CompiledTypeInfoProfiles) {
        if (profile.entityManager == activeProfile.entityManager &&
            profile.mc == activeProfile.mc &&
            profile.mcCommon == activeProfile.mcCommon) {
            continue;
        }
        if (!profileMatches(profile)) continue;
        SKJH_ApplyTypeInfoRvaProfile(profile);
        return true;
    }

    // Normal startup deliberately stops after the constant-time compiled
    // profile checks. Full module scanning remains an explicit diagnostic.
    if (!force) return false;

    std::array<std::vector<SKJH_AobCandidate>, 4> candidates;
    const size_t longestPattern = std::max_element(targets.begin(), targets.end(),
        [](const SignatureTarget& left, const SignatureTarget& right) {
            return left.pattern->size() < right.pattern->size();
        })->pattern->size();
    constexpr DWORD64 kScanBlockSize = 0x100000;
    constexpr DWORD64 kFallbackBlockSize = 0x1000;
    std::vector<unsigned char> page(kScanBlockSize + longestPattern - 1);
    std::vector<unsigned char> fallback(kFallbackBlockSize + longestPattern - 1);
    const DWORD64 scanSize = moduleSize;
    const auto scanBuffer = [&](DWORD64 bufferOffset, const unsigned char* data,
                                DWORD bytes) {
        for (size_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
            const SignatureTarget& target = targets[targetIndex];
            const std::vector<int16_t>& pattern = *target.pattern;
            for (DWORD index = 0; index + pattern.size() <= bytes; ++index) {
                bool matches = true;
                for (size_t byteIndex = 0; byteIndex < pattern.size(); ++byteIndex) {
                    if (pattern[byteIndex] >= 0 &&
                        data[index + byteIndex] != pattern[byteIndex]) {
                        matches = false;
                        break;
                    }
                }
                if (!matches) continue;
                int32_t displacement = 0;
                std::memcpy(&displacement, data + index +
                                target.ripInstructionOffset + 3,
                            sizeof(displacement));
                const DWORD64 instruction = moduleBase + bufferOffset + index +
                    target.ripInstructionOffset;
                const DWORD64 slot = instruction + 7 + displacement;
                if (slot < moduleBase || slot > moduleBase + moduleSize - sizeof(DWORD64))
                    continue;
                const uint64_t rva = slot - moduleBase;
                const auto identity = SKJH_TypeInfoSemanticIdentity(
                    moduleBase, rva, target.nameSpace, target.name);
                if (identity != SKJH_AobCandidate::Identity::Mismatch)
                    candidates[targetIndex].push_back({rva, identity});
            }
        }
    };
    const auto refreshIdentities = [&]() {
        for (size_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
            const SignatureTarget& target = targets[targetIndex];
            for (SKJH_AobCandidate& candidate : candidates[targetIndex]) {
                if (candidate.identity != SKJH_AobCandidate::Identity::Unknown)
                    continue;
                candidate.identity = SKJH_TypeInfoSemanticIdentity(
                    moduleBase, candidate.rva, target.nameSpace, target.name);
            }
        }
        return std::all_of(candidates.begin(), candidates.end(),
            [](const std::vector<SKJH_AobCandidate>& values) {
                return std::any_of(values.begin(), values.end(),
                    [](const SKJH_AobCandidate& candidate) {
                        return candidate.identity == SKJH_AobCandidate::Identity::Match;
                    });
            });
    };
    bool allExact = false;
    for (DWORD64 offset = 0; offset < scanSize && !allExact;
         offset += kScanBlockSize) {
        if (expired()) return false;
        const DWORD bytes = static_cast<DWORD>((std::min)(
            static_cast<DWORD64>(page.size()), scanSize - offset));
        if (mem.Read(moduleBase + offset, page.data(), bytes)) {
            scanBuffer(offset, page.data(), bytes);
            allExact = refreshIdentities();
            continue;
        }

        const DWORD64 primaryBytes = (std::min)(kScanBlockSize, scanSize - offset);
        for (DWORD64 inner = 0; inner < primaryBytes && !allExact;
             inner += kFallbackBlockSize) {
            if (expired()) return false;
            const DWORD fallbackBytes = static_cast<DWORD>((std::min)(
                static_cast<DWORD64>(fallback.size()), scanSize - offset - inner));
            if (!mem.Read(moduleBase + offset + inner, fallback.data(), fallbackBytes))
                continue;
            scanBuffer(offset + inner, fallback.data(), fallbackBytes);
            allExact = refreshIdentities();
        }
    }

    std::array<uint64_t, 4> resolved{};
    for (size_t index = 0; index < targets.size(); ++index) {
        if (expired()) return false;
        auto& matches = candidates[index];
        if (matches.empty()) return false;
        std::sort(matches.begin(), matches.end(),
            [](const SKJH_AobCandidate& left, const SKJH_AobCandidate& right) {
                if (left.rva != right.rva) return left.rva < right.rva;
                return left.identity == SKJH_AobCandidate::Identity::Match &&
                    right.identity != SKJH_AobCandidate::Identity::Match;
            });
        matches.erase(std::unique(matches.begin(), matches.end(),
            [](const SKJH_AobCandidate& left, const SKJH_AobCandidate& right) {
                return left.rva == right.rva;
            }), matches.end());
        const auto exact = std::find_if(matches.begin(), matches.end(),
            [](const SKJH_AobCandidate& candidate) {
                return candidate.identity == SKJH_AobCandidate::Identity::Match;
            });
        if (exact == matches.end()) return false;
        resolved[index] = exact->rva;
    }

    const SKJH_RuntimeOffsets previous = g_RuntimeOffsets;
    g_RuntimeOffsets.entityManagerTypeInfo = resolved[0];
    g_RuntimeOffsets.mcTypeInfo = resolved[1];
    g_RuntimeOffsets.mcCommonTypeInfo = resolved[2];
    g_RuntimeOffsets.languageManagerTypeInfo = resolved[3];
    for (size_t index = 0; index < targets.size(); ++index) {
        if (expired()) {
            g_RuntimeOffsets = previous;
            return false;
        }
        DWORD64 klass = 0;
        if (!mem.Read(moduleBase + resolved[index], &klass, sizeof(klass)) ||
            !Mem::IsUserAddress(klass)) {
            g_RuntimeOffsets = previous;
            return false;
        }
        if (!SKJH_TypeInfoSemanticMatch(moduleBase, resolved[index], index,
                targets[index].nameSpace, targets[index].name)) {
            g_RuntimeOffsets = previous;
            return false;
        }
    }
    return true;
}

inline bool SKJH_ValidateRuntimeSdk() {
    const DWORD64 gameAssembly = mem.GetBase("GameAssembly.dll");
    if (!gameAssembly || !SdkManifestIsSane()) return false;
    const SKJH_TypeInfoProbe entityManager = SKJH_ProbeTypeInfo(
        gameAssembly, g_RuntimeOffsets.entityManagerTypeInfo,
        "WizardGames.Soc.Share.Framework", "EntityManager");
    const SKJH_TypeInfoProbe mc = SKJH_ProbeTypeInfo(
        gameAssembly, g_RuntimeOffsets.mcTypeInfo,
        "WizardGames.Soc.SocClient.Manager", "Mc");
    const SKJH_TypeInfoProbe mcCommon = SKJH_ProbeTypeInfo(
        gameAssembly, g_RuntimeOffsets.mcCommonTypeInfo,
        "WizardGames.Soc.Common.Manager", "McCommon");
    const bool semanticTypes = entityManager.valid && mc.valid &&
        mcCommon.valid;
    const SKJH_TypeInfoRvaProfile activeProfile{
        g_RuntimeOffsets.entityManagerTypeInfo,
        g_RuntimeOffsets.mcTypeInfo,
        g_RuntimeOffsets.mcCommonTypeInfo,
        g_RuntimeOffsets.languageManagerTypeInfo};
    const bool structuralTypes =
        SKJH_ScoreCompiledTypeInfoProfile(gameAssembly, activeProfile) >= 6;
    if (!semanticTypes && !structuralTypes) return false;
    const DWORD64 tables = mem.Read<DWORD64>(
        mcCommon.staticFields + g_RuntimeOffsets.mcCommonTables);
    return Mem::IsUserAddress(SKJH_GetEntitiesDictionary()) &&
        Mem::IsUserAddress(SKJH_GetMcStaticFields()) && Mem::IsUserAddress(tables);
}

inline bool SKJH_WaitForRuntimeSdk(DWORD timeoutMs) {
    const auto started = std::chrono::steady_clock::now();
    while (true) {
        DWORD remainingMs = INFINITE;
        if (timeoutMs != INFINITE) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= timeoutMs) return false;
            remainingMs = timeoutMs - static_cast<DWORD>(elapsed);
        }
        SKJH_ResolveRuntimeTypeInfoSignatures(false, remainingMs);
        if (SKJH_ValidateRuntimeSdk()) return true;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        if (timeoutMs != INFINITE && elapsed >= timeoutMs) return false;
        mem.RefreshTlb();
        Sleep(250);
    }
}

struct SKJH_TransformDiagnostic {
    const char* stage = "managed_transform";
    FVector position = {0, 0, 0};
    int hierarchyDepth = 0;
    bool valid = false;
};

inline SKJH_TransformDiagnostic SKJH_DiagnoseUnityTransform(
        DWORD64 managedTransform) {
    SKJH_TransformDiagnostic diagnostic;
    if (!Mem::IsUserAddress(managedTransform)) return diagnostic;

    const DWORD64 nativeTransform = mem.Read<DWORD64>(
        managedTransform + g_RuntimeOffsets.unityObjectCachedPtr);
    if (!Mem::IsUserAddress(nativeTransform)) {
        diagnostic.stage = "native_transform";
        return diagnostic;
    }
    const DWORD64 transformData = mem.Read<DWORD64>(
        nativeTransform + g_RuntimeOffsets.unityNativeData);
    if (!Mem::IsUserAddress(transformData)) {
        diagnostic.stage = "transform_data";
        return diagnostic;
    }
    int32_t transformIndex = -1;
    if (!mem.Read(nativeTransform + g_RuntimeOffsets.unityNativeIndex,
                  &transformIndex, sizeof(transformIndex)) ||
        transformIndex < 0 || transformIndex > 1000000) {
        diagnostic.stage = "transform_index";
        return diagnostic;
    }
    const DWORD64 transforms = mem.Read<DWORD64>(
        transformData + g_RuntimeOffsets.unityDataTransforms);
    const DWORD64 parents = mem.Read<DWORD64>(
        transformData + g_RuntimeOffsets.unityDataParents);
    if (!Mem::IsUserAddress(transforms) || !Mem::IsUserAddress(parents)) {
        diagnostic.stage = "transform_arrays";
        return diagnostic;
    }

    const SKJH_UnityTransformResult world =
        SKJH_ReadUnityTransform(managedTransform);
    if (!world.valid || !SKJH_IsFiniteVector(world.position)) {
        diagnostic.stage = "world_transform";
        return diagnostic;
    }
    diagnostic.stage = "ok";
    diagnostic.position = world.position;
    diagnostic.hierarchyDepth = world.hierarchyDepth;
    diagnostic.valid = true;
    return diagnostic;
}

inline std::vector<SKJH_Entity> SKJH_EnumerateBoneProbePlayers(
        size_t playerLimit, int32_t* entityCountOut = nullptr) {
    std::vector<SKJH_Entity> players;
    const DWORD64 dictionary = SKJH_GetEntitiesDictionary();
    if (!Mem::IsUserAddress(dictionary)) return players;
    const DWORD64 entries = mem.Read<DWORD64>(dictionary + 0x18);
    const int32_t count = mem.Read<int32_t>(dictionary + 0x20);
    if (entityCountOut) *entityCountOut = count;
    if (!Mem::IsUserAddress(entries) || count <= 0 || count > 20000)
        return players;
    const int32_t capacity = mem.Read<int32_t>(entries + 0x18);
    if (capacity <= 0 || capacity > 40000) return players;

    DWORD64 playerKlass = 0;
    const DWORD64 localPlayer = SKJH_GetLocalPlayer();
    if (Mem::IsUserAddress(localPlayer)) {
        const DWORD64 localKlass = mem.Read<DWORD64>(localPlayer);
        if (SKJH_KlassIsOrDerivesFrom(localKlass, "PlayerEntity"))
            playerKlass = localKlass;
    }
    std::unordered_map<DWORD64, bool> klassMatches;
    const int32_t scanCount = (std::min)(count, capacity);
    players.reserve((std::min)(playerLimit, static_cast<size_t>(64)));
    for (int32_t index = 0;
         index < scanCount && players.size() < playerLimit; ++index) {
        const DWORD64 entry = entries + 0x20 +
            static_cast<DWORD64>(index) * 0x18;
        if (mem.Read<int32_t>(entry) < 0) continue;
        const DWORD64 entityPtr = mem.Read<DWORD64>(entry + 0x10);
        if (!Mem::IsUserAddress(entityPtr)) continue;
        const DWORD64 klass = mem.Read<DWORD64>(entityPtr);
        bool isPlayer = playerKlass && klass == playerKlass;
        if (!isPlayer && Mem::IsUserAddress(klass)) {
            const auto known = klassMatches.find(klass);
            if (known != klassMatches.end()) {
                isPlayer = known->second;
            } else {
                isPlayer = SKJH_KlassIsOrDerivesFrom(klass, "PlayerEntity");
                klassMatches.emplace(klass, isPlayer);
                if (isPlayer && !playerKlass) playerKlass = klass;
            }
        }
        if (!isPlayer) continue;

        SKJH_Entity entity;
        entity.entityPtr = entityPtr;
        entity.klass = klass;
        entity.className = "PlayerEntity";
        entity.type = SKJH_PLAYER;
        entity.entityId = mem.Read<int64_t>(
            entityPtr + g_RuntimeOffsets.entityEntityId);
        entity.pos = SKJH_GetPosition(entityPtr, klass);
        entity.botKnown = SKJH_GetBoolProperty(
            entityPtr, klass, "IsRobot", entity.isBot);
        players.push_back(std::move(entity));
    }
    return players;
}

inline bool SKJH_RunBoneProbe(const std::filesystem::path& outputPath,
                              size_t playerLimit = 128) {
    const DWORD64 gameAssembly = mem.GetBase("GameAssembly.dll");
    const SKJH_TypeInfoProbe entityManager = SKJH_ProbeTypeInfo(
        gameAssembly, g_RuntimeOffsets.entityManagerTypeInfo,
        "WizardGames.Soc.Share.Framework", "EntityManager");
    const SKJH_TypeInfoProbe mc = SKJH_ProbeTypeInfo(
        gameAssembly, g_RuntimeOffsets.mcTypeInfo,
        "WizardGames.Soc.SocClient.Manager", "Mc");
    const DWORD64 entityDictionary = SKJH_GetEntitiesDictionary();
    const DWORD64 entityGoManager = SKJH_GetMgrEntityGo();
    const auto entityGoMap = SKJH_ReadEntityGoMap();
    int32_t entityCount = 0;
    const std::vector<SKJH_Entity> entities =
        SKJH_EnumerateBoneProbePlayers(playerLimit, &entityCount);

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << std::fixed << std::setprecision(3);
    output << "{\n  \"schema\": 2,\n";
    output << "  \"global\": {\"module\": "
           << (gameAssembly ? "true" : "false")
           << ", \"sdkManifest\": " << (SdkManifestIsSane() ? "true" : "false")
           << ", \"entityManagerType\": " << (entityManager.valid ? "true" : "false")
           << ", \"mcType\": " << (mc.valid ? "true" : "false")
           << ", \"entityDictionary\": "
           << (Mem::IsUserAddress(entityDictionary) ? "true" : "false")
           << ", \"entityGoManager\": "
           << (Mem::IsUserAddress(entityGoManager) ? "true" : "false")
           << ", \"entityGoCount\": " << entityGoMap.size()
           << ", \"entityCount\": " << entityCount << "},\n";
    output << "  \"players\": [\n";

    size_t playerCount = 0;
    size_t mappedCount = 0;
    size_t matchingGoCount = 0;
    size_t rootValidCount = 0;
    size_t completeCount = 0;
    bool firstPlayer = true;
    for (const SKJH_Entity& entity : entities) {
        if (entity.type != SKJH_PLAYER || playerCount >= playerLimit) continue;
        const size_t playerIndex = playerCount++;
        if (!firstPlayer) output << ",\n";
        firstPlayer = false;

        DWORD64 playerGo = 0;
        const auto mapped = entityGoMap.find(entity.entityId);
        if (mapped != entityGoMap.end()) playerGo = mapped->second;
        const bool hasPlayerGo = Mem::IsUserAddress(playerGo);
        if (hasPlayerGo) ++mappedCount;

        bool backReferenceValid = false;
        bool classValid = false;
        std::string goClass;
        DWORD64 goKlass = 0;
        DWORD64 goKlassName = 0;
        if (hasPlayerGo) {
            backReferenceValid = SKJH_HasMatchingPlayerEntity(
                playerGo, entity.entityPtr);
            goKlass = mem.Read<DWORD64>(playerGo);
            if (Mem::IsUserAddress(goKlass)) {
                goKlassName = mem.Read<DWORD64>(
                    goKlass + g_RuntimeOffsets.il2cppClassName);
            }
            goClass = SKJH_ReadKlassName(goKlass);
            classValid = SKJH_IsMatchingPlayerGo(
                playerGo, entity.entityPtr);
        }
        if (backReferenceValid && classValid) ++matchingGoCount;

        SKJH_TransformDiagnostic root;
        if (backReferenceValid && classValid) {
            root = SKJH_DiagnoseUnityTransform(mem.Read<DWORD64>(
                playerGo + g_RuntimeOffsets.basePlayerGoRootBone));
        } else {
            root.stage = !hasPlayerGo ? "entity_go_missing" :
                (!backReferenceValid ? "entity_back_reference" : "go_class");
        }
        if (root.valid) ++rootValidCount;

        std::array<SKJH_TransformDiagnostic, BONE_COUNT> boneDiagnostics{};
        std::array<bool, BONE_COUNT> poseValid{};
        SKJH_BoneData sampledBones[BONE_COUNT]{};
        int validBones = 0;
        bool bindingValid = false;
        const char* bindingSource = "none";
        DWORD64 bindingOriginalTree = 0;
        DWORD64 bindingPointCache = 0;
        int32_t bindingOriginalNodeCount = 0;
        int32_t bindingOriginalTransformIdCount = 0;
        SKJH_SkeletonCandidateProbe candidateProbe{};
        const char* bindingStage = "not_attempted";
        std::array<const char*, 3> candidateStages{{
            "not_attempted", "not_attempted", "not_attempted"}};
        if (backReferenceValid && classValid) {
            SKJH_PlayerSkeletonBinding* binding =
                SKJH_GetPlayerSkeletonBinding(playerGo);
            bindingValid = binding != nullptr;
            if (binding) {
                bindingSource = binding->sourceKind ==
                    SKJH_SkeletonSourceKind::OriginalPointCache
                    ? "original_point_cache" : "serialize_tree";
                bindingOriginalTree = binding->originalTree;
                bindingPointCache = binding->pointCache;
                bindingOriginalNodeCount = binding->originalNodeCount;
                bindingOriginalTransformIdCount =
                    binding->originalTransformIdCount;
            }
            bindingStage = SKJH_GetSkeletonBindingStage();
            candidateStages = SKJH_GetSkeletonCandidateStages();
            candidateProbe = SKJH_GetSkeletonCandidateProbe();
            validBones = SKJH_ReadPlayerBones(
                playerGo, entity.entityPtr, entity.pos, sampledBones);
            for (int bone = 0; bone < BONE_COUNT; ++bone) {
                auto& diagnostic = boneDiagnostics[bone];
                diagnostic.valid = sampledBones[bone].valid;
                diagnostic.position = sampledBones[bone].worldPos;
                diagnostic.stage = diagnostic.valid
                    ? (bindingValid ? "tree_pose" : "direct_anchor")
                    : "missing";
                poseValid[bone] = diagnostic.valid;
            }
        }

        bool anchorsValid = poseValid[BONE_HEAD] && poseValid[BONE_NECK] &&
            poseValid[BONE_BODY] &&
            (poseValid[BONE_LEFT_FOOT] || poseValid[BONE_RIGHT_FOOT]);
        float maximumSeparationSq = 0.0f;
        if (anchorsValid) {
            for (int first = 0; first < BONE_COUNT; ++first) {
                if (!poseValid[first]) continue;
                for (int second = first + 1; second < BONE_COUNT; ++second) {
                    if (!poseValid[second]) continue;
                    const float dx = boneDiagnostics[first].position.X -
                        boneDiagnostics[second].position.X;
                    const float dy = boneDiagnostics[first].position.Y -
                        boneDiagnostics[second].position.Y;
                    const float dz = boneDiagnostics[first].position.Z -
                        boneDiagnostics[second].position.Z;
                    maximumSeparationSq = (std::max)(maximumSeparationSq,
                        dx*dx + dy*dy + dz*dz);
                }
            }
        }
        const bool geometryValid = anchorsValid &&
            std::isfinite(maximumSeparationSq) && maximumSeparationSq <= 25.0f;
        const bool complete = bindingValid && validBones == BONE_COUNT &&
            geometryValid;
        if (complete) ++completeCount;

        std::string failureStage = "complete";
        if (!hasPlayerGo) failureStage = "entity_go_missing";
        else if (!backReferenceValid) failureStage = "entity_back_reference";
        else if (!classValid) failureStage = "go_class";
        else if (!bindingValid) failureStage = "skeleton_binding";
        else {
            for (int bone = 0; bone < BONE_COUNT; ++bone) {
                if (!boneDiagnostics[bone].valid) {
                    failureStage = std::string("bone_") + BoneNames[bone] +
                        "_" + boneDiagnostics[bone].stage;
                    break;
                }
            }
            if (failureStage == "complete" && !anchorsValid)
                failureStage = "humanoid_anchors";
            else if (failureStage == "complete" && !geometryValid)
                failureStage = "humanoid_geometry";
        }

        output << "    {\"index\": " << playerIndex
               << ", \"botKnown\": " << (entity.botKnown ? "true" : "false")
               << ", \"isBot\": " << (entity.isBot ? "true" : "false")
               << ", \"entityGoMapped\": " << (hasPlayerGo ? "true" : "false")
               << ", \"entityGo\": \"" << SKJH_Hex(playerGo) << "\""
               << ", \"backReference\": "
               << (backReferenceValid ? "true" : "false")
               << ", \"goKlass\": \"" << SKJH_Hex(goKlass) << "\""
               << ", \"goKlassName\": \"" << SKJH_Hex(goKlassName) << "\""
               << ", \"goClass\": \"" << SKJH_JsonEscape(goClass) << "\""
               << ", \"classValid\": " << (classValid ? "true" : "false")
               << ", \"bindingValid\": "
               << (bindingValid ? "true" : "false")
               << ", \"bindingSource\": \"" << bindingSource << "\""
               << ", \"bindingOriginalTree\": \""
               << SKJH_Hex(bindingOriginalTree) << "\""
               << ", \"bindingPointCache\": \""
               << SKJH_Hex(bindingPointCache) << "\""
               << ", \"bindingOriginalNodeCount\": "
               << bindingOriginalNodeCount
               << ", \"bindingOriginalTransformIdCount\": "
               << bindingOriginalTransformIdCount
               << ", \"candidateProbe\": {\"targetRootPoint\": \""
               << SKJH_Hex(candidateProbe.targetRootPoint)
               << "\", \"pointCache\": \""
               << SKJH_Hex(candidateProbe.pointCache)
               << "\", \"originalTree\": \""
               << SKJH_Hex(candidateProbe.originalTree)
               << "\", \"keyArray\": \""
               << SKJH_Hex(candidateProbe.keyArray)
               << "\", \"valueArray\": \""
               << SKJH_Hex(candidateProbe.valueArray)
               << "\", \"nodeData\": \""
               << SKJH_Hex(candidateProbe.nodeData)
               << "\", \"transformIdData\": \""
               << SKJH_Hex(candidateProbe.transformIdData)
               << "\", \"keyCount\": " << candidateProbe.keyCount
               << ", \"valueCount\": " << candidateProbe.valueCount
               << ", \"nodeCount\": " << candidateProbe.nodeCount
               << ", \"transformIdCount\": "
               << candidateProbe.transformIdCount
               << ", \"transformIdSample\": [";
         for (int32_t sample = 0;
              sample < candidateProbe.transformIdSampleCount; ++sample) {
             if (sample) output << ',';
             output << candidateProbe.transformIdSample[
                 static_cast<size_t>(sample)];
         }
         output << "], \"duplicateTransformId\": "
                << candidateProbe.duplicateTransformId
                << ", \"duplicateTransformIdIndex\": "
                << candidateProbe.duplicateTransformIdIndex
                << ", \"duplicateTransformIdFirstIndex\": "
                << candidateProbe.duplicateTransformIdFirstIndex
                << ", \"selectedInstanceIdOffset\": "
                << candidateProbe.selectedInstanceIdOffset
                << ", \"selectedInstanceIdMatches\": "
                << candidateProbe.selectedInstanceIdMatches
                << ", \"originalRootInstanceId\": "
                << candidateProbe.originalRootInstanceId
                << ", \"selectedRootInstanceId\": "
                << candidateProbe.selectedRootInstanceId
                << ", \"parentMismatchFirst\": "
                << candidateProbe.parentMismatchFirst
                << ", \"parentMismatchSecond\": "
                << candidateProbe.parentMismatchSecond
                << ", \"parentMismatchOriginalFirst\": "
                << candidateProbe.parentMismatchOriginalFirst
                << ", \"parentMismatchOriginalSecond\": "
                << candidateProbe.parentMismatchOriginalSecond
                << ", \"parentMismatchNativeFirst\": "
                << candidateProbe.parentMismatchNativeFirst
               << ", \"parentMismatchNativeSecond\": "
               << candidateProbe.parentMismatchNativeSecond
               << ", \"parentInterpretation\": "
               << candidateProbe.parentInterpretation
               << ", \"parentNodeIndexMismatchCount\": "
               << candidateProbe.parentNodeIndexMismatchCount
               << ", \"parentBoneIndexMismatchCount\": "
               << candidateProbe.parentBoneIndexMismatchCount
               << ", \"parentNativeMismatchCount\": "
               << candidateProbe.parentNativeMismatchCount
               << ", \"parentRelationsCompared\": "
               << candidateProbe.parentRelationsCompared
               << ", \"namedOriginalIndices\": [";
         for (int bone = 0; bone < BONE_COUNT; ++bone) {
             if (bone) output << ',';
             output << candidateProbe.namedOriginalIndices[bone];
         }
         output << "], \"namedNodeIndexParents\": [";
         for (int bone = 0; bone < BONE_COUNT; ++bone) {
             if (bone) output << ',';
             output << candidateProbe.namedNodeIndexParents[bone];
         }
         output << "], \"namedBoneIndexParents\": [";
         for (int bone = 0; bone < BONE_COUNT; ++bone) {
             if (bone) output << ',';
             output << candidateProbe.namedBoneIndexParents[bone];
         }
         output << "], \"namedNativeIndices\": [";
         for (int bone = 0; bone < BONE_COUNT; ++bone) {
             if (bone) output << ',';
             output << candidateProbe.namedNativeIndices[bone];
         }
         output << "], \"namedNativeParents\": [";
         for (int bone = 0; bone < BONE_COUNT; ++bone) {
             if (bone) output << ',';
             output << candidateProbe.namedNativeParents[bone];
         }
         output << "]}"
               << ", \"bindingStage\": \"" << bindingStage << "\""
               << ", \"candidateStages\": [\"" << candidateStages[0]
               << "\",\"" << candidateStages[1] << "\",\""
               << candidateStages[2] << "\"]"
               << ", \"root\": {\"stage\": \"" << root.stage
               << "\", \"valid\": " << (root.valid ? "true" : "false")
               << "}, \"validBones\": " << validBones
               << ", \"anchorsValid\": " << (anchorsValid ? "true" : "false")
               << ", \"geometryValid\": " << (geometryValid ? "true" : "false")
               << ", \"failureStage\": \"" << failureStage
               << "\", \"bones\": [";
        for (int bone = 0; bone < BONE_COUNT; ++bone) {
            const SKJH_TransformDiagnostic& diagnostic = boneDiagnostics[bone];
            output << "{\"name\": \"" << BoneNames[bone]
                   << "\", \"stage\": \"" << diagnostic.stage
                   << "\", \"valid\": " << (diagnostic.valid ? "true" : "false")
                   << ", \"depth\": " << diagnostic.hierarchyDepth << '}';
            if (bone + 1 != BONE_COUNT) output << ',';
        }
        output << "]}";
    }
    output << "\n  ],\n";

    // Remote players may legitimately have no loaded model at the current
    // camera LOD. One complete sample proves the full binding/transform chain;
    // mapping and root checks still have to pass for every enumerated player.
    const bool completeCoverage = completeCount > 0;
    const bool valid = playerCount > 0 && mappedCount == playerCount &&
        matchingGoCount == playerCount && rootValidCount == playerCount &&
        completeCoverage;
    output << "  \"summary\": {\"players\": " << playerCount
           << ", \"mapped\": " << mappedCount
           << ", \"matchingGo\": " << matchingGoCount
           << ", \"rootValid\": " << rootValidCount
           << ", \"complete\": " << completeCount
           << ", \"valid\": " << (valid ? "true" : "false") << "}\n}\n";
    output.close();
    return output.good() && valid;
}

inline bool SKJH_RunDmaProbe(const std::filesystem::path& outputPath,
                              size_t entitySampleLimit = 20000) {
    const DWORD64 gameAssembly = mem.GetBase("GameAssembly.dll");
    const DWORD64 gameAssemblySize = mem.GetBaseSize("GameAssembly.dll");
    const SKJH_TypeInfoProbe entityManager = SKJH_ProbeTypeInfo(
        gameAssembly, g_RuntimeOffsets.entityManagerTypeInfo,
        "WizardGames.Soc.Share.Framework", "EntityManager");
    const SKJH_TypeInfoProbe mc = SKJH_ProbeTypeInfo(
        gameAssembly, g_RuntimeOffsets.mcTypeInfo,
        "WizardGames.Soc.SocClient.Manager", "Mc");
    const SKJH_TypeInfoProbe mcCommon = SKJH_ProbeTypeInfo(
        gameAssembly, g_RuntimeOffsets.mcCommonTypeInfo,
        "WizardGames.Soc.Common.Manager", "McCommon");

    const DWORD64 dictionary = SKJH_GetEntitiesDictionary();
    const DWORD64 entries = dictionary ? mem.Read<DWORD64>(dictionary + 0x18) : 0;
    const int32_t dictionaryCount = dictionary ? mem.Read<int32_t>(dictionary + 0x20) : 0;
    const int32_t entriesCapacity = entries ? mem.Read<int32_t>(entries + 0x18) : 0;
    const DWORD64 mcFields = SKJH_GetMcStaticFields();
    const DWORD64 localPlayer = SKJH_GetLocalPlayer();
    const DWORD64 entityGoManager = SKJH_GetMgrEntityGo();
    auto goMap = SKJH_ReadEntityGoMap();
    const SKJH_CameraData camera = SKJH_ReadCamera();
    const bool catalogRefreshed = SKJH_RefreshTemplateCatalog(true);
    const SKJH_TemplateCatalogStats templateStats = SKJH_GetTemplateCatalogStats();
    std::vector<SKJH_Entity> entities = SKJH_EnumerateEntities();

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << std::fixed << std::setprecision(3);
    output << "{\n";
    output << "  \"sdk\": {\"scriptLoaded\": "
           << (g_RuntimeOffsets.scriptLoaded ? "true" : "false")
           << ", \"dumpLoaded\": " << (g_RuntimeOffsets.dumpLoaded ? "true" : "false")
           << ", \"entityManagerRva\": \"" << SKJH_Hex(g_RuntimeOffsets.entityManagerTypeInfo)
           << "\", \"mcRva\": \"" << SKJH_Hex(g_RuntimeOffsets.mcTypeInfo) << "\"},\n";
    output << "  \"process\": {\"pid\": " << mem.pid
           << ", \"mainBase\": \"" << SKJH_Hex(mem.base)
           << "\", \"gameAssemblyBase\": \"" << SKJH_Hex(gameAssembly)
           << "\", \"gameAssemblySize\": " << gameAssemblySize << "},\n";

    const auto writeTypeInfo = [&output](const char* key, const SKJH_TypeInfoProbe& value) {
        output << "  \"" << key << "\": {\"slot\": \"" << SKJH_Hex(value.slot)
               << "\", \"klass\": \"" << SKJH_Hex(value.klass)
               << "\", \"staticFields\": \"" << SKJH_Hex(value.staticFields)
               << "\", \"namespace\": \"" << SKJH_JsonEscape(value.nameSpace)
               << "\", \"name\": \"" << SKJH_JsonEscape(value.name)
               << "\", \"valid\": " << (value.valid ? "true" : "false") << "},\n";
    };
    writeTypeInfo("entityManagerTypeInfo", entityManager);
    writeTypeInfo("mcTypeInfo", mc);
    writeTypeInfo("mcCommonTypeInfo", mcCommon);
    output << "  \"chains\": {\"entityDictionary\": \"" << SKJH_Hex(dictionary)
           << "\", \"entries\": \"" << SKJH_Hex(entries)
           << "\", \"dictionaryCount\": " << dictionaryCount
           << ", \"entriesCapacity\": " << entriesCapacity
           << ", \"mcStaticFields\": \"" << SKJH_Hex(mcFields)
           << "\", \"localPlayer\": \"" << SKJH_Hex(localPlayer)
           << "\", \"entityGoManager\": \"" << SKJH_Hex(entityGoManager)
           << "\", \"goCount\": " << goMap.size() << "},\n";
    output << "  \"templateCatalog\": {\"manager\": \""
           << SKJH_Hex(templateStats.manager) << "\", \"tablesLoaded\": "
           << templateStats.tablesLoaded << ", \"namesLoaded\": "
           << templateStats.namesLoaded << ", \"complete\": "
           << (templateStats.complete ? "true" : "false")
           << ", \"tableNames\": {";
    for (size_t tableIndex = 0; tableIndex < templateStats.namesPerTable.size();
         ++tableIndex) {
        const auto table = static_cast<SKJH_TemplateTable>(tableIndex);
        output << "\"" << SKJH_GetTemplateTableName(table) << "\": "
               << templateStats.namesPerTable[tableIndex];
        if (tableIndex + 1 != templateStats.namesPerTable.size()) output << ',';
    }
    output << "}, \"tableRows\": {";
    for (size_t tableIndex = 0; tableIndex < templateStats.rowsPerTable.size();
         ++tableIndex) {
        const auto table = static_cast<SKJH_TemplateTable>(tableIndex);
        output << "\"" << SKJH_GetTemplateTableName(table) << "\": "
               << templateStats.rowsPerTable[tableIndex];
        if (tableIndex + 1 != templateStats.rowsPerTable.size()) output << ',';
    }
    output << "}},\n";
    output << "  \"camera\": {\"valid\": " << (camera.valid ? "true" : "false")
           << ", \"position\": [" << camera.camLoc.X << ',' << camera.camLoc.Y << ','
           << camera.camLoc.Z << "], \"localPosition\": [" << camera.localPos.X << ','
           << camera.localPos.Y << ',' << camera.localPos.Z << "], \"fov\": "
           << camera.camFov << "},\n";
    output << "  \"entityCount\": " << entities.size() << ",\n";
    output << "  \"entities\": [\n";

    const size_t sampleCount = (std::min)(entitySampleLimit, entities.size());
    size_t detailedPlayerCount = 0;
    size_t playerCount = 0;
    size_t skeletonEligibleCount = 0;
    size_t playerGoMissingCount = 0;
    size_t fullSkeletonCount = 0;
    size_t unknownCount = 0;
    size_t templateEntityCount = 0;
    size_t namedTemplateCount = 0;
    size_t displayTemplateCount = 0;
    for (size_t index = 0; index < sampleCount; ++index) {
        const SKJH_Entity& entity = entities[index];
        int validBones = 0;
        DWORD64 entityGo = 0;
        auto go = goMap.find(entity.entityId);
        if (go != goMap.end()) entityGo = go->second;
        if (entity.type == SKJH_PLAYER) {
            for (int attempt = 0; attempt < 3 && validBones != BONE_COUNT; ++attempt) {
                if (!entityGo || attempt > 0) {
                    if (attempt) Sleep(50);
                    goMap = SKJH_ReadEntityGoMap();
                    entityGo = 0;
                    go = goMap.find(entity.entityId);
                    if (go != goMap.end()) entityGo = go->second;
                }
                if (!entityGo) continue;
                SKJH_BoneData bones[BONE_COUNT];
                validBones = SKJH_ReadPlayerBones(
                    entityGo, entity.entityPtr, entity.pos, bones);
                if (validBones != BONE_COUNT) Sleep(25);
            }
        }
        if (entity.type == SKJH_PLAYER) {
            ++playerCount;
            if (entityGo)
                ++skeletonEligibleCount;
            else
                ++playerGoMissingCount;
            if (validBones == BONE_COUNT) ++fullSkeletonCount;
        }
        if (entity.type == SKJH_UNKNOWN) ++unknownCount;
        const std::string templateName = SKJH_GetTemplateDisplayName(
            entity.type, entity.templateId, entity.className);
        SKJH_TemplateTable anyTemplateTable = SKJH_TemplateTable::Count;
        const std::string anyTemplateName = SKJH_FindTemplateNameAny(
            entity.templateId, &anyTemplateTable);
        if (entity.templateId) {
            ++templateEntityCount;
            if (!templateName.empty()) ++namedTemplateCount;
            const std::string displayName = templateName.empty()
                ? SKJH_GetEntityClassLabel(entity.className.c_str(), entity.type)
                : templateName;
            if (!displayName.empty()) ++displayTemplateCount;
        }
        output << "    {\"ptr\": \"" << SKJH_Hex(entity.entityPtr)
               << "\", \"id\": " << entity.entityId
               << ", \"klass\": \"" << SKJH_Hex(entity.klass)
               << "\", \"class\": \"" << SKJH_JsonEscape(entity.className)
               << "\", \"type\": \"" << SKJH_JsonEscape(SKJH_GetEntityDisplayName(entity.type))
               << "\", \"classHash\": " << entity.classHash
               << ", \"templateId\": " << entity.templateId
               << ", \"templateName\": \"" << SKJH_JsonEscape(templateName) << "\""
               << ", \"templateAnyName\": \"" << SKJH_JsonEscape(anyTemplateName) << "\""
               << ", \"templateAnyTable\": \""
               << SKJH_GetTemplateTableName(anyTemplateTable) << "\""
               << ", \"spawnType\": " << entity.spawnType
               << ", \"position\": [" << entity.pos.X << ',' << entity.pos.Y << ','
               << entity.pos.Z << "], \"hp\": " << entity.hp
               << ", \"maxHp\": " << entity.maxHp
               << ", \"entityGo\": \"" << SKJH_Hex(entityGo)
               << "\", \"validBones\": " << validBones;
        if (entity.type == SKJH_PLAYER && entityGo && detailedPlayerCount++ < 4) {
            const DWORD64 goKlass = mem.Read<DWORD64>(entityGo);
            output << ", \"goClass\": \"" << SKJH_JsonEscape(SKJH_ReadKlassName(goKlass))
                   << "\", \"goBackReference\": \""
                   << SKJH_Hex(mem.Read<DWORD64>(entityGo + g_RuntimeOffsets.basePlayerGoEntity))
                   << "\", \"boneDebug\": [";
            for (int bone = 0; bone < BONE_COUNT; ++bone) {
                const DWORD64 managed = mem.Read<DWORD64>(
                    entityGo + SKJH_PlayerBoneOffset(bone));
                const DWORD64 native = Mem::IsUserAddress(managed)
                    ? mem.Read<DWORD64>(managed + g_RuntimeOffsets.unityObjectCachedPtr) : 0;
                const DWORD64 data = Mem::IsUserAddress(native)
                    ? mem.Read<DWORD64>(native + g_RuntimeOffsets.unityNativeData) : 0;
                const int32_t transformIndex = Mem::IsUserAddress(native)
                    ? mem.Read<int32_t>(native + g_RuntimeOffsets.unityNativeIndex) : -1;
                const DWORD64 transformArray = Mem::IsUserAddress(data)
                    ? mem.Read<DWORD64>(data + g_RuntimeOffsets.unityDataTransforms) : 0;
                const DWORD64 parentArray = Mem::IsUserAddress(data)
                    ? mem.Read<DWORD64>(data + g_RuntimeOffsets.unityDataParents) : 0;
                const SKJH_UnityTransformResult world = SKJH_ReadUnityTransform(managed);
                output << "{\"bone\": \"" << BoneNames[bone]
                       << "\", \"managed\": \"" << SKJH_Hex(managed)
                       << "\", \"managedClass\": \""
                       << SKJH_JsonEscape(Mem::IsUserAddress(managed)
                            ? SKJH_ReadKlassName(mem.Read<DWORD64>(managed)) : std::string{})
                       << "\", \"native\": \"" << SKJH_Hex(native)
                       << "\", \"data\": \"" << SKJH_Hex(data)
                       << "\", \"index\": " << transformIndex
                       << ", \"transforms\": \"" << SKJH_Hex(transformArray)
                       << "\", \"parents\": \"" << SKJH_Hex(parentArray)
                       << "\", \"worldValid\": " << (world.valid ? "true" : "false")
                       << ", \"world\": [" << world.position.X << ',' << world.position.Y
                       << ',' << world.position.Z << ']';
                output << '}';
                if (bone + 1 != BONE_COUNT) output << ',';
            }
            output << ']';
        }
        output << '}';
        if (index + 1 != sampleCount) output << ',';
        output << '\n';
    }
    const bool dictionaryValid = Mem::IsUserAddress(dictionary) &&
        Mem::IsUserAddress(entries) && dictionaryCount > 0 &&
        entriesCapacity >= dictionaryCount && entriesCapacity <= 40000;
    const bool runtimeSdkValid = SKJH_ValidateRuntimeSdk();
    const bool skeletonsValid = playerCount > 0 &&
        skeletonEligibleCount > 0 &&
        playerGoMissingCount + skeletonEligibleCount == playerCount &&
        skeletonEligibleCount * 5 >= playerCount * 4 &&
        fullSkeletonCount == skeletonEligibleCount;
    const bool allEntitiesSampled = sampleCount == entities.size();
    const bool templatesCovered =
        templateEntityCount == displayTemplateCount;
    const bool catalogRequired = templateEntityCount > 0;
    const bool catalogValid = !catalogRequired ||
        (catalogRefreshed && templateStats.complete);
    const bool localPlayerValid = Mem::IsUserAddress(localPlayer) &&
        SKJH_IsFiniteVector(camera.localPos) &&
        (camera.localPos.X != 0.0f || camera.localPos.Y != 0.0f ||
         camera.localPos.Z != 0.0f);
    output << "  ],\n";
    output << R"(  "skeletonCoverage": {"eligible": )"
           << skeletonEligibleCount
           << R"(, "missingPlayerGo": )" << playerGoMissingCount
           << R"(, "full": )" << fullSkeletonCount
           << R"(, "valid": )" << (skeletonsValid ? "true" : "false")
           << "},\n";
    output << "  \"validation\": {\"runtimeSdk\": "
           << (runtimeSdkValid ? "true" : "false")
           << ", \"dictionary\": " << (dictionaryValid ? "true" : "false")
           << ", \"camera\": " << (camera.valid ? "true" : "false")
           << ", \"catalog\": "
           << (catalogValid ? "true" : "false")
           << ", \"localPlayer\": " << (localPlayerValid ? "true" : "false")
           << ", \"allEntitiesSampled\": "
           << (allEntitiesSampled ? "true" : "false")
           << ", \"templatesCovered\": "
           << (templatesCovered ? "true" : "false")
           << ", \"playerCount\": " << playerCount
           << ", \"fullSkeletonCount\": " << fullSkeletonCount
           << ", \"unknownCount\": " << unknownCount
           << ", \"templateEntityCount\": " << templateEntityCount
           << ", \"namedTemplateCount\": " << namedTemplateCount
           << ", \"displayTemplateCount\": " << displayTemplateCount
           << "}\n}\n";
    output.close();
    return output.good() && gameAssembly && entityManager.valid &&
           runtimeSdkValid && dictionaryValid &&
           !entities.empty() && allEntitiesSampled && camera.valid &&
           localPlayerValid && catalogValid &&
           templatesCovered;
}

inline bool SKJH_RunPlayerIntelProbe(const std::filesystem::path& outputPath,
                                      size_t playerLimit = 64) {
    SKJH_RefreshTemplateCatalog(true);
    const std::vector<SKJH_Entity> entities = SKJH_EnumerateEntities();
    const DWORD64 localPlayer = SKJH_GetLocalPlayer();
    std::vector<const SKJH_Entity*> players;
    for (const auto& entity : entities) {
        if (entity.type == SKJH_PLAYER) players.push_back(&entity);
    }
    std::stable_sort(
        players.begin(), players.end(),
        [localPlayer](const SKJH_Entity* left, const SKJH_Entity* right) {
            return (left->entityPtr == localPlayer) >
                   (right->entityPtr == localPlayer);
        });
    if (players.size() > playerLimit) players.resize(playerLimit);

    std::vector<SKJH_PlayerIntel> playerIntel(players.size());
    std::unordered_set<int64_t> wantedWeapons;
    for (size_t index = 0; index < players.size(); ++index) {
        const auto* entity = players[index];
        SKJH_ReadPlayerWeapon(
            entity->entityPtr, entity->klass, playerIntel[index]);
        if (!playerIntel[index].weaponValid &&
            playerIntel[index].weaponEntityId > 0) {
            wantedWeapons.insert(playerIntel[index].weaponEntityId);
        }
    }
    const auto resolvedWeapons =
        SKJH_FindEntityPointersById(wantedWeapons);
    for (size_t index = 0; index < players.size(); ++index) {
        const auto* entity = players[index];
        auto& intel = playerIntel[index];
        const auto weapon = resolvedWeapons.find(intel.weaponEntityId);
        if (weapon != resolvedWeapons.end()) {
            SKJH_ReadPlayerWeapon(
                entity->entityPtr, entity->klass, intel, weapon->second);
        }
        SKJH_ReadPlayerInventory(
            entity->entityPtr, intel, entity->entityPtr == localPlayer);
    }

    for (int retry = 0; retry < 3; ++retry) {
        std::unordered_set<int64_t> retryIds;
        for (size_t index = 0; index < players.size(); ++index) {
            if (playerIntel[index].weaponValid) continue;
            const auto* entity = players[index];
            SKJH_ReadPlayerWeapon(
                entity->entityPtr, entity->klass, playerIntel[index]);
            if (playerIntel[index].weaponEntityId > 0)
                retryIds.insert(playerIntel[index].weaponEntityId);
        }
        if (retryIds.empty()) break;
        const auto retryResolved =
            SKJH_FindEntityPointersById(retryIds);
        bool allResolved = true;
        for (size_t index = 0; index < players.size(); ++index) {
            auto& intel = playerIntel[index];
            if (intel.weaponValid) continue;
            const auto weapon = retryResolved.find(intel.weaponEntityId);
            if (weapon != retryResolved.end()) {
                const auto* entity = players[index];
                SKJH_ReadPlayerWeapon(
                    entity->entityPtr, entity->klass,
                    intel, weapon->second);
            }
            if (!intel.weaponValid) allResolved = false;
        }
        if (allResolved) break;
        Sleep(25);
    }

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output) return false;

    output << "{\n  \"players\": [\n";
    size_t playerCount = 0;
    size_t weaponCount = 0;
    size_t inventoryCount = 0;
    size_t healthCount = 0;
    bool localSampled = false;
    bool localInventoryValid = false;
    bool localWeaponValid = false;
    for (size_t playerIndex = 0;
         playerIndex < players.size(); ++playerIndex) {
        const auto& entity = *players[playerIndex];
        const auto& intel = playerIntel[playerIndex];
        const bool isLocal =
            entity.entityPtr == localPlayer || intel.inventoryValid;
        const char* availability = "unknown";
        if (intel.inventoryAvailability ==
            SKJH_InventoryAvailability::Available) {
            availability = "available";
        } else if (intel.inventoryAvailability ==
                   SKJH_InventoryAvailability::NotReplicated) {
            availability = "not_replicated";
        } else if (intel.inventoryAvailability ==
                   SKJH_InventoryAvailability::ReadError) {
            availability = "read_error";
        }
        if (playerCount) output << ",\n";
        output << "    {\"id\": " << entity.entityId
               << ", \"ptr\": \"" << SKJH_Hex(entity.entityPtr) << "\""
               << ", \"hp\": " << entity.hp
               << ", \"maxHp\": " << entity.maxHp
               << ", \"weaponEntityId\": " << intel.weaponEntityId
               << ", \"weaponTemplateId\": " << intel.weaponTemplateId
               << ", \"weaponName\": \""
               << SKJH_JsonEscape(intel.weaponName) << "\""
               << ", \"weaponValid\": " << (intel.weaponValid ? "true" : "false")
               << ", \"inventoryRoot\": \"" << SKJH_Hex(intel.inventoryRoot) << "\""
               << ", \"inventoryValid\": "
               << (intel.inventoryValid ? "true" : "false")
               << ", \"nodeCount\": " << intel.inventoryNodeCount
               << ", \"mainContainerId\": " << intel.mainContainerId
               << ", \"mainCapacity\": " << intel.mainContainerCapacity
               << ", \"items\": [";
        for (size_t index = 0; index < intel.inventory.size(); ++index) {
            const auto& item = intel.inventory[index];
            if (index) output << ',';
            output << "{\"templateId\": " << item.templateId
                   << ", \"name\": \"" << SKJH_JsonEscape(item.name)
                   << "\", \"count\": " << item.count
                   << ", \"containerId\": " << item.containerId
                   << ", \"slot\": " << item.slot << '}';
        }
        output << R"(], "isLocal": )" << (isLocal ? "true" : "false")
               << R"(, "weaponObject": ")" << SKJH_Hex(intel.weaponObject)
               << '"'
               << R"(, "weaponClass": ")"
               << SKJH_JsonEscape(intel.weaponClassName) << '"'
               << R"(, "weaponAmount": )" << intel.weaponAmount
               << R"(, "inventoryAvailability": ")" << availability
               << '"' << '}';
        ++playerCount;
        if (intel.weaponValid) ++weaponCount;
        if (intel.inventoryValid) ++inventoryCount;
        if (entity.maxHp > 0.0f && entity.hp >= 0.0f &&
            entity.hp <= entity.maxHp + 1.0f) {
            ++healthCount;
        }
        if (isLocal) {
            localSampled = true;
            localInventoryValid = intel.inventoryValid;
            localWeaponValid = intel.weaponValid;
        }
    }
    output << "\n  ],\n  \"validation\": {\"playerCount\": " << playerCount
           << ", \"weaponCount\": " << weaponCount
           << ", \"inventoryCount\": " << inventoryCount << "}\n}\n";
    output.close();
    const bool weaponCoverage = playerCount > 0 &&
        weaponCount * 100 >= playerCount * 80;
    return output.good() && playerCount > 0 &&
           healthCount == playerCount && weaponCoverage &&
           localSampled && localInventoryValid && localWeaponValid;
}
