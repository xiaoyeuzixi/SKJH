#pragma once
/*
 * SKJH property reader. Property IDs are resolved from each generated
 * entity class' PropNameArray and only fall back to the known schema IDs.
 */
#include "Mem.h"
#include "Offset.h"
#include "GameMatrix.h"
#include <cmath>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace PropId {
    constexpr int32_t PosX  = 0x02;
    constexpr int32_t PosY  = 0x03;
    constexpr int32_t PosZ  = 0x04;
    constexpr int32_t RotX  = 0x05;
    constexpr int32_t RotY  = 0x06;
    constexpr int32_t RotZ  = 0x07;
    constexpr int32_t Hp    = 0xB7;
    constexpr int32_t MaxHp = 0xB8;
}

inline std::shared_mutex g_SKJH_PropertyCacheMutex;
inline std::unordered_map<DWORD64, std::unordered_map<std::string, int32_t>>
    g_SKJH_PropertyIds;

inline void SKJH_AppendUtf8CodePoint(uint32_t codePoint, std::string& out) {
    if (codePoint <= 0x7f) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0u | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    } else if (codePoint <= 0xffff) {
        out.push_back(static_cast<char>(0xe0u | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    } else {
        out.push_back(static_cast<char>(0xf0u | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 12) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3fu)));
    }
}

// IL2CPP strings store a UTF-16 code-unit length at +0x10 and data at +0x14.
// Malformed surrogate sequences are replaced so one bad nickname cannot poison
// the rest of an entity snapshot.
inline bool SKJH_ReadManagedUtf8(DWORD64 stringObject, std::string& out,
                                 size_t maxCodeUnits = 256) {
    out.clear();
    if (!Mem::IsUserAddress(stringObject) || maxCodeUnits == 0 ||
        maxCodeUnits > 4096) {
        return false;
    }
    const int32_t length = mem.Read<int32_t>(stringObject + 0x10);
    if (length <= 0 || static_cast<size_t>(length) > maxCodeUnits) return false;

    std::vector<char16_t> chars(static_cast<size_t>(length));
    if (!mem.Read(stringObject + 0x14, chars.data(),
                  static_cast<DWORD>(chars.size() * sizeof(char16_t)))) {
        return false;
    }

    constexpr uint32_t kReplacementCharacter = 0xfffd;
    out.reserve(chars.size() * 3);
    for (size_t index = 0; index < chars.size(); ++index) {
        const uint32_t first = static_cast<uint16_t>(chars[index]);
        uint32_t codePoint = first;
        if (first >= 0xd800 && first <= 0xdbff) {
            if (index + 1 < chars.size()) {
                const uint32_t second =
                    static_cast<uint16_t>(chars[index + 1]);
                if (second >= 0xdc00 && second <= 0xdfff) {
                    codePoint = 0x10000u + ((first - 0xd800u) << 10) +
                        (second - 0xdc00u);
                    ++index;
                } else {
                    codePoint = kReplacementCharacter;
                }
            } else {
                codePoint = kReplacementCharacter;
            }
        } else if (first >= 0xdc00 && first <= 0xdfff) {
            codePoint = kReplacementCharacter;
        }
        SKJH_AppendUtf8CodePoint(codePoint, out);
    }
    return !out.empty();
}

inline bool SKJH_ReadManagedAscii(DWORD64 stringObject, std::string& out) {
    out.clear();
    if (!Mem::IsUserAddress(stringObject)) return false;
    const int32_t length = mem.Read<int32_t>(stringObject + 0x10);
    if (length <= 0 || length > 256) return false;
    std::vector<char16_t> chars(static_cast<size_t>(length));
    if (!mem.Read(stringObject + 0x14, chars.data(),
                  static_cast<DWORD>(chars.size() * sizeof(char16_t)))) return false;
    out.reserve(chars.size());
    for (char16_t ch : chars) {
        if (ch > 0x7f) return false;
        out.push_back(static_cast<char>(ch));
    }
    return true;
}

inline bool SKJH_TryLoadPropertyIdsAt(
    DWORD64 staticFields, uint32_t namesOffset,
    std::unordered_map<std::string, int32_t>& result) {
    result.clear();
    if (!Mem::IsUserAddress(staticFields)) return false;
    const DWORD64 names = mem.Read<DWORD64>(staticFields + namesOffset);
    if (!Mem::IsUserAddress(names)) return false;
    const int32_t length = mem.Read<int32_t>(names + 0x18);
    if (length <= 0 || length > 4096) return false;

    for (int32_t index = 0; index < length; ++index) {
        const DWORD64 stringObject = mem.Read<DWORD64>(
            names + 0x20 + static_cast<DWORD64>(index) * sizeof(DWORD64));
        std::string propertyName;
        if (SKJH_ReadManagedAscii(stringObject, propertyName) && !propertyName.empty())
            result.emplace(std::move(propertyName), index);
    }
    return !result.empty();
}

inline bool SKJH_LoadPropertyIds(DWORD64 klass,
                                 std::unordered_map<std::string, int32_t>& result) {
    result.clear();
    if (!Mem::IsUserAddress(klass)) return false;
    const DWORD64 staticFields = mem.Read<DWORD64>(
        klass + g_RuntimeOffsets.il2cppClassStaticFields);
    if (!Mem::IsUserAddress(staticFields)) return false;

    // Entity/component classes use StaticClassHash+0, PropNameArray+8.
    // Generated custom types add a logger first and use +8/+10 instead.
    constexpr uint32_t candidateOffsets[] = {0x08, 0x10};
    for (const uint32_t offset : candidateOffsets) {
        std::unordered_map<std::string, int32_t> loaded;
        if (SKJH_TryLoadPropertyIdsAt(staticFields, offset, loaded)) {
            result = std::move(loaded);
            return true;
        }
    }
    return false;
}

inline int32_t SKJH_FindPropertyId(DWORD64 klass, const char* propertyName) {
    if (!klass || !propertyName || !propertyName[0]) return -1;
    {
        std::shared_lock<std::shared_mutex> lock(g_SKJH_PropertyCacheMutex);
        const auto byClass = g_SKJH_PropertyIds.find(klass);
        if (byClass != g_SKJH_PropertyIds.end()) {
            const auto property = byClass->second.find(propertyName);
            return property == byClass->second.end() ? -1 : property->second;
        }
    }

    std::unordered_map<std::string, int32_t> loaded;
    if (!SKJH_LoadPropertyIds(klass, loaded)) return -1;
    const auto property = loaded.find(propertyName);
    const int32_t result = property == loaded.end() ? -1 : property->second;
    {
        std::lock_guard<std::shared_mutex> lock(g_SKJH_PropertyCacheMutex);
        g_SKJH_PropertyIds.emplace(klass, std::move(loaded));
    }
    return result;
}

inline int32_t SKJH_PropertyIdOr(DWORD64 klass, const char* name, int32_t fallback) {
    const int32_t resolved = SKJH_FindPropertyId(klass, name);
    return resolved >= 0 ? resolved : fallback;
}

inline int32_t SKJH_GetMappedIndex(DWORD64 entity, int32_t propId) {
    if (!Mem::IsUserAddress(entity)) return -1;
    const DWORD64 dataSet = mem.Read<DWORD64>(entity + g_RuntimeOffsets.typeBaseDataSet);
    if (!Mem::IsUserAddress(dataSet)) return -1;
    const DWORD64 p2i = mem.Read<DWORD64>(dataSet + g_RuntimeOffsets.dataSetPropertyMap);
    if (!Mem::IsUserAddress(p2i)) return -1;
    const int32_t arrLen = mem.Read<int32_t>(p2i + 0x18);
    if (propId < 0 || propId >= arrLen || arrLen > 16384) return -1;
    const int32_t mappedIndex = mem.Read<int32_t>(
        p2i + 0x20 + static_cast<DWORD64>(propId) * sizeof(int32_t));
    return (mappedIndex >= 0 && mappedIndex < 16384) ? mappedIndex : -1;
}

template<typename T>
inline bool SKJH_GetPropertyValue(DWORD64 entity, int32_t propId, T& result) {
    result = {};
    const int32_t mappedIndex = SKJH_GetMappedIndex(entity, propId);
    if (mappedIndex < 0) return false;
    const DWORD64 dataSet = mem.Read<DWORD64>(entity + g_RuntimeOffsets.typeBaseDataSet);
    if (!Mem::IsUserAddress(dataSet)) return false;
    const DWORD64 propArray = mem.Read<DWORD64>(dataSet + g_RuntimeOffsets.dataSetValProps);
    if (!Mem::IsUserAddress(propArray)) return false;
    const DWORD64 values = mem.Read<DWORD64>(propArray + g_RuntimeOffsets.propArrayArray);
    if (!Mem::IsUserAddress(values)) return false;
    const int32_t valueCount = mem.Read<int32_t>(values + 0x18);
    if (mappedIndex >= valueCount || valueCount <= 0 || valueCount > 16384) return false;
    return mem.Read(values + 0x20 + static_cast<DWORD64>(mappedIndex) * 0x10,
                    &result, static_cast<DWORD>(sizeof(T)));
}

inline bool SKJH_GetReferencePropertyValue(DWORD64 object, int32_t propId,
                                            DWORD64& result) {
    result = 0;
    const int32_t rawMappedIndex = SKJH_GetMappedIndex(object, propId);
    if (rawMappedIndex < 0) return false;
    // ArrayDataSet encodes refProps as 10000 + the RefTypeValue index.
    // Accept a direct index as a compatibility fallback for older layouts.
    constexpr int32_t kReferenceIndexBase = 10000;
    const int32_t mappedIndex = rawMappedIndex >= kReferenceIndexBase
        ? rawMappedIndex - kReferenceIndexBase : rawMappedIndex;
    const DWORD64 dataSet = mem.Read<DWORD64>(
        object + g_RuntimeOffsets.typeBaseDataSet);
    if (!Mem::IsUserAddress(dataSet)) return false;
    const DWORD64 propArray = mem.Read<DWORD64>(
        dataSet + g_RuntimeOffsets.dataSetRefProps);
    if (!Mem::IsUserAddress(propArray)) return false;
    const DWORD64 values = mem.Read<DWORD64>(
        propArray + g_RuntimeOffsets.propArrayArray);
    if (!Mem::IsUserAddress(values)) return false;
    const int32_t valueCount = mem.Read<int32_t>(values + 0x18);
    if (mappedIndex >= valueCount || valueCount <= 0 || valueCount > 16384)
        return false;
    return mem.Read(values + 0x20 + static_cast<DWORD64>(mappedIndex) * 0x10,
                    &result, sizeof(result)) &&
        (!result || Mem::IsUserAddress(result));
}

template<typename T>
inline bool SKJH_GetPropertyValueByName(DWORD64 object, DWORD64 klass,
                                        const char* name, T& result) {
    const int32_t property = SKJH_FindPropertyId(klass, name);
    return property >= 0 && SKJH_GetPropertyValue(object, property, result);
}

inline bool SKJH_GetReferencePropertyByName(DWORD64 object, DWORD64 klass,
                                             const char* name, DWORD64& result) {
    const int32_t property = SKJH_FindPropertyId(klass, name);
    return property >= 0 &&
        SKJH_GetReferencePropertyValue(object, property, result);
}

inline float SKJH_GetFloat(DWORD64 entity, int32_t propId) {
    float result = 0.0f;
    SKJH_GetPropertyValue(entity, propId, result);
    return result;
}

inline bool SKJH_HasPropertySystem(DWORD64 entity) {
    if (!Mem::IsUserAddress(entity)) return false;
    const DWORD64 dataSet = mem.Read<DWORD64>(entity + g_RuntimeOffsets.typeBaseDataSet);
    if (!Mem::IsUserAddress(dataSet)) return false;
    return Mem::IsUserAddress(mem.Read<DWORD64>(dataSet + g_RuntimeOffsets.dataSetPropertyMap)) &&
           Mem::IsUserAddress(mem.Read<DWORD64>(dataSet + g_RuntimeOffsets.dataSetValProps));
}

inline FVector SKJH_GetPosition(DWORD64 entity, DWORD64 klass = 0) {
    FVector position = {0, 0, 0};
    if (!entity) return position;
    if (!klass) klass = mem.Read<DWORD64>(entity);

    const int32_t posX = SKJH_PropertyIdOr(klass, "PosX", PropId::PosX);
    const int32_t posY = SKJH_PropertyIdOr(klass, "PosY", PropId::PosY);
    const int32_t posZ = SKJH_PropertyIdOr(klass, "PosZ", PropId::PosZ);
    const bool readX = SKJH_GetPropertyValue(entity, posX, position.X);
    const bool readY = SKJH_GetPropertyValue(entity, posY, position.Y);
    const bool readZ = SKJH_GetPropertyValue(entity, posZ, position.Z);
    if (!readX || !readY || !readZ ||
        !std::isfinite(position.X) || !std::isfinite(position.Y) ||
        !std::isfinite(position.Z) || std::fabs(position.X) > 1.0e7f ||
        std::fabs(position.Y) > 1.0e7f || std::fabs(position.Z) > 1.0e7f) {
        return {0, 0, 0};
    }
    return position;
}

inline float SKJH_GetHp(DWORD64 entity, DWORD64 klass = 0) {
    if (!klass) klass = mem.Read<DWORD64>(entity);
    return SKJH_GetFloat(entity, SKJH_PropertyIdOr(klass, "Hp", PropId::Hp));
}

inline float SKJH_GetMaxHp(DWORD64 entity, DWORD64 klass = 0) {
    if (!klass) klass = mem.Read<DWORD64>(entity);
    return SKJH_GetFloat(entity, SKJH_PropertyIdOr(klass, "MaxHp", PropId::MaxHp));
}

inline int64_t SKJH_GetTemplateId(DWORD64 entity, DWORD64 klass) {
    int64_t value = 0;
    const int32_t property = SKJH_FindPropertyId(klass, "TemplateId");
    if (property >= 0) SKJH_GetPropertyValue(entity, property, value);
    return value;
}

inline int64_t SKJH_GetTableId(DWORD64 entity, DWORD64 klass) {
    int64_t value = 0;
    const int32_t property = SKJH_FindPropertyId(klass, "TableId");
    if (property >= 0) SKJH_GetPropertyValue(entity, property, value);
    return value;
}

inline bool SKJH_GetBoolProperty(DWORD64 entity, DWORD64 klass,
                                 const char* propertyName, bool& value) {
    value = false;
    const int32_t property = SKJH_FindPropertyId(klass, propertyName);
    if (property < 0) return false;
    uint8_t raw = 0;
    if (!SKJH_GetPropertyValue(entity, property, raw)) return false;
    value = raw != 0;
    return true;
}

inline int32_t SKJH_GetSpawnType(DWORD64 entity, DWORD64 klass) {
    int32_t value = 0;
    const int32_t property = SKJH_FindPropertyId(klass, "SpawnType");
    if (property >= 0) SKJH_GetPropertyValue(entity, property, value);
    return value;
}

inline int32_t SKJH_GetClassHash(DWORD64 entity) {
    const DWORD64 dataSet = mem.Read<DWORD64>(entity + g_RuntimeOffsets.typeBaseDataSet);
    return Mem::IsUserAddress(dataSet) ? mem.Read<int32_t>(dataSet + 0x14) : 0;
}
