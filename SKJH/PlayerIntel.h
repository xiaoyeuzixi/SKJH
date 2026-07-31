#pragma once

#include "SKJH_Entity.h"
#include "TemplateCatalog.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class SKJH_InventoryAvailability : uint8_t {
    Unknown = 0,
    Available,
    NotReplicated,
    ReadError,
};

struct SKJH_PlayerInventoryItem {
    int64_t templateId = 0;
    int32_t count = 1;
    int32_t containerId = 0;
    int64_t slot = 0;
    std::string name;
};

struct SKJH_PlayerIntel {
    std::string playerName;
    uint64_t roleId = 0;
    bool playerNameValid = false;

    int64_t weaponEntityId = 0;
    int64_t weaponTemplateId = 0;
    DWORD64 weaponObject = 0;
    int32_t weaponAmount = 0;
    std::string weaponClassName;
    std::string weaponName;
    bool weaponValid = false;

    std::vector<SKJH_PlayerInventoryItem> inventory;
    DWORD64 inventoryRoot = 0;
    int32_t mainContainerId = 0;
    int32_t mainContainerCapacity = 0;
    size_t inventoryNodeCount = 0;
    bool inventoryValid = false;
    bool inventoryIsLocal = false;
    SKJH_InventoryAvailability inventoryAvailability =
        SKJH_InventoryAvailability::Unknown;
};

inline bool SKJH_IntelKlassDerivesFrom(DWORD64 klass, const char* expected) {
    for (int depth = 0; depth < 16 && Mem::IsUserAddress(klass); ++depth) {
        if (SKJH_ReadKlassName(klass) == expected) return true;
        const DWORD64 parent = mem.Read<DWORD64>(
            klass + g_RuntimeOffsets.il2cppClassParent);
        if (!Mem::IsUserAddress(parent) || parent == klass) break;
        klass = parent;
    }
    return false;
}

inline bool SKJH_ReadManagedDictionaryPointerValues(
    DWORD64 dictionary, std::vector<DWORD64>& values, size_t limit = 4096) {
    values.clear();
    if (!Mem::IsUserAddress(dictionary) || !limit) return false;
    const DWORD64 entries = mem.Read<DWORD64>(dictionary + 0x18);
    const int32_t count = mem.Read<int32_t>(dictionary + 0x20);
    if (count < 0 || count > static_cast<int32_t>(limit)) return false;
    if (!count) return true;
    if (!Mem::IsUserAddress(entries)) return false;
    const int32_t capacity = mem.Read<int32_t>(entries + 0x18);
    if (capacity < count || capacity > static_cast<int32_t>(limit * 2))
        return false;

    values.reserve(static_cast<size_t>(count));
    for (int32_t index = 0; index < count; ++index) {
        const DWORD64 entry = entries + 0x20 +
            static_cast<DWORD64>(index) * 0x18;
        if (mem.Read<int32_t>(entry) < 0) continue;
        const DWORD64 value = mem.Read<DWORD64>(entry + 0x10);
        if (Mem::IsUserAddress(value)) values.push_back(value);
    }
    return true;
}

inline DWORD64 SKJH_FindDictionaryObjectByClass(
    DWORD64 dictionary, const char* className, size_t limit = 512) {
    std::vector<DWORD64> values;
    if (!SKJH_ReadManagedDictionaryPointerValues(dictionary, values, limit))
        return 0;
    for (const DWORD64 value : values) {
        const DWORD64 klass = mem.Read<DWORD64>(value);
        if (SKJH_IntelKlassDerivesFrom(klass, className)) return value;
    }
    return 0;
}

inline DWORD64 SKJH_GetMgrEntity() {
    const DWORD64 fields = SKJH_GetMcStaticFields();
    if (!Mem::IsUserAddress(fields)) return 0;
    const DWORD64 manager = mem.Read<DWORD64>(
        fields + g_RuntimeOffsets.mcEntity);
    return Mem::IsUserAddress(manager) ? manager : 0;
}

inline void SKJH_ScanEntitySetArrayForIds(
    DWORD64 entitySetArray, std::unordered_set<int64_t>& remaining,
    std::unordered_map<int64_t, DWORD64>& resolved) {
    if (!Mem::IsUserAddress(entitySetArray) || remaining.empty()) return;
    const int32_t setCount = mem.Read<int32_t>(entitySetArray + 0x18);
    if (setCount <= 0 || setCount > 4096) return;

    std::unordered_set<DWORD64> visitedDictionaries;
    for (int32_t setIndex = 0; setIndex < setCount && !remaining.empty(); ++setIndex) {
        const DWORD64 entitySet = mem.Read<DWORD64>(
            entitySetArray + 0x20 + static_cast<DWORD64>(setIndex) * 8);
        if (!Mem::IsUserAddress(entitySet)) continue;
        const DWORD64 dictionary = mem.Read<DWORD64>(
            entitySet + g_RuntimeOffsets.entitySetGenericEntities);
        if (!Mem::IsUserAddress(dictionary) ||
            !visitedDictionaries.emplace(dictionary).second) {
            continue;
        }
        const DWORD64 entries = mem.Read<DWORD64>(dictionary + 0x18);
        const int32_t count = mem.Read<int32_t>(dictionary + 0x20);
        if (!Mem::IsUserAddress(entries) || count <= 0 || count > 200000)
            continue;
        const int32_t capacity = mem.Read<int32_t>(entries + 0x18);
        if (capacity <= 0 || capacity > 400000) continue;
        const int32_t scanCount = (std::min)(count, capacity);
        for (int32_t entryIndex = 0;
             entryIndex < scanCount && !remaining.empty(); ++entryIndex) {
            const DWORD64 entry = entries + 0x20 +
                static_cast<DWORD64>(entryIndex) * 0x18;
            if (mem.Read<int32_t>(entry) < 0) continue;
            const int64_t key = mem.Read<int64_t>(entry + 0x08);
            if (remaining.find(key) == remaining.end()) continue;
            const DWORD64 value = mem.Read<DWORD64>(entry + 0x10);
            if (!Mem::IsUserAddress(value)) continue;
            resolved[key] = value;
            remaining.erase(key);
        }
    }
}

inline std::unordered_map<int64_t, DWORD64> SKJH_FindEntityPointersById(
    const std::unordered_set<int64_t>& entityIds) {
    std::unordered_map<int64_t, DWORD64> resolved;
    std::unordered_set<int64_t> remaining;
    for (const int64_t entityId : entityIds) {
        if (entityId > 0) remaining.insert(entityId);
    }
    if (remaining.empty()) return resolved;

    const DWORD64 manager = SKJH_GetMgrEntity();
    if (!Mem::IsUserAddress(manager)) return resolved;
    const DWORD64 collection = mem.Read<DWORD64>(
        manager + g_RuntimeOffsets.mgrEntitySet);
    if (!Mem::IsUserAddress(collection)) return resolved;

    // InterfaceCollection has far fewer sets than EntityCollection.
    const DWORD64 interfaceSets = mem.Read<DWORD64>(
        collection + g_RuntimeOffsets.entitySetInterfaceCollection);
    SKJH_ScanEntitySetArrayForIds(interfaceSets, remaining, resolved);
    if (!remaining.empty()) {
        const DWORD64 typeSets = mem.Read<DWORD64>(
            collection + g_RuntimeOffsets.entitySetCollection);
        SKJH_ScanEntitySetArrayForIds(typeSets, remaining, resolved);
    }
    return resolved;
}

inline DWORD64 SKJH_FindPlayerInventoryRoot(DWORD64 player) {
    if (!Mem::IsUserAddress(player)) return 0;
    const DWORD64 components = mem.Read<DWORD64>(
        player + g_RuntimeOffsets.entityComponents);
    if (!Mem::IsUserAddress(components)) return 0;
    const DWORD64 rootComponent = SKJH_FindDictionaryObjectByClass(
        components, "RootNodeComponent");
    if (!Mem::IsUserAddress(rootComponent)) return 0;
    const DWORD64 systemRoots = mem.Read<DWORD64>(
        rootComponent + g_RuntimeOffsets.rootSystemRoots);
    if (!Mem::IsUserAddress(systemRoots)) return 0;
    return SKJH_FindDictionaryObjectByClass(
        systemRoots, "PlayerInventoryRootNode");
}

inline int64_t SKJH_SelectItemTemplateId(
    int64_t bizId, int64_t index, std::string* resolvedName = nullptr) {
    const std::array<int64_t, 6> candidates = {
        bizId, index,
        static_cast<int64_t>(static_cast<uint32_t>(bizId)),
        static_cast<int64_t>(static_cast<uint64_t>(bizId) >> 32),
        static_cast<int64_t>(static_cast<uint32_t>(index)),
        static_cast<int64_t>(static_cast<uint64_t>(index) >> 32),
    };
    for (const int64_t candidate : candidates) {
        if (candidate <= 0) continue;
        std::string name = SKJH_LookupTemplateName(
            SKJH_TemplateTable::Item, candidate);
        if (name.empty()) continue;
        if (resolvedName) *resolvedName = std::move(name);
        return candidate;
    }
    return 0;
}

inline bool SKJH_ReadPlayerIdentity(DWORD64 player, DWORD64 playerKlass,
                                    SKJH_PlayerIntel& intel) {
    if (!Mem::IsUserAddress(player) || !Mem::IsUserAddress(playerKlass))
        return false;

    bool readAny = false;
    DWORD64 nameObject = 0;
    std::string playerName;
    if (SKJH_GetReferencePropertyByName(
            player, playerKlass, "Name", nameObject) &&
        SKJH_ReadManagedUtf8(nameObject, playerName, 128)) {
        intel.playerName = std::move(playerName);
        intel.playerNameValid = true;
        readAny = true;
    }

    int64_t longRoleId = 0;
    if (SKJH_GetPropertyValueByName(
            player, playerKlass, "LongRoleId", longRoleId) &&
        longRoleId > 0) {
        intel.roleId = static_cast<uint64_t>(longRoleId);
        readAny = true;
    } else {
        uint64_t roleId = 0;
        if (SKJH_GetPropertyValueByName(
                player, playerKlass, "RoleId", roleId) &&
            roleId != 0) {
            intel.roleId = roleId;
            readAny = true;
        }
    }
    return readAny;
}

inline bool SKJH_ReadResolvedWeaponObject(DWORD64 weapon,
                                          int64_t expectedEntityId,
                                          SKJH_PlayerIntel& intel) {
    if (!Mem::IsUserAddress(weapon)) return false;
    const DWORD64 klass = mem.Read<DWORD64>(weapon);
    if (!Mem::IsUserAddress(klass) ||
        !SKJH_IntelKlassDerivesFrom(klass, "CustomBaseItem")) {
        return false;
    }
    int64_t actualEntityId = 0;
    SKJH_GetPropertyValueByName(
        weapon, klass, "EntityId", actualEntityId);
    if (expectedEntityId > 0 && actualEntityId > 0 &&
        actualEntityId != expectedEntityId) {
        return false;
    }
    int64_t tableId = 0;
    SKJH_GetPropertyValueByName(weapon, klass, "TableId", tableId);
    int32_t amount = 0;
    SKJH_GetPropertyValueByName(weapon, klass, "Amount", amount);

    intel.weaponObject = weapon;
    intel.weaponClassName = SKJH_ReadKlassName(klass);
    intel.weaponAmount = amount;
    intel.weaponTemplateId = tableId;
    if (tableId > 0) {
        intel.weaponName = SKJH_LookupTemplateName(
            SKJH_TemplateTable::Item, tableId);
        if (intel.weaponName.empty()) {
            intel.weaponName = intel.weaponClassName + " #" +
                std::to_string(tableId);
        }
    } else if (SKJH_IntelKlassDerivesFrom(klass, "MeleeCustom")) {
        intel.weaponName = u8"徒手/近战";
    } else {
        intel.weaponName = intel.weaponClassName;
    }
    intel.weaponValid = !intel.weaponName.empty();
    return intel.weaponValid;
}

inline bool SKJH_ReadPlayerWeapon(DWORD64 player, DWORD64 playerKlass,
                                   SKJH_PlayerIntel& intel,
                                   DWORD64 resolvedWeapon = 0) {
    intel.weaponEntityId = 0;
    intel.weaponTemplateId = 0;
    intel.weaponObject = 0;
    intel.weaponAmount = 0;
    intel.weaponClassName.clear();
    intel.weaponName.clear();
    intel.weaponValid = false;
    if (!Mem::IsUserAddress(player) || !Mem::IsUserAddress(playerKlass))
        return false;

    const bool weaponIdRead = SKJH_GetPropertyValueByName(
        player, playerKlass, "CurrentWeaponId", intel.weaponEntityId);
    if (intel.weaponEntityId > 0 &&
        SKJH_ReadResolvedWeaponObject(
            resolvedWeapon, intel.weaponEntityId, intel)) {
        return true;
    }

    // Cheap fallback for versions that replicate the animation table ID.
    int64_t animatedTemplate = 0;
    const bool animatedTemplateRead = mem.Read(
        player + g_RuntimeOffsets.playerAnimHeldItemTableId,
        &animatedTemplate, sizeof(animatedTemplate));
    if (animatedTemplate > 0) {
        intel.weaponTemplateId = animatedTemplate;
        intel.weaponName = SKJH_LookupTemplateName(
            SKJH_TemplateTable::Item, animatedTemplate);
        intel.weaponValid = !intel.weaponName.empty();
    }

    if (weaponIdRead && animatedTemplateRead &&
        !intel.weaponEntityId && !animatedTemplate &&
        !intel.weaponValid) {
        intel.weaponName = u8"徒手";
        intel.weaponValid = true;
    }
    return intel.weaponValid;
}

inline bool SKJH_ReadPlayerInventory(DWORD64 player,
                                      SKJH_PlayerIntel& intel,
                                      bool isLocalPlayer = false,
                                      size_t nodeLimit = 512,
                                      size_t itemLimit = 96) {
    intel.inventory.clear();
    intel.inventoryRoot = SKJH_FindPlayerInventoryRoot(player);
    intel.mainContainerId = 0;
    intel.mainContainerCapacity = 0;
    intel.inventoryNodeCount = 0;
    intel.inventoryValid = false;
    intel.inventoryIsLocal = isLocalPlayer;
    intel.inventoryAvailability = SKJH_InventoryAvailability::Unknown;
    if (!Mem::IsUserAddress(intel.inventoryRoot)) {
        intel.inventoryAvailability = isLocalPlayer
            ? SKJH_InventoryAvailability::ReadError
            : SKJH_InventoryAvailability::NotReplicated;
        return false;
    }

    const DWORD64 rootKlass = mem.Read<DWORD64>(intel.inventoryRoot);
    DWORD64 nodesWrapper = 0;
    if (!SKJH_GetReferencePropertyByName(
            intel.inventoryRoot, rootKlass, "Nodes", nodesWrapper) ||
        !Mem::IsUserAddress(nodesWrapper)) {
        intel.inventoryAvailability = isLocalPlayer
            ? SKJH_InventoryAvailability::ReadError
            : SKJH_InventoryAvailability::NotReplicated;
        return false;
    }
    const DWORD64 nodeDictionary = mem.Read<DWORD64>(
        nodesWrapper + g_RuntimeOffsets.customDictionaryInner);
    std::vector<DWORD64> nodes;
    if (!SKJH_ReadManagedDictionaryPointerValues(
            nodeDictionary, nodes, nodeLimit)) {
        intel.inventoryAvailability = SKJH_InventoryAvailability::ReadError;
        return false;
    }
    intel.inventoryNodeCount = nodes.size();

    struct ContainerInfo {
        int32_t id = 0;
        int32_t capacity = 0;
    };
    std::vector<ContainerInfo> containers;
    for (const DWORD64 node : nodes) {
        const DWORD64 klass = mem.Read<DWORD64>(node);
        if (!SKJH_IntelKlassDerivesFrom(klass, "ItemContainerNode"))
            continue;
        ContainerInfo container;
        SKJH_GetPropertyValueByName(node, klass, "Id", container.id);
        SKJH_GetPropertyValueByName(
            node, klass, "Capacity", container.capacity);
        if (container.id > 0 && container.capacity > 0) {
            containers.push_back(container);
            if (container.capacity > intel.mainContainerCapacity) {
                intel.mainContainerCapacity = container.capacity;
                intel.mainContainerId = container.id;
            }
        }
    }

    // A valid inventory root always exposes at least one ItemContainerNode.
    // Treat an unrecognised container layout as a read/schema error instead of
    // reporting a misleading empty backpack after a game update.
    if (containers.empty()) {
        intel.inventoryAvailability = SKJH_InventoryAvailability::ReadError;
        return false;
    }
    intel.inventoryAvailability = SKJH_InventoryAvailability::Available;

    std::unordered_map<int64_t, size_t> byTemplate;
    for (const DWORD64 node : nodes) {
        if (intel.inventory.size() >= itemLimit) break;
        const DWORD64 klass = mem.Read<DWORD64>(node);
        if (!SKJH_IntelKlassDerivesFrom(klass, "BaseItemNode")) continue;

        int32_t parentId = 0;
        int64_t slot = 0;
        int32_t count = 1;
        int64_t bizId = 0;
        SKJH_GetPropertyValueByName(node, klass, "ParentId", parentId);
        SKJH_GetPropertyValueByName(node, klass, "Index", slot);
        SKJH_GetPropertyValueByName(node, klass, "BizId", bizId);
        if (SKJH_IntelKlassDerivesFrom(klass, "StackableItemNode"))
            SKJH_GetPropertyValueByName(node, klass, "Count", count);
        if (intel.mainContainerId && parentId != intel.mainContainerId)
            continue;
        if (count <= 0 || count > 100000000) count = 1;

        std::string name;
        const int64_t templateId = SKJH_SelectItemTemplateId(
            bizId, 0, &name);
        if (!templateId) continue;
        const auto existing = byTemplate.find(templateId);
        if (existing != byTemplate.end()) {
            intel.inventory[existing->second].count += count;
            continue;
        }
        SKJH_PlayerInventoryItem item;
        item.templateId = templateId;
        item.count = count;
        item.containerId = parentId;
        item.slot = slot;
        item.name = std::move(name);
        byTemplate.emplace(templateId, intel.inventory.size());
        intel.inventory.push_back(std::move(item));
    }
    std::sort(intel.inventory.begin(), intel.inventory.end(),
              [](const auto& left, const auto& right) {
                  if (left.count != right.count) return left.count > right.count;
                  return left.name < right.name;
              });
    intel.inventoryValid = !containers.empty();
    return intel.inventoryValid;
}

inline SKJH_PlayerIntel SKJH_ReadPlayerIntel(DWORD64 player,
                                              bool includeInventory = true,
                                              bool isLocalPlayer = false) {
    SKJH_PlayerIntel intel;
    if (!Mem::IsUserAddress(player)) return intel;
    const DWORD64 klass = mem.Read<DWORD64>(player);
    SKJH_ReadPlayerIdentity(player, klass, intel);
    SKJH_ReadPlayerWeapon(player, klass, intel);
    if (!intel.weaponValid && intel.weaponEntityId > 0) {
        const std::unordered_set<int64_t> wanted{intel.weaponEntityId};
        const auto resolved = SKJH_FindEntityPointersById(wanted);
        const auto weapon = resolved.find(intel.weaponEntityId);
        if (weapon != resolved.end())
            SKJH_ReadPlayerWeapon(player, klass, intel, weapon->second);
    }
    if (includeInventory)
        SKJH_ReadPlayerInventory(player, intel, isLocalPlayer);
    return intel;
}
