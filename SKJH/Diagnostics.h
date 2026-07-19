#pragma once

#include "Mem.h"
#include "Offset.h"
#include "ESPUtils.h"
#include "SKJH_Entity.h"
#include "SKJH_Skeleton.h"
#include "TemplateCatalog.h"
#include "PlayerIntel.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

inline std::string SKJH_JsonEscape(const std::string& value) {
    std::ostringstream escaped;
    for (unsigned char ch : value) {
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
                } else {
                    escaped << static_cast<char>(ch);
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
    uint32_t actual = 0;
    return SKJH_FindRuntimeFieldOffset(klass, fieldName, actual) && actual == expected;
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
    return entityManager.valid && mc.valid && mcCommon.valid &&
        SKJH_RuntimeFieldMatches(entityManager.klass, "entities",
                                 g_RuntimeOffsets.entityManagerEntities) &&
        SKJH_RuntimeFieldMatches(mc.klass, "Entity", g_RuntimeOffsets.mcEntity) &&
        SKJH_RuntimeFieldMatches(mc.klass, "MyPlayer", g_RuntimeOffsets.mcMyPlayer) &&
        SKJH_RuntimeFieldMatches(mc.klass, "Go", g_RuntimeOffsets.mcEntityGo) &&
        SKJH_RuntimeFieldMatches(mc.klass, "Camera", g_RuntimeOffsets.mcCamera) &&
        SKJH_RuntimeFieldMatches(mcCommon.klass, "Tables",
                                 g_RuntimeOffsets.mcCommonTables);
}

inline bool SKJH_WaitForRuntimeSdk(DWORD timeoutMs) {
    const auto started = std::chrono::steady_clock::now();
    while (true) {
        if (SKJH_ValidateRuntimeSdk()) return true;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        if (timeoutMs != INFINITE && elapsed >= timeoutMs) return false;
        mem.RefreshTlb();
        Sleep(250);
    }
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
           << (catalogRefreshed && templateStats.complete ? "true" : "false")
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
    return output.good() && gameAssembly && entityManager.valid && mc.valid &&
           mcCommon.valid && runtimeSdkValid && dictionaryValid &&
           !entities.empty() && allEntitiesSampled && camera.valid &&
           localPlayerValid && catalogRefreshed && templateStats.complete &&
           templatesCovered && unknownCount == 0 && skeletonsValid;
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
        }
    }
    output << "\n  ],\n  \"validation\": {\"playerCount\": " << playerCount
           << ", \"weaponCount\": " << weaponCount
           << ", \"inventoryCount\": " << inventoryCount << "}\n}\n";
    output.close();
    return output.good() && playerCount > 0 &&
           healthCount == playerCount && weaponCount == playerCount &&
           localSampled && localInventoryValid;
}
