#pragma once
/* Entity dictionary traversal and inheritance-aware IL2CPP classification. */
#include "Mem.h"
#include "Offset.h"
#include "GameMatrix.h"
#include "SKJH_Property.h"
#include "ItemMap.h"
#include <atomic>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct SKJH_Entity {
    DWORD64 entityPtr = 0;
    DWORD64 klass     = 0;
    int64_t entityId  = 0;
    int     type      = SKJH_UNKNOWN;
    int32_t classHash = 0;
    int64_t templateId = 0;
    int32_t spawnType = 0;
    bool    isBot = false;
    bool    botKnown = false;
    std::string className;
    FVector pos       = {0, 0, 0};
    float   hp        = 0;
    float   maxHp     = 0;
    float   distance  = 0;
    bool    valid     = false;
};

struct SKJH_KlassInfo {
    int type = SKJH_UNKNOWN;
    std::string name;
    std::string nameSpace;
};

inline std::shared_mutex g_SKJH_KlassMutex;
inline std::unordered_map<DWORD64, SKJH_KlassInfo> g_SKJH_KlassMap;
inline std::atomic<DWORD64> g_SKJH_PlayerKlass{0};

inline std::string SKJH_ReadNativeString(DWORD64 address, size_t maxLength = 127) {
    if (!Mem::IsUserAddress(address) || maxLength == 0) return {};
    std::vector<char> buffer(maxLength + 1, 0);
    const size_t firstRead = (std::min)(maxLength, static_cast<size_t>(64));
    // Use ReadMeta (VMM-cached) for metadata strings — class names never
    // change, so caching is safe and dramatically reduces DMA bus pressure.
    if (mem.ReadMeta(address, buffer.data(), static_cast<DWORD>(firstRead))) {
        size_t length = 0;
        while (length < firstRead && buffer[length]) ++length;
        if (length && length < firstRead) return std::string(buffer.data(), length);
    }
    // Fallback 1: retry with uncached read (in case VMM cache is stale)
    if (mem.Read(address, buffer.data(), static_cast<DWORD>(firstRead))) {
        size_t length = 0;
        while (length < firstRead && buffer[length]) ++length;
        if (length && length < firstRead) return std::string(buffer.data(), length);
    }
    // Fallback 2: byte-by-byte reads (handles page-boundary edge cases)
    for (size_t index = 0; index < maxLength; ++index) {
        char ch = 0;
        if (!mem.ReadMeta(address + index, &ch, 1)) {
            if (!mem.Read(address + index, &ch, 1)) return {};
        }
        if (!ch) return index ? std::string(buffer.data(), index) : std::string{};
        buffer[index] = ch;
    }
    return {};
}

inline std::string SKJH_ReadKlassName(DWORD64 klass) {
    if (!Mem::IsUserAddress(klass)) return {};
    // Retry up to 3 times — DMA reads of IL2CPP metadata can fail
    // intermittently due to bus contention or page-out.
    for (int attempt = 0; attempt < 3; ++attempt) {
        const DWORD64 namePtr = mem.ReadMeta<DWORD64>(
            klass + g_RuntimeOffsets.il2cppClassName);
        if (Mem::IsUserAddress(namePtr)) {
            std::string name = SKJH_ReadNativeString(namePtr);
            if (!name.empty()) return name;
        }
        // Fallback: uncached read
        const DWORD64 namePtr2 = mem.Read<DWORD64>(
            klass + g_RuntimeOffsets.il2cppClassName);
        if (Mem::IsUserAddress(namePtr2)) {
            std::string name = SKJH_ReadNativeString(namePtr2);
            if (!name.empty()) return name;
        }
    }
    return {};
}

inline std::string SKJH_ReadKlassNamespace(DWORD64 klass) {
    if (!Mem::IsUserAddress(klass)) return {};
    const DWORD64 nsPtr = mem.ReadMeta<DWORD64>(
        klass + g_RuntimeOffsets.il2cppClassNamespace);
    if (!Mem::IsUserAddress(nsPtr)) return {};
    return SKJH_ReadNativeString(nsPtr);
}

inline SKJH_KlassInfo SKJH_ResolveKlassInfo(DWORD64 klass) {
    SKJH_KlassInfo info;
    if (!Mem::IsUserAddress(klass)) return info;
    info.name = SKJH_ReadKlassName(klass);
    info.nameSpace = SKJH_ReadKlassNamespace(klass);

    DWORD64 current = klass;
    for (int depth = 0; depth < 10 && Mem::IsUserAddress(current); ++depth) {
        const std::string currentName = depth == 0 ? info.name : SKJH_ReadKlassName(current);
        const int classified = SKJH_ClassNameToType(currentName.c_str());
        if (classified != SKJH_UNKNOWN) {
            info.type = classified;
            break;
        }
        // Use ReadMeta for parent pointer — IL2CPP class hierarchy is immutable
        DWORD64 parent = mem.ReadMeta<DWORD64>(
            current + g_RuntimeOffsets.il2cppClassParent);
        if (!Mem::IsUserAddress(parent))
            parent = mem.Read<DWORD64>(
                current + g_RuntimeOffsets.il2cppClassParent);
        if (parent == current) break;
        current = parent;
    }
    return info;
}

inline SKJH_KlassInfo SKJH_GetKlassInfo(DWORD64 klass) {
    if (!klass) return {};
    // Fast path: if playerKlass is known, return PLAYER immediately.
    // This avoids unreliable DMA class-name reads for the most common case.
    const DWORD64 playerKlass = g_SKJH_PlayerKlass.load();
    if (playerKlass && klass == playerKlass) {
        std::shared_lock<std::shared_mutex> lock(g_SKJH_KlassMutex);
        const auto cached = g_SKJH_KlassMap.find(klass);
        if (cached != g_SKJH_KlassMap.end()) return cached->second;
        SKJH_KlassInfo info;
        info.type = SKJH_PLAYER;
        info.name = "PlayerEntity";
        return info;
    }
    // Check cache for other known classes
    {
        std::shared_lock<std::shared_mutex> lock(g_SKJH_KlassMutex);
        const auto cached = g_SKJH_KlassMap.find(klass);
        if (cached != g_SKJH_KlassMap.end()) return cached->second;
    }
    SKJH_KlassInfo resolved = SKJH_ResolveKlassInfo(klass);
    // Do not freeze a transient parent/name short read as UNKNOWN forever.
    if (resolved.type != SKJH_UNKNOWN) {
        std::lock_guard<std::shared_mutex> lock(g_SKJH_KlassMutex);
        g_SKJH_KlassMap.emplace(klass, resolved);
    }
    if (resolved.type == SKJH_PLAYER && !playerKlass)
        g_SKJH_PlayerKlass.store(klass);
    return resolved;
}

inline int SKJH_GetEntityType(DWORD64 klass) {
    if (klass == g_SKJH_PlayerKlass.load() && klass) return SKJH_PLAYER;
    return SKJH_GetKlassInfo(klass).type;
}

inline std::string SKJH_GetEntityClassName(DWORD64 klass) {
    return SKJH_GetKlassInfo(klass).name;
}

inline size_t SKJH_GetKlassCacheSize() {
    std::shared_lock<std::shared_mutex> lock(g_SKJH_KlassMutex);
    return g_SKJH_KlassMap.size();
}

inline DWORD64 SKJH_GetMcStaticFields() {
    const DWORD64 gameAssembly = mem.GetBase("GameAssembly.dll");
    if (!gameAssembly) return 0;
    DWORD64 klass = mem.ReadMeta<DWORD64>(
        gameAssembly + g_RuntimeOffsets.mcTypeInfo);
    if (!Mem::IsUserAddress(klass))
        klass = mem.Read<DWORD64>(
            gameAssembly + g_RuntimeOffsets.mcTypeInfo);
    if (!Mem::IsUserAddress(klass)) return 0;
    DWORD64 fields = mem.ReadMeta<DWORD64>(
        klass + g_RuntimeOffsets.il2cppClassStaticFields);
    if (!Mem::IsUserAddress(fields))
        fields = mem.Read<DWORD64>(
            klass + g_RuntimeOffsets.il2cppClassStaticFields);
    return Mem::IsUserAddress(fields) ? fields : 0;
}

inline DWORD64 SKJH_GetEntitiesDictionary() {
    const DWORD64 gameAssembly = mem.GetBase("GameAssembly.dll");
    if (!gameAssembly) return 0;
    DWORD64 klass = mem.ReadMeta<DWORD64>(
        gameAssembly + g_RuntimeOffsets.entityManagerTypeInfo);
    if (!Mem::IsUserAddress(klass))
        klass = mem.Read<DWORD64>(
            gameAssembly + g_RuntimeOffsets.entityManagerTypeInfo);
    if (!Mem::IsUserAddress(klass)) return 0;
    DWORD64 fields = mem.ReadMeta<DWORD64>(
        klass + g_RuntimeOffsets.il2cppClassStaticFields);
    if (!Mem::IsUserAddress(fields))
        fields = mem.Read<DWORD64>(
            klass + g_RuntimeOffsets.il2cppClassStaticFields);
    if (!Mem::IsUserAddress(fields)) return 0;
    DWORD64 instance = mem.ReadMeta<DWORD64>(
        fields + g_RuntimeOffsets.entityManagerInstance);
    if (!Mem::IsUserAddress(instance))
        instance = mem.Read<DWORD64>(
            fields + g_RuntimeOffsets.entityManagerInstance);
    if (!Mem::IsUserAddress(instance)) return 0;
    DWORD64 dictionary = mem.ReadMeta<DWORD64>(
        instance + g_RuntimeOffsets.entityManagerEntities);
    if (!Mem::IsUserAddress(dictionary))
        dictionary = mem.Read<DWORD64>(
            instance + g_RuntimeOffsets.entityManagerEntities);
    return Mem::IsUserAddress(dictionary) ? dictionary : 0;
}

inline DWORD64 SKJH_GetLocalPlayer() {
    const DWORD64 fields = SKJH_GetMcStaticFields();
    if (!fields) return 0;
    const DWORD64 manager = mem.Read<DWORD64>(fields + g_RuntimeOffsets.mcMyPlayer);
    if (!Mem::IsUserAddress(manager)) return 0;
    const DWORD64 player = mem.Read<DWORD64>(manager + g_RuntimeOffsets.mgrMyPlayerLocal);
    return Mem::IsUserAddress(player) ? player : 0;
}

inline SKJH_Entity SKJH_ReadEntity(DWORD64 entityPtr) {
    SKJH_Entity entity;
    entity.entityPtr = entityPtr;
    if (!Mem::IsUserAddress(entityPtr)) return entity;

    entity.klass = mem.Read<DWORD64>(entityPtr);
    // SKJH_GetKlassInfo now checks g_SKJH_PlayerKlass first (fast path),
    // avoiding unreliable DMA class-name reads for known player entities.
    const SKJH_KlassInfo classInfo = SKJH_GetKlassInfo(entity.klass);
    entity.type = classInfo.type;
    entity.className = classInfo.name;
    if (entity.className == "ThrownEntity") {
        bool canPickUp = false;
        if (SKJH_GetBoolProperty(entityPtr, entity.klass, "CanPickUp", canPickUp) &&
            !canPickUp) entity.type = SKJH_SYSTEM;
    }
    entity.entityId = mem.Read<int64_t>(entityPtr + g_RuntimeOffsets.entityEntityId);
    entity.classHash = SKJH_GetClassHash(entityPtr);
    if (entity.type == SKJH_PLAYER) {
        entity.botKnown = SKJH_GetBoolProperty(
            entityPtr, entity.klass, "IsRobot", entity.isBot);
    }
    entity.pos = SKJH_GetPosition(entityPtr, entity.klass);

    if (SKJH_HasHealth(entity.type)) {
        entity.hp = SKJH_GetHp(entityPtr, entity.klass);
        entity.maxHp = SKJH_GetMaxHp(entityPtr, entity.klass);
    }
    if (SKJH_IsLootType(entity.type) || entity.type == SKJH_PART ||
        entity.type == SKJH_TREE || entity.type == SKJH_VEHICLE ||
        entity.type == SKJH_MONSTER || entity.type == SKJH_NPC) {
        entity.templateId = SKJH_GetTemplateId(entityPtr, entity.klass);
        if (entity.className.find("ThrownEntity") != std::string::npos) {
            const int64_t tableId = SKJH_GetTableId(entityPtr, entity.klass);
            if (tableId) entity.templateId = tableId;
        }
        entity.spawnType = SKJH_GetSpawnType(entityPtr, entity.klass);
    }

    entity.valid = std::isfinite(entity.pos.X) && std::isfinite(entity.pos.Y) &&
        std::isfinite(entity.pos.Z) &&
        (entity.pos.X != 0.0f || entity.pos.Y != 0.0f || entity.pos.Z != 0.0f);
    return entity;
}

inline std::vector<SKJH_Entity> SKJH_EnumerateEntities(size_t maximum = 20000) {
    std::vector<SKJH_Entity> result;
    const DWORD64 dictionary = SKJH_GetEntitiesDictionary();
    if (!dictionary) return result;
    const DWORD64 entries = mem.Read<DWORD64>(dictionary + 0x18);
    const int32_t count = mem.Read<int32_t>(dictionary + 0x20);
    if (!Mem::IsUserAddress(entries) || count <= 0 ||
        count > static_cast<int32_t>(maximum)) return result;
    const int32_t capacity = mem.Read<int32_t>(entries + 0x18);
    if (capacity <= 0 || capacity > static_cast<int32_t>(maximum * 2)) return result;
    const int32_t scanCount = count < capacity ? count : capacity;
    result.reserve(scanCount);
    for (int32_t index = 0; index < scanCount; ++index) {
        const DWORD64 entry = entries + 0x20 + static_cast<DWORD64>(index) * 0x18;
        if (mem.Read<int32_t>(entry) < 0) continue;
        const DWORD64 pointer = mem.Read<DWORD64>(entry + 0x10);
        SKJH_Entity entity = SKJH_ReadEntity(pointer);
        if (entity.valid) result.push_back(std::move(entity));
    }
    return result;
}
