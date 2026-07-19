#pragma once

#include <Windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

// Runtime SDK manifest. Defaults match the SDK dumped on 2026-07-15; when
// script.json/dump.cs are present, LoadSdkManifest replaces these values.
struct SKJH_RuntimeOffsets {
    uint64_t entityManagerTypeInfo = 0x90E0C10;
    uint64_t mcTypeInfo            = 0x913EE70;
    uint64_t mcCommonTypeInfo      = 0x913F1A8;
    uint64_t languageManagerTypeInfo = 0x92F12B0;

    uint32_t il2cppClassName         = 0x10;
    uint32_t il2cppClassNamespace    = 0x18;
    uint32_t il2cppClassParent       = 0x58;
    uint32_t il2cppClassStaticFields = 0xB8;

    uint32_t entityManagerInstance = 0x00;
    uint32_t entityManagerEntities = 0x18;
    uint32_t entityComponents      = 0x40;
    uint32_t mgrEntitySet          = 0x18;
    uint32_t entitySetCollection   = 0x10;
    uint32_t entitySetInterfaceCollection = 0x18;
    uint32_t entitySetGenericEntities = 0x10;

    uint32_t typeBaseDataSet       = 0x20;
    uint32_t entityEntityId        = 0x30;
    uint32_t dataSetValProps       = 0x18;
    uint32_t dataSetRefProps       = 0x20;
    uint32_t dataSetPropertyMap    = 0x30;
    uint32_t propArrayArray        = 0x10;
    uint32_t customDictionaryInner = 0x28;
    uint32_t rootSystemRoots       = 0x40;
    uint32_t playerAnimHeldItemTableId = 0xD78;

    uint32_t mcEntity              = 0x10;
    uint32_t mcMyPlayer            = 0x50;
    uint32_t mcEntityGo            = 0x58;
    uint32_t mcCamera              = 0x80;
    uint32_t mcCommonTables        = 0x58;
    uint32_t languageManagerInstance = 0x08;
    uint32_t languageManagerDataMap  = 0x10;
    uint32_t tablesItemConfig      = 0x88;
    uint32_t tablesCorpse          = 0xE8;
    uint32_t tablesMonster         = 0x100;
    uint32_t tablesGatherTree      = 0xD0;
    uint32_t tablesGatherOre       = 0xD8;
    uint32_t tablesGatherCollect   = 0xE0;
    uint32_t tablesTreasureBox     = 0x1A0;
    uint32_t tablesElevator        = 0x310;
    uint32_t tablesElevatorPart    = 0x318;
    uint32_t tablesBuildingCore    = 0x4B0;
    uint32_t tablesVehicleInfo     = 0x8E0;
    uint32_t tablesNpc             = 0x9E0;
    uint32_t tablesCaveLift        = 0xD68;
    uint32_t tableDataMap          = 0x30;
    uint32_t tableDataList         = 0x38;
    uint32_t templateBeanId        = 0x10;
    uint32_t templateBuildingId    = 0x10;
    uint32_t itemNameL10nIndex     = 0x18;
    uint32_t treasureNameL10nIndex = 0x2C;
    uint32_t vehicleNameL10nIndex  = 0x18;
    uint32_t oreNameL10nIndex      = 0x18;
    uint32_t collectNameL10nIndex  = 0x18;
    uint32_t treeNameL10nIndex     = 0x18;
    uint32_t corpseNameL10nIndex   = 0x18;
    uint32_t monsterNameL10nIndex  = 0x18;
    uint32_t npcNameL10nIndex      = 0x1C;
    uint32_t buildingTextL10nIndex = 0x5C;
    uint32_t elevatorNameL10nIndex = 0x18;
    uint32_t caveLiftNameL10nIndex = 0x20;
    uint32_t itemConstName         = 0x130;
    uint32_t itemIconPath          = 0x90;
    uint32_t itemDropModelPath     = 0xA0;
    uint32_t treasurePrefab       = 0x38;
    uint32_t treasureConstName     = 0xD0;
    uint32_t vehicleConstName      = 0x20;
    uint32_t vehiclePrefabPath     = 0x30;
    uint32_t oreGatherType         = 0x30;
    uint32_t oreResourcePath       = 0x48;
    uint32_t collectGatherType     = 0x28;
    uint32_t collectResourcePath   = 0x40;
    uint32_t treeResourcePath      = 0x48;
    uint32_t corpseResourcePath    = 0x40;
    uint32_t monsterDisplayName    = 0x20;
    uint32_t monsterPrefabPath     = 0x48;
    uint32_t npcModelPath          = 0x20;
    uint32_t buildingName          = 0x18;
    uint32_t elevatorTemplateId    = 0x10;
    uint32_t elevatorModelPath     = 0x20;
    uint32_t elevatorPartId        = 0x10;
    uint32_t elevatorPartModelPath = 0x20;
    uint32_t caveLiftId            = 0x10;
    uint32_t caveLiftModelPath     = 0x18;
    uint32_t mgrMyPlayerLocal      = 0x20;
    uint32_t mgrMyPlayerTpGo       = 0x50;
    uint32_t mgrMyPlayerBoneMgr    = 0xA0;
    uint32_t playerBoneCamera      = 0x18;
    uint32_t mgrEntityGoGos        = 0x18;
    uint32_t basePlayerGoEntity    = 0x68;

    uint32_t mgrCameraController   = 0x18;
    uint32_t cameraNowState        = 0x10;
    uint32_t cameraYaw             = 0xA4;
    uint32_t cameraPitch           = 0xA8;
    uint32_t cameraRoll            = 0xAC;
    uint32_t cameraCurrentFov      = 0xC4;
    uint32_t baseCameraTransform   = 0x20;

    uint32_t clientPlayerHead      = 0xD8;
    uint32_t clientPlayerBody      = 0xE0;
    uint32_t clientPlayerSpine     = 0xE8;
    uint32_t clientPlayerLeftFoot  = 0xF0;
    uint32_t clientPlayerRightFoot = 0xF8;
    uint32_t clientPlayerNeck      = 0x100;

    uint32_t unityObjectCachedPtr  = 0x10;
    // Current Unity native Transform profile. The probe validates this
    // profile geometrically before publishing bone/camera coordinates.
    uint32_t unityNativeData       = 0x50;
    uint32_t unityNativeIndex      = 0x58;
    uint32_t unityDataTransforms   = 0x18;
    uint32_t unityDataParents      = 0x20;
    uint32_t unityTransformStride  = 0x30;

    bool scriptLoaded = false;
    bool dumpLoaded   = false;
    std::filesystem::path scriptPath;
    std::filesystem::path dumpPath;
};

inline SKJH_RuntimeOffsets g_RuntimeOffsets;

namespace SKJH_SdkDetail {

inline std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

inline bool ParseIntegerAfter(const std::string& line, char separator, uint64_t& value) {
    const auto pos = line.find(separator);
    if (pos == std::string::npos) return false;
    const char* begin = line.c_str() + pos + 1;
    while (*begin == ' ' || *begin == '\t') ++begin;
    char* end = nullptr;
    const int base = (begin[0] == '0' && (begin[1] == 'x' || begin[1] == 'X')) ? 16 : 10;
    value = std::strtoull(begin, &end, base);
    return end != begin;
}

inline bool ParseHexOffset(const std::string& line, uint64_t& value) {
    const auto pos = line.find("0x");
    if (pos == std::string::npos) return false;
    const char* begin = line.c_str() + pos + 2;
    char* end = nullptr;
    value = std::strtoull(begin, &end, 16);
    return end != begin;
}

inline std::string DeclaredFieldName(const std::string& line) {
    const auto semicolon = line.find(';');
    if (semicolon == std::string::npos) return {};
    const auto start = line.find_last_of(" \t", semicolon - 1);
    if (start == std::string::npos || start + 1 >= semicolon) return {};
    return line.substr(start + 1, semicolon - start - 1);
}

inline std::vector<std::filesystem::path> CandidatePaths(const wchar_t* filename) {
    std::vector<std::filesystem::path> result;
    std::error_code ec;
    result.emplace_back(std::filesystem::current_path(ec) / filename);

    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH)) {
        auto dir = std::filesystem::path(modulePath).parent_path();
        for (int i = 0; i < 5 && !dir.empty(); ++i) {
            result.emplace_back(dir / filename);
            dir = dir.parent_path();
        }
    }

    std::vector<std::filesystem::path> unique;
    for (const auto& candidate : result) {
        const auto normalized = candidate.lexically_normal();
        bool found = false;
        for (const auto& existing : unique) {
            if (existing == normalized) { found = true; break; }
        }
        if (!found) unique.push_back(normalized);
    }
    return unique;
}

inline std::filesystem::path FindSdkFile(const wchar_t* filename) {
    std::error_code ec;
    for (const auto& candidate : CandidatePaths(filename)) {
        if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
        ec.clear();
    }
    return {};
}

inline bool FindSdkPair(std::filesystem::path& scriptPath,
                        std::filesystem::path& dumpPath) {
    std::error_code ec;
    for (const auto& candidate : CandidatePaths(L"script.json")) {
        const auto directory = candidate.parent_path();
        const auto script = directory / L"script.json";
        const auto dump = directory / L"dump.cs";
        if (std::filesystem::is_regular_file(script, ec) &&
            std::filesystem::is_regular_file(dump, ec)) {
            scriptPath = script;
            dumpPath = dump;
            return true;
        }
        ec.clear();
    }
    return false;
}

inline bool ParseScriptJson(const std::filesystem::path& path, SKJH_RuntimeOffsets& offsets) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;

    constexpr const char* kEntityManager =
        "WizardGames.Soc.Share.Framework.EntityManager_TypeInfo";
    constexpr const char* kMc =
        "WizardGames.Soc.SocClient.Manager.Mc_TypeInfo";
    constexpr const char* kMcCommon =
        "WizardGames.Soc.Common.Manager.McCommon_TypeInfo";
    constexpr const char* kLanguageManager =
        "WizardGames.Soc.SocClient.Data.LanguageManager_TypeInfo";

    uint64_t objectAddress = 0;
    bool haveAddress = false;
    std::string objectName;
    bool foundEntity = false;
    bool foundMc = false;
    bool foundMcCommon = false;
    bool foundLanguageManager = false;
    std::string line;
    while (std::getline(input, line)) {
        if (line.find('{') != std::string::npos) {
            objectAddress = 0;
            haveAddress = false;
            objectName.clear();
        }
        if (line.find("\"Address\"") != std::string::npos) {
            haveAddress = ParseIntegerAfter(line, ':', objectAddress);
        } else if (line.find("\"Name\"") != std::string::npos) {
            const auto colon = line.find(':');
            const auto firstQuote = colon == std::string::npos
                ? std::string::npos : line.find('"', colon + 1);
            const auto lastQuote = firstQuote == std::string::npos
                ? std::string::npos : line.find('"', firstQuote + 1);
            if (firstQuote != std::string::npos && lastQuote != std::string::npos)
                objectName = line.substr(firstQuote + 1, lastQuote - firstQuote - 1);
        }
        if (!haveAddress || objectName.empty()) continue;
        if (objectName == kEntityManager) {
            offsets.entityManagerTypeInfo = objectAddress;
            foundEntity = true;
        } else if (objectName == kMc) {
            offsets.mcTypeInfo = objectAddress;
            foundMc = true;
        } else if (objectName == kMcCommon) {
            offsets.mcCommonTypeInfo = objectAddress;
            foundMcCommon = true;
        } else if (objectName == kLanguageManager) {
            offsets.languageManagerTypeInfo = objectAddress;
            foundLanguageManager = true;
        }
        if (foundEntity && foundMc && foundMcCommon && foundLanguageManager) break;
    }
    return foundEntity && foundMc && foundMcCommon && foundLanguageManager;
}

struct FieldTarget {
    const char* nameSpace;
    const char* className;
    const char* fieldName;
    uint32_t* destination;
};

inline std::string ParseTypeName(const std::string& line) {
    static constexpr const char* markers[] = { " class ", " struct " };
    for (const char* marker : markers) {
        auto pos = line.find(marker);
        if (pos == std::string::npos) continue;
        pos += std::strlen(marker);
        const auto end = line.find_first_of(" :<{\r\n", pos);
        return line.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    }
    return {};
}

inline bool ParseDumpCs(const std::filesystem::path& path, SKJH_RuntimeOffsets& o) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;

    std::vector<FieldTarget> targets = {
        {"WizardGames.Soc.Share.Framework", "EntityManager", "entities", &o.entityManagerEntities},
        {"WizardGames.Soc.Share.Framework", "TypeBase", "DataSet", &o.typeBaseDataSet},
        {"WizardGames.Soc.Share.Framework", "EntityBase", "<EntityId>k__BackingField", &o.entityEntityId},
        {"WizardGames.Soc.Share.Framework", "EntityBase", "components", &o.entityComponents},
        {"WizardGames.Soc.Common.Entity", "BaseMgrEntity", "EntitySet", &o.mgrEntitySet},
        {"WizardGames.Soc.Common.Entity.Collection", "EntitySetCollection", "EntityCollection", &o.entitySetCollection},
        {"WizardGames.Soc.Common.Entity.Collection", "EntitySetCollection", "InterfaceCollection", &o.entitySetInterfaceCollection},
        {"WizardGames.Soc.Common.Entity.EntityCollection", "EntitySet", "GenericEntities", &o.entitySetGenericEntities},
        {"WizardGames.Soc.Share.Framework", "ArrayDataSet", "valProps", &o.dataSetValProps},
        {"WizardGames.Soc.Share.Framework", "ArrayDataSet", "refProps", &o.dataSetRefProps},
        {"WizardGames.Soc.Share.Framework", "ArrayDataSet", "propertyId2Index", &o.dataSetPropertyMap},
        {"WizardGames.Soc.Common.Component", "RootNodeComponent", "systemRoots", &o.rootSystemRoots},
        {"WizardGames.Soc.Common.Entity", "PlayerEntity", "Anim_HeldItemTableId", &o.playerAnimHeldItemTableId},
        {"WizardGames.Soc.SocClient.Manager", "Mc", "Entity", &o.mcEntity},
        {"WizardGames.Soc.SocClient.Manager", "Mc", "MyPlayer", &o.mcMyPlayer},
        {"WizardGames.Soc.SocClient.Manager", "Mc", "Go", &o.mcEntityGo},
        {"WizardGames.Soc.SocClient.Manager", "Mc", "Camera", &o.mcCamera},
        {"WizardGames.Soc.Common.Manager", "McCommon", "Tables", &o.mcCommonTables},
        {"WizardGames.Soc.SocClient.Data", "LanguageManager", "_instance", &o.languageManagerInstance},
        {"WizardGames.Soc.SocClient.Data", "LanguageManager", "DataMap", &o.languageManagerDataMap},
        {"WizardGames.Soc.Common.Data", "MgrTables", "<TbItemConfig>k__BackingField", &o.tablesItemConfig},
        {"WizardGames.Soc.Common.Data", "MgrTables", "<TbCorpse>k__BackingField", &o.tablesCorpse},
        {"WizardGames.Soc.Common.Data", "MgrTables", "<TbMonster>k__BackingField", &o.tablesMonster},
        {"WizardGames.Soc.Common.Data", "MgrTables", "<TbGatherResourcesTree>k__BackingField", &o.tablesGatherTree},
        {"WizardGames.Soc.Common.Data", "MgrTables", "<TbGatherResourcesOre>k__BackingField", &o.tablesGatherOre},
        {"WizardGames.Soc.Common.Data", "MgrTables", "<TbGatherResourcesCollectable>k__BackingField", &o.tablesGatherCollect},
        {"WizardGames.Soc.Common.Data", "MgrTables", "<TbTreasureBox>k__BackingField", &o.tablesTreasureBox},
        {"WizardGames.Soc.Common.Data", "MgrTables", "<TbElevatorInteractive>k__BackingField", &o.tablesElevator},
        {"WizardGames.Soc.Common.Data", "MgrTables", "<TbElevatorInteractiveComponent>k__BackingField", &o.tablesElevatorPart},
        {"WizardGames.Soc.Common.Data", "MgrTables", "<TbBuildingCore>k__BackingField", &o.tablesBuildingCore},
        {"WizardGames.Soc.Common.Data", "MgrTables", "<TbVehicleInfo>k__BackingField", &o.tablesVehicleInfo},
        {"WizardGames.Soc.Common.Data", "MgrTables", "<TbNPC>k__BackingField", &o.tablesNpc},
        {"WizardGames.Soc.Common.Data", "MgrTables", "<TbCaveLiftConfig>k__BackingField", &o.tablesCaveLift},
        {"WizardGames.Soc.Common.Data.DataItem", "TbItemConfig", "DataMap", &o.tableDataMap},
        {"WizardGames.Soc.Common.Data.DataItem", "TbItemConfig", "dataList", &o.tableDataList},
        {"WizardGames.Soc.Common.Data.DataItem", "ItemConfig", "Id", &o.templateBeanId},
        {"WizardGames.Soc.Common.Data.constraction", "BuildingCore", "PartId", &o.templateBuildingId},
        {"WizardGames.Soc.Common.Data.DataItem", "ItemConfig", "Name_l10n_index", &o.itemNameL10nIndex},
        {"WizardGames.Soc.Common.Data.units", "TreasureBox", "Name_l10n_index", &o.treasureNameL10nIndex},
        {"WizardGames.Soc.Common.Data.Vehicle", "VehicleInfo", "Name_l10n_index", &o.vehicleNameL10nIndex},
        {"WizardGames.Soc.Common.Data.units", "GatherResourcesOre", "Name_l10n_index", &o.oreNameL10nIndex},
        {"WizardGames.Soc.Common.Data.units", "GatherResourcesCollectable", "Name_l10n_index", &o.collectNameL10nIndex},
        {"WizardGames.Soc.Common.Data.units", "GatherResourcesTree", "Name_l10n_index", &o.treeNameL10nIndex},
        {"WizardGames.Soc.Common.Data.units", "Corpse", "Name_l10n_index", &o.corpseNameL10nIndex},
        {"WizardGames.Soc.Common.Data.units", "Monster", "Name_l10n_index", &o.monsterNameL10nIndex},
        {"WizardGames.Soc.Common.Data.units", "NPC", "NpcName_l10n_index", &o.npcNameL10nIndex},
        {"WizardGames.Soc.Common.Data.constraction", "BuildingCore", "Text_l10n_index", &o.buildingTextL10nIndex},
        {"WizardGames.Soc.Common.Data.units", "ElevatorInteractive", "ElevatorName_l10n_index", &o.elevatorNameL10nIndex},
        {"WizardGames.Soc.Common.Data.units", "CaveLiftConfig", "ElevatorName_l10n_index", &o.caveLiftNameL10nIndex},
        {"WizardGames.Soc.Common.Data.DataItem", "ItemConfig", "ConstName", &o.itemConstName},
        {"WizardGames.Soc.Common.Data.DataItem", "ItemConfig", "Icon", &o.itemIconPath},
        {"WizardGames.Soc.Common.Data.DataItem", "ItemConfig", "DropModelPath", &o.itemDropModelPath},
        {"WizardGames.Soc.Common.Data.units", "TreasureBox", "Prefab", &o.treasurePrefab},
        {"WizardGames.Soc.Common.Data.units", "TreasureBox", "ConstName", &o.treasureConstName},
        {"WizardGames.Soc.Common.Data.Vehicle", "VehicleInfo", "ConstName", &o.vehicleConstName},
        {"WizardGames.Soc.Common.Data.Vehicle", "VehicleInfo", "PrefabPath", &o.vehiclePrefabPath},
        {"WizardGames.Soc.Common.Data.units", "GatherResourcesOre", "GatherType", &o.oreGatherType},
        {"WizardGames.Soc.Common.Data.units", "GatherResourcesOre", "ResPath", &o.oreResourcePath},
        {"WizardGames.Soc.Common.Data.units", "GatherResourcesCollectable", "GatherType", &o.collectGatherType},
        {"WizardGames.Soc.Common.Data.units", "GatherResourcesCollectable", "ResPath", &o.collectResourcePath},
        {"WizardGames.Soc.Common.Data.units", "GatherResourcesTree", "ResPath", &o.treeResourcePath},
        {"WizardGames.Soc.Common.Data.units", "Corpse", "ResPath", &o.corpseResourcePath},
        {"WizardGames.Soc.Common.Data.units", "Monster", "HpBarShowName", &o.monsterDisplayName},
        {"WizardGames.Soc.Common.Data.units", "Monster", "PrefabPath", &o.monsterPrefabPath},
        {"WizardGames.Soc.Common.Data.units", "NPC", "NpcModel", &o.npcModelPath},
        {"WizardGames.Soc.Common.Data.constraction", "BuildingCore", "Name", &o.buildingName},
        {"WizardGames.Soc.Common.Data.units", "ElevatorInteractive", "ElevatorTemplateId", &o.elevatorTemplateId},
        {"WizardGames.Soc.Common.Data.units", "ElevatorInteractive", "ElevatorPlatformModelPath", &o.elevatorModelPath},
        {"WizardGames.Soc.Common.Data.units", "ElevatorInteractiveComponent", "ComponentTemplateId", &o.elevatorPartId},
        {"WizardGames.Soc.Common.Data.units", "ElevatorInteractiveComponent", "ComponentModelPath", &o.elevatorPartModelPath},
        {"WizardGames.Soc.Common.Data.units", "CaveLiftConfig", "Id", &o.caveLiftId},
        {"WizardGames.Soc.Common.Data.units", "CaveLiftConfig", "ModelPath", &o.caveLiftModelPath},
        {"WizardGames.Soc.Common.Syncronization", "MgrMyPlayer", "MyEntityLocal", &o.mgrMyPlayerLocal},
        {"WizardGames.Soc.Common.Syncronization", "MgrMyPlayer", "TpPlayerGo", &o.mgrMyPlayerTpGo},
        {"WizardGames.Soc.Common.Syncronization", "MgrMyPlayer", "PlayerBoneManager", &o.mgrMyPlayerBoneMgr},
        {"WizardGames.Soc.SocClient.Player", "PlayerBoneManager", "cameraLocator", &o.playerBoneCamera},
        {"WizardGames.Soc.Common.Unity.Go", "BaseMgrEntityGo", "Gos", &o.mgrEntityGoGos},
        {"WizardGames.Soc.Common.Unity.Go", "BasePlayerGo", "<PlayerEntity>k__BackingField", &o.basePlayerGoEntity},
        {"WizardGames.Soc.Common.Unity", "MgrCamera", "_controller", &o.mgrCameraController},
        {"WizardGames.Soc.Common.Unity", "CameraStateController", "_nowCameraState", &o.cameraNowState},
        {"WizardGames.Soc.Common.Unity", "CameraStateController", "CameraYaw", &o.cameraYaw},
        {"WizardGames.Soc.Common.Unity", "CameraStateController", "CameraPitch", &o.cameraPitch},
        {"WizardGames.Soc.Common.Unity", "CameraStateController", "CameraRoll", &o.cameraRoll},
        {"WizardGames.Soc.Common.Unity", "CameraStateController", "CurrentGunFov", &o.cameraCurrentFov},
        {"WizardGames.Soc.Common.Unity", "BaseCameraState", "<SceneCameraTransform>k__BackingField", &o.baseCameraTransform},
        {"WizardGames.Soc.Common.Unity.Go", "ClientPlayerGo", "HeadBone", &o.clientPlayerHead},
        {"WizardGames.Soc.Common.Unity.Go", "ClientPlayerGo", "BodyBone", &o.clientPlayerBody},
        {"WizardGames.Soc.Common.Unity.Go", "ClientPlayerGo", "SpineBone", &o.clientPlayerSpine},
        {"WizardGames.Soc.Common.Unity.Go", "ClientPlayerGo", "LeftFootBone", &o.clientPlayerLeftFoot},
        {"WizardGames.Soc.Common.Unity.Go", "ClientPlayerGo", "RightFootBone", &o.clientPlayerRightFoot},
        {"WizardGames.Soc.Common.Unity.Go", "ClientPlayerGo", "Bip01Neck", &o.clientPlayerNeck},
        {"UnityEngine", "Object", "m_CachedPtr", &o.unityObjectCachedPtr},
    };

    std::string currentNamespace;
    std::string currentClass;
    std::string line;
    size_t found = 0;
    while (std::getline(input, line)) {
        constexpr const char* nsMarker = "// Namespace:";
        if (line.rfind(nsMarker, 0) == 0) {
            currentNamespace = Trim(line.substr(std::strlen(nsMarker)));
            continue;
        }
        const auto parsedClass = ParseTypeName(line);
        if (!parsedClass.empty()) {
            currentClass = parsedClass;
            continue;
        }
        const auto comment = line.find("// 0x");
        if (comment == std::string::npos) continue;

        const std::string declaredField = DeclaredFieldName(line);
        for (auto& target : targets) {
            if (!target.destination || currentNamespace != target.nameSpace ||
                currentClass != target.className || declaredField != target.fieldName) {
                continue;
            }
            uint64_t parsed = 0;
            if (ParseHexOffset(line.substr(comment), parsed)) {
                *target.destination = static_cast<uint32_t>(parsed);
                target.destination = nullptr;
                ++found;
            }
            break;
        }
        if (found == targets.size()) break;
    }
    return found == targets.size();
}

inline bool ValidateManifestShape(const SKJH_RuntimeOffsets& o) {
    const auto validRva = [](uint64_t value) {
        return value >= 0x10000 && value < 0x100000000ull;
    };
    if (!validRva(o.entityManagerTypeInfo) || !validRva(o.mcTypeInfo) ||
        !validRva(o.mcCommonTypeInfo) || !validRva(o.languageManagerTypeInfo) ||
        o.entityManagerTypeInfo == o.mcTypeInfo ||
        o.mcTypeInfo == o.mcCommonTypeInfo ||
        o.languageManagerTypeInfo == o.entityManagerTypeInfo ||
        o.languageManagerTypeInfo == o.mcTypeInfo ||
        o.languageManagerTypeInfo == o.mcCommonTypeInfo) return false;

    // Every dump.cs target is an instance/static field offset and must be
    // non-zero. entityManagerInstance is intentionally excluded because the
    // EntityManager singleton occupies offset 0 in its static-fields block.
    const uint32_t required[] = {
        o.il2cppClassName, o.il2cppClassNamespace, o.il2cppClassParent,
        o.il2cppClassStaticFields, o.entityManagerEntities, o.typeBaseDataSet,
        o.entityEntityId, o.entityComponents, o.mgrEntitySet,
        o.entitySetCollection, o.entitySetInterfaceCollection,
        o.entitySetGenericEntities, o.dataSetValProps,
        o.dataSetRefProps, o.dataSetPropertyMap, o.propArrayArray,
        o.customDictionaryInner, o.rootSystemRoots,
        o.playerAnimHeldItemTableId,

        o.mcEntity, o.mcMyPlayer, o.mcEntityGo, o.mcCamera,
        o.mcCommonTables, o.languageManagerInstance,
        o.languageManagerDataMap, o.tablesItemConfig, o.tablesCorpse,
        o.tablesMonster, o.tablesGatherTree, o.tablesGatherOre,
        o.tablesGatherCollect, o.tablesTreasureBox, o.tablesElevator,
        o.tablesElevatorPart, o.tablesBuildingCore, o.tablesVehicleInfo,
        o.tablesNpc, o.tablesCaveLift,

        o.tableDataMap, o.tableDataList, o.templateBeanId,
        o.templateBuildingId, o.itemNameL10nIndex,
        o.treasureNameL10nIndex, o.vehicleNameL10nIndex,
        o.oreNameL10nIndex, o.collectNameL10nIndex,
        o.treeNameL10nIndex, o.corpseNameL10nIndex,
        o.monsterNameL10nIndex, o.npcNameL10nIndex,
        o.buildingTextL10nIndex, o.elevatorNameL10nIndex,
        o.caveLiftNameL10nIndex, o.itemConstName, o.itemIconPath,
        o.itemDropModelPath, o.treasurePrefab, o.treasureConstName,
        o.vehicleConstName, o.vehiclePrefabPath, o.oreGatherType,
        o.oreResourcePath, o.collectGatherType, o.collectResourcePath,
        o.treeResourcePath, o.corpseResourcePath, o.monsterDisplayName,
        o.monsterPrefabPath, o.npcModelPath, o.buildingName,
        o.elevatorTemplateId, o.elevatorModelPath, o.elevatorPartId,
        o.elevatorPartModelPath, o.caveLiftId, o.caveLiftModelPath,

        o.mgrMyPlayerLocal, o.mgrMyPlayerTpGo, o.mgrMyPlayerBoneMgr,
        o.playerBoneCamera, o.mgrEntityGoGos, o.basePlayerGoEntity,
        o.mgrCameraController, o.cameraNowState, o.cameraYaw,
        o.cameraPitch, o.cameraRoll, o.cameraCurrentFov,
        o.baseCameraTransform, o.clientPlayerHead, o.clientPlayerBody,
        o.clientPlayerSpine, o.clientPlayerLeftFoot,
        o.clientPlayerRightFoot, o.clientPlayerNeck,

        o.unityObjectCachedPtr, o.unityNativeData, o.unityNativeIndex,
        o.unityDataTransforms, o.unityDataParents, o.unityTransformStride,
    };
    for (const uint32_t value : required)
        if (!value || value >= 0x4000) return false;
    return true;
}

inline uint64_t FileSize(const std::filesystem::path& path) {
    std::error_code ec;
    const auto value = std::filesystem::file_size(path, ec);
    return ec ? 0 : static_cast<uint64_t>(value);
}

inline int64_t FileStamp(const std::filesystem::path& path) {
    std::error_code ec;
    const auto value = std::filesystem::last_write_time(path, ec);
    return ec ? 0 : static_cast<int64_t>(value.time_since_epoch().count());
}

inline bool FileSha256(const std::filesystem::path& path, std::string& fingerprint) {
    fingerprint.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool success = false;
    const char* failureStage = "BCryptOpenAlgorithmProvider";
    NTSTATUS failureStatus = 0;
    do {
        failureStatus = BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (failureStatus < 0) break;

        DWORD objectLength = 0;
        DWORD hashLength = 0;
        DWORD copied = 0;
        failureStage = "BCryptGetProperty(BCRYPT_OBJECT_LENGTH)";
        failureStatus = BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
            &copied, 0);
        if (failureStatus < 0 || copied != sizeof(objectLength) || !objectLength) {
            break;
        }
        failureStage = "BCryptGetProperty(BCRYPT_HASH_LENGTH)";
        failureStatus = BCryptGetProperty(
            algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength),
            &copied, 0);
        if (failureStatus < 0 || copied != sizeof(hashLength) || !hashLength) {
            break;
        }

        std::vector<UCHAR> hashObject(objectLength);
        std::vector<UCHAR> digest(hashLength);
        failureStage = "BCryptCreateHash";
        failureStatus = BCryptCreateHash(
            algorithm, &hash, hashObject.data(), objectLength, nullptr, 0, 0);
        if (failureStatus < 0) {
            break;
        }

        constexpr size_t kBufferSize = 1024u * 1024u;
        std::vector<UCHAR> buffer(kBufferSize);
        bool hashFailed = false;
        while (input) {
            input.read(reinterpret_cast<char*>(buffer.data()),
                       static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if (count > 0) {
                failureStage = "BCryptHashData";
                failureStatus = BCryptHashData(
                    hash, buffer.data(), static_cast<ULONG>(count), 0);
                if (failureStatus < 0) {
                    hashFailed = true;
                    break;
                }
            }
        }
        if (hashFailed) break;
        failureStage = "file read";
        failureStatus = 0;
        if (input.bad() || !input.eof()) break;
        failureStage = "BCryptFinishHash";
        failureStatus = BCryptFinishHash(hash, digest.data(), hashLength, 0);
        if (failureStatus < 0) break;

        static constexpr char kHex[] = "0123456789abcdef";
        fingerprint.resize(digest.size() * 2);
        for (size_t index = 0; index < digest.size(); ++index) {
            fingerprint[index * 2] = kHex[digest[index] >> 4];
            fingerprint[index * 2 + 1] = kHex[digest[index] & 0x0F];
        }
        success = true;
    } while (false);

    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!success) {
        fingerprint.clear();
        std::fprintf(stderr, "SDK cache SHA-256 failed at %s (0x%08lX): %ls\n",
                     failureStage, static_cast<unsigned long>(failureStatus),
                     path.c_str());
    }
    return success;
}

inline bool ReadCache(const std::filesystem::path& cachePath,
                      const std::filesystem::path& scriptPath,
                      const std::filesystem::path& dumpPath,
                      SKJH_RuntimeOffsets& o) {
    std::ifstream input(cachePath, std::ios::binary);
    std::string magic;
    std::string expectedScriptFingerprint;
    std::string expectedDumpFingerprint;
    uint64_t scriptSize = 0, dumpSize = 0;
    int64_t scriptStamp = 0, dumpStamp = 0;
    if (!(input >> magic >> scriptSize >> scriptStamp >> expectedScriptFingerprint
          >> dumpSize >> dumpStamp >> expectedDumpFingerprint) ||
        magic != "SKJH_SDK_CACHE_V13" || scriptSize != FileSize(scriptPath) ||
        scriptStamp != FileStamp(scriptPath) || dumpSize != FileSize(dumpPath) ||
        dumpStamp != FileStamp(dumpPath)) return false;
    std::string scriptFingerprint;
    std::string dumpFingerprint;
    if (!FileSha256(scriptPath, scriptFingerprint) ||
        !FileSha256(dumpPath, dumpFingerprint) ||
        scriptSize != FileSize(scriptPath) || scriptStamp != FileStamp(scriptPath) ||
        dumpSize != FileSize(dumpPath) || dumpStamp != FileStamp(dumpPath) ||
        scriptFingerprint != expectedScriptFingerprint ||
        dumpFingerprint != expectedDumpFingerprint) {
        return false;
    }
    input >> std::hex
        >> o.entityManagerTypeInfo >> o.mcTypeInfo >> o.mcCommonTypeInfo
        >> o.languageManagerTypeInfo
        >> o.languageManagerInstance >> o.languageManagerDataMap
        >> o.entityManagerEntities >> o.typeBaseDataSet >> o.entityEntityId
        >> o.entityComponents >> o.mgrEntitySet >> o.entitySetCollection
        >> o.entitySetInterfaceCollection >> o.entitySetGenericEntities
        >> o.dataSetValProps >> o.dataSetRefProps
        >> o.dataSetPropertyMap >> o.customDictionaryInner >> o.rootSystemRoots
        >> o.playerAnimHeldItemTableId
        >> o.mcEntity >> o.mcMyPlayer >> o.mcEntityGo >> o.mcCamera
        >> o.mcCommonTables >> o.tablesItemConfig >> o.tablesGatherTree
        >> o.tablesGatherOre >> o.tablesGatherCollect >> o.tablesTreasureBox
        >> o.tablesVehicleInfo >> o.tablesCorpse >> o.tablesMonster
        >> o.tablesBuildingCore >> o.tablesNpc
        >> o.tablesElevator >> o.tablesElevatorPart >> o.tablesCaveLift
        >> o.tableDataMap >> o.tableDataList >> o.templateBeanId >> o.templateBuildingId
        >> o.itemNameL10nIndex >> o.treasureNameL10nIndex
        >> o.vehicleNameL10nIndex >> o.oreNameL10nIndex
        >> o.collectNameL10nIndex >> o.treeNameL10nIndex
        >> o.corpseNameL10nIndex >> o.monsterNameL10nIndex
        >> o.npcNameL10nIndex >> o.buildingTextL10nIndex
        >> o.elevatorNameL10nIndex >> o.caveLiftNameL10nIndex
        >> o.itemConstName >> o.itemIconPath >> o.itemDropModelPath
        >> o.treasurePrefab >> o.treasureConstName
        >> o.vehicleConstName >> o.vehiclePrefabPath
        >> o.oreGatherType >> o.oreResourcePath
        >> o.collectGatherType >> o.collectResourcePath >> o.treeResourcePath
        >> o.corpseResourcePath >> o.monsterDisplayName >> o.monsterPrefabPath
        >> o.npcModelPath >> o.buildingName
        >> o.elevatorTemplateId >> o.elevatorModelPath
        >> o.elevatorPartId >> o.elevatorPartModelPath
        >> o.caveLiftId >> o.caveLiftModelPath
        >> o.mgrMyPlayerLocal >> o.mgrMyPlayerTpGo >> o.mgrMyPlayerBoneMgr >> o.playerBoneCamera
        >> o.mgrEntityGoGos >> o.basePlayerGoEntity
        >> o.mgrCameraController >> o.cameraNowState
        >> o.cameraYaw >> o.cameraPitch >> o.cameraRoll >> o.cameraCurrentFov
        >> o.baseCameraTransform
        >> o.clientPlayerHead >> o.clientPlayerBody >> o.clientPlayerSpine
        >> o.clientPlayerLeftFoot >> o.clientPlayerRightFoot >> o.clientPlayerNeck
        >> o.unityObjectCachedPtr;
    if (!input) return false;
    o.scriptLoaded = true;
    o.dumpLoaded = true;
    return true;
}

inline void WriteCache(const std::filesystem::path& cachePath,
                       const std::filesystem::path& scriptPath,
                       const std::filesystem::path& dumpPath,
                       const SKJH_RuntimeOffsets& o) {
    const uint64_t scriptSize = FileSize(scriptPath);
    const int64_t scriptStamp = FileStamp(scriptPath);
    const uint64_t dumpSize = FileSize(dumpPath);
    const int64_t dumpStamp = FileStamp(dumpPath);
    std::string scriptFingerprint;
    std::string dumpFingerprint;
    if (!scriptSize || !dumpSize ||
        !FileSha256(scriptPath, scriptFingerprint) ||
        !FileSha256(dumpPath, dumpFingerprint) ||
        scriptSize != FileSize(scriptPath) || scriptStamp != FileStamp(scriptPath) ||
        dumpSize != FileSize(dumpPath) || dumpStamp != FileStamp(dumpPath)) {
        return;
    }

    std::filesystem::path temporary = cachePath;
    temporary += L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return;
    output << "SKJH_SDK_CACHE_V13\n"
           << scriptSize << ' ' << scriptStamp << ' ' << scriptFingerprint << ' '
           << dumpSize << ' ' << dumpStamp << ' ' << dumpFingerprint << '\n'
           << std::hex
           << o.entityManagerTypeInfo << ' ' << o.mcTypeInfo << ' ' << o.mcCommonTypeInfo << ' '
           << o.languageManagerTypeInfo << ' '
           << o.languageManagerInstance << ' ' << o.languageManagerDataMap << ' '
           << o.entityManagerEntities << ' ' << o.typeBaseDataSet << ' ' << o.entityEntityId << ' '
           << o.entityComponents << ' ' << o.mgrEntitySet << ' ' << o.entitySetCollection << ' '
           << o.entitySetInterfaceCollection << ' ' << o.entitySetGenericEntities << ' '
           << o.dataSetValProps << ' ' << o.dataSetRefProps << ' '
           << o.dataSetPropertyMap << ' ' << o.customDictionaryInner << ' ' << o.rootSystemRoots << ' '
           << o.playerAnimHeldItemTableId << ' '
           << o.mcEntity << ' ' << o.mcMyPlayer << ' ' << o.mcEntityGo << ' ' << o.mcCamera << ' '
           << o.mcCommonTables << ' ' << o.tablesItemConfig << ' ' << o.tablesGatherTree << ' '
           << o.tablesGatherOre << ' ' << o.tablesGatherCollect << ' '
           << o.tablesTreasureBox << ' ' << o.tablesVehicleInfo << ' '
           << o.tablesCorpse << ' ' << o.tablesMonster << ' '
           << o.tablesBuildingCore << ' ' << o.tablesNpc << ' '
           << o.tablesElevator << ' ' << o.tablesElevatorPart << ' '
           << o.tablesCaveLift << ' '
           << o.tableDataMap << ' ' << o.tableDataList << ' '
           << o.templateBeanId << ' ' << o.templateBuildingId << ' '
           << o.itemNameL10nIndex << ' ' << o.treasureNameL10nIndex << ' '
           << o.vehicleNameL10nIndex << ' ' << o.oreNameL10nIndex << ' '
           << o.collectNameL10nIndex << ' ' << o.treeNameL10nIndex << ' '
           << o.corpseNameL10nIndex << ' ' << o.monsterNameL10nIndex << ' '
           << o.npcNameL10nIndex << ' ' << o.buildingTextL10nIndex << ' '
           << o.elevatorNameL10nIndex << ' ' << o.caveLiftNameL10nIndex << ' '
           << o.itemConstName << ' ' << o.itemIconPath << ' '
           << o.itemDropModelPath << ' ' << o.treasurePrefab << ' '
           << o.treasureConstName << ' ' << o.vehicleConstName << ' '
           << o.vehiclePrefabPath << ' ' << o.oreGatherType << ' '
           << o.oreResourcePath << ' ' << o.collectGatherType << ' '
           << o.collectResourcePath << ' ' << o.treeResourcePath << ' '
           << o.corpseResourcePath << ' ' << o.monsterDisplayName << ' '
           << o.monsterPrefabPath << ' ' << o.npcModelPath << ' '
           << o.buildingName << ' ' << o.elevatorTemplateId << ' '
           << o.elevatorModelPath << ' ' << o.elevatorPartId << ' '
           << o.elevatorPartModelPath << ' ' << o.caveLiftId << ' '
           << o.caveLiftModelPath << ' '
           << o.mgrMyPlayerLocal << ' ' << o.mgrMyPlayerTpGo << ' ' << o.mgrMyPlayerBoneMgr << ' '
           << o.playerBoneCamera << ' '
           << o.mgrEntityGoGos << ' ' << o.basePlayerGoEntity << ' '
           << o.mgrCameraController << ' ' << o.cameraNowState << ' '
           << o.cameraYaw << ' ' << o.cameraPitch << ' ' << o.cameraRoll << ' '
           << o.cameraCurrentFov << ' ' << o.baseCameraTransform << ' '
           << o.clientPlayerHead << ' ' << o.clientPlayerBody << ' ' << o.clientPlayerSpine << ' '
           << o.clientPlayerLeftFoot << ' ' << o.clientPlayerRightFoot << ' '
           << o.clientPlayerNeck << ' ' << o.unityObjectCachedPtr << '\n';
    output.close();
    if (!output || !MoveFileExW(temporary.c_str(), cachePath.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
    }
}

} // namespace SKJH_SdkDetail

inline bool LoadSdkManifest() {
    SKJH_RuntimeOffsets loaded;
    SKJH_SdkDetail::FindSdkPair(loaded.scriptPath, loaded.dumpPath);
    const std::filesystem::path cachePath = !loaded.scriptPath.empty()
        ? loaded.scriptPath.parent_path() / L"skjh_sdk.cache" : std::filesystem::path{};
    if (!cachePath.empty() && !loaded.dumpPath.empty() &&
        SKJH_SdkDetail::ReadCache(cachePath, loaded.scriptPath, loaded.dumpPath, loaded) &&
        SKJH_SdkDetail::ValidateManifestShape(loaded)) {
        g_RuntimeOffsets = loaded;
        return true;
    }
    if (!loaded.scriptPath.empty())
        loaded.scriptLoaded = SKJH_SdkDetail::ParseScriptJson(loaded.scriptPath, loaded);
    if (!loaded.dumpPath.empty())
        loaded.dumpLoaded = SKJH_SdkDetail::ParseDumpCs(loaded.dumpPath, loaded);
    const bool valid = loaded.scriptLoaded && loaded.dumpLoaded &&
        SKJH_SdkDetail::ValidateManifestShape(loaded);
    if (valid) {
        if (!cachePath.empty())
            SKJH_SdkDetail::WriteCache(
                cachePath, loaded.scriptPath, loaded.dumpPath, loaded);
        g_RuntimeOffsets = loaded;
        return true;
    }

    SKJH_RuntimeOffsets fallback;
    fallback.scriptPath = loaded.scriptPath;
    fallback.dumpPath = loaded.dumpPath;
    g_RuntimeOffsets = std::move(fallback);
    return SKJH_SdkDetail::ValidateManifestShape(g_RuntimeOffsets);
}

inline std::string SdkManifestSummary() {
    std::ostringstream out;
    out << "SDK script=" << (g_RuntimeOffsets.scriptLoaded ? "ok" : "fallback")
        << " dump=" << (g_RuntimeOffsets.dumpLoaded ? "ok" : "fallback")
        << " EntityManager=0x" << std::hex << std::uppercase
        << g_RuntimeOffsets.entityManagerTypeInfo
        << " Mc=0x" << g_RuntimeOffsets.mcTypeInfo
        << " McCommon=0x" << g_RuntimeOffsets.mcCommonTypeInfo
        << " LanguageManager=0x" << g_RuntimeOffsets.languageManagerTypeInfo;
    return out.str();
}

inline bool SdkManifestIsSane() {
    return SKJH_SdkDetail::ValidateManifestShape(g_RuntimeOffsets);
}
