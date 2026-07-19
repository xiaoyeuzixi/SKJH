#pragma once

#include "Mem.h"
#include "Offset.h"
#include "ItemMap.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum class SKJH_TemplateTable : size_t {
    Item,
    Treasure,
    Ore,
    Collectable,
    Tree,
    Vehicle,
    Corpse,
    Monster,
    Npc,
    Building,
    Elevator,
    ElevatorPart,
    CaveLift,
    Count,
};

struct SKJH_TemplateCatalogStats {
    DWORD64 manager = 0;
    size_t tablesLoaded = 0;
    size_t namesLoaded = 0;
    size_t localizationEntries = 0;
    size_t localizedNames = 0;
    std::array<size_t, static_cast<size_t>(SKJH_TemplateTable::Count)> namesPerTable{};
    std::array<size_t, static_cast<size_t>(SKJH_TemplateTable::Count)> rowsPerTable{};
    bool localizationLoaded = false;
    bool complete = false;
};

inline std::shared_mutex g_SKJH_TemplateMutex;
inline std::mutex g_SKJH_TemplateRefreshMutex;
inline std::array<std::unordered_map<int64_t, std::string>,
                  static_cast<size_t>(SKJH_TemplateTable::Count)>
    g_SKJH_TemplateNames;
inline SKJH_TemplateCatalogStats g_SKJH_TemplateStats;
inline std::chrono::steady_clock::time_point g_SKJH_TemplateLastAttempt{};

inline DWORD64 SKJH_GetTemplateTablesManager() {
    const DWORD64 gameAssembly = mem.GetBase("GameAssembly.dll");
    if (!gameAssembly) return 0;
    const DWORD64 klass = mem.Read<DWORD64>(
        gameAssembly + g_RuntimeOffsets.mcCommonTypeInfo);
    if (!Mem::IsUserAddress(klass)) return 0;
    const DWORD64 staticFields = mem.Read<DWORD64>(
        klass + g_RuntimeOffsets.il2cppClassStaticFields);
    if (!Mem::IsUserAddress(staticFields)) return 0;
    const DWORD64 manager = mem.Read<DWORD64>(
        staticFields + g_RuntimeOffsets.mcCommonTables);
    return Mem::IsUserAddress(manager) ? manager : 0;
}

inline DWORD64 SKJH_GetLanguageDataMap() {
    const DWORD64 gameAssembly = mem.GetBase("GameAssembly.dll");
    if (!gameAssembly) return 0;
    const DWORD64 klass = mem.Read<DWORD64>(
        gameAssembly + g_RuntimeOffsets.languageManagerTypeInfo);
    if (!Mem::IsUserAddress(klass)) return 0;
    const DWORD64 staticFields = mem.Read<DWORD64>(
        klass + g_RuntimeOffsets.il2cppClassStaticFields);
    if (!Mem::IsUserAddress(staticFields)) return 0;
    const DWORD64 instance = mem.Read<DWORD64>(
        staticFields + g_RuntimeOffsets.languageManagerInstance);
    if (!Mem::IsUserAddress(instance)) return 0;
    const DWORD64 dataMap = mem.Read<DWORD64>(
        instance + g_RuntimeOffsets.languageManagerDataMap);
    return Mem::IsUserAddress(dataMap) ? dataMap : 0;
}

inline bool SKJH_ReadManagedUtf8(DWORD64 stringObject, std::string& result,
                                 int32_t maximumLength = 512,
                                 bool* readFailed = nullptr) {
    result.clear();
    if (readFailed) *readFailed = false;
    if (!Mem::IsUserAddress(stringObject)) return false;
    int32_t length = 0;
    if (!mem.Read(stringObject + 0x10, &length, sizeof(length))) {
        if (readFailed) *readFailed = true;
        return false;
    }
    if (length <= 0 || length > maximumLength) return false;
    std::vector<wchar_t> characters(static_cast<size_t>(length));
    if (!mem.Read(stringObject + 0x14, characters.data(),
                  static_cast<DWORD>(characters.size() * sizeof(wchar_t)))) {
        if (readFailed) *readFailed = true;
        return false;
    }
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                    characters.data(), length, nullptr, 0,
                                    nullptr, nullptr);
    if (bytes <= 0) {
        bytes = WideCharToMultiByte(CP_UTF8, 0, characters.data(), length,
                                    nullptr, 0, nullptr, nullptr);
    }
    if (bytes <= 0) return false;
    result.resize(static_cast<size_t>(bytes));
    return WideCharToMultiByte(CP_UTF8, 0, characters.data(), length,
                               result.data(), bytes, nullptr, nullptr) == bytes;
}

inline std::string SKJH_NormalizeTemplateName(std::string value) {
    auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    std::replace(value.begin(), value.end(), '\\', '/');
    const size_t slash = value.find_last_of('/');
    if (slash != std::string::npos && slash + 1 < value.size())
        value.erase(0, slash + 1);

    const size_t dot = value.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        std::string extension = value.substr(dot);
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (extension == ".prefab" || extension == ".asset" ||
            extension == ".fbx" || extension == ".controller") {
            value.erase(dot);
        }
    }
    if (value.size() > 96) value.resize(96);
    return value;
}

inline std::string SKJH_NormalizeLocalizedName(std::string value) {
    const size_t nul = value.find('\0');
    if (nul != std::string::npos) value.resize(nul);
    for (char& ch : value) {
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
    }
    auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    if (value.size() > 192) {
        size_t end = 192;
        while (end > 0 &&
               (static_cast<unsigned char>(value[end]) & 0xC0u) == 0x80u) {
            --end;
        }
        value.resize(end);
    }
    return value;
}

struct SKJH_LocalizationEntryRaw {
    int32_t hashCode = -1;
    int32_t next = -1;
    int32_t key = 0;
    int32_t padding = 0;
    DWORD64 stringObject = 0;
};
static_assert(sizeof(SKJH_LocalizationEntryRaw) == 0x18,
              "Unexpected IL2CPP Dictionary<int,string> entry layout");

using SKJH_LocalizationPointers = std::unordered_map<int32_t, DWORD64>;

inline bool SKJH_LoadLocalizationPointers(SKJH_LocalizationPointers& output) {
    output.clear();
    const DWORD64 dataMap = SKJH_GetLanguageDataMap();
    if (!dataMap) return false;

    DWORD64 entriesArray = 0;
    if (!mem.Read(dataMap + 0x18, &entriesArray, sizeof(entriesArray)) ||
        !Mem::IsUserAddress(entriesArray)) {
        return false;
    }
    int32_t arrayLength = 0;
    int32_t count = 0;
    if (!mem.Read(entriesArray + 0x18, &arrayLength, sizeof(arrayLength)) ||
        !mem.Read(dataMap + 0x20, &count, sizeof(count)) ||
        arrayLength <= 0 || arrayLength > 500000 ||
        count <= 0 || count > arrayLength) {
        return false;
    }

    std::vector<SKJH_LocalizationEntryRaw> entries(static_cast<size_t>(count));
    const size_t byteCount = entries.size() * sizeof(entries.front());
    if (byteCount > MAXDWORD ||
        !mem.Read(entriesArray + 0x20, entries.data(),
                  static_cast<DWORD>(byteCount))) {
        return false;
    }

    output.reserve(static_cast<size_t>(count));
    for (const auto& entry : entries) {
        if (entry.hashCode < 0 || entry.key <= 0 ||
            !Mem::IsUserAddress(entry.stringObject)) {
            continue;
        }
        output.insert_or_assign(entry.key, entry.stringObject);
    }
    return !output.empty();
}

inline bool SKJH_ReadLocalizedTemplateName(
    DWORD64 bean, uint32_t indexOffset,
    const SKJH_LocalizationPointers& localization,
    std::string& name, bool* readFailed = nullptr) {
    name.clear();
    if (readFailed) *readFailed = false;
    if (!indexOffset || localization.empty()) return false;

    int32_t localizationIndex = 0;
    if (!mem.Read(bean + indexOffset, &localizationIndex,
                  sizeof(localizationIndex))) {
        if (readFailed) *readFailed = true;
        return false;
    }
    if (localizationIndex <= 0) return false;
    const auto found = localization.find(localizationIndex);
    if (found == localization.end()) return false;

    bool stringReadFailed = false;
    if (!SKJH_ReadManagedUtf8(found->second, name, 256, &stringReadFailed)) {
        if (readFailed) *readFailed = stringReadFailed;
        return false;
    }
    name = SKJH_NormalizeLocalizedName(std::move(name));
    return !name.empty();
}

struct SKJH_TemplateTableSpec {
    SKJH_TemplateTable kind;
    uint32_t managerOffset;
    uint32_t idOffset;
    uint32_t localizedNameOffset;
    std::array<uint32_t, 3> nameOffsets;
};

inline bool SKJH_LoadTemplateTable(
    DWORD64 manager, const SKJH_TemplateTableSpec& spec,
    const SKJH_LocalizationPointers& localization,
    std::unordered_map<int64_t, std::string>& output,
    size_t* rowCount = nullptr, size_t* localizedCount = nullptr) {
    output.clear();
    if (rowCount) *rowCount = 0;
    if (localizedCount) *localizedCount = 0;
    DWORD64 table = 0;
    if (!mem.Read(manager + spec.managerOffset, &table, sizeof(table))) return false;
    if (!Mem::IsUserAddress(table)) return false;
    DWORD64 dataList = 0;
    if (!mem.Read(table + g_RuntimeOffsets.tableDataList,
                  &dataList, sizeof(dataList))) return false;
    if (!Mem::IsUserAddress(dataList)) return false;
    int32_t length = 0;
    if (!mem.Read(dataList + 0x18, &length, sizeof(length))) return false;
    if (length <= 0 || length > 100000) return false;
    if (rowCount) *rowCount = static_cast<size_t>(length);

    std::vector<DWORD64> beans(static_cast<size_t>(length));
    const bool bulkRead = mem.Read(
        dataList + 0x20, beans.data(),
        static_cast<DWORD>(beans.size() * sizeof(DWORD64)));
    output.reserve(static_cast<size_t>(length));
    bool completeRead = true;
    for (int32_t index = 0; index < length; ++index) {
        DWORD64 bean = bulkRead ? beans[static_cast<size_t>(index)] : 0;
        if (!bulkRead &&
            !mem.Read(dataList + 0x20 + static_cast<DWORD64>(index) * sizeof(DWORD64),
                      &bean, sizeof(bean))) {
            completeRead = false;
            continue;
        }
        if (!Mem::IsUserAddress(bean)) {
            completeRead = false;
            continue;
        }
        int64_t id = 0;
        if (!mem.Read(bean + spec.idOffset, &id, sizeof(id))) {
            completeRead = false;
            continue;
        }
        if (!id) {
            completeRead = false;
            continue;
        }

        std::string name;
        bool localizedReadFailed = false;
        if (SKJH_ReadLocalizedTemplateName(
                bean, spec.localizedNameOffset, localization, name,
                &localizedReadFailed)) {
            if (localizedCount) ++*localizedCount;
        } else {
            if (localizedReadFailed) completeRead = false;
            for (const uint32_t offset : spec.nameOffsets) {
                if (!offset) continue;
                DWORD64 stringObject = 0;
                if (!mem.Read(bean + offset, &stringObject, sizeof(stringObject))) {
                    completeRead = false;
                    continue;
                }
                bool stringReadFailed = false;
                if (SKJH_ReadManagedUtf8(
                        stringObject, name, 512, &stringReadFailed)) {
                    name = SKJH_NormalizeTemplateName(std::move(name));
                    if (!name.empty()) break;
                }
                if (stringReadFailed) completeRead = false;
            }
        }
        if (name.empty() && spec.kind == SKJH_TemplateTable::Item)
            name = "Item #" + std::to_string(id);
        if (!name.empty()) output.emplace(id, std::move(name));
    }
    return completeRead;
}

inline bool SKJH_RefreshTemplateCatalog(bool force = false) {
    const std::lock_guard<std::mutex> refreshLock(g_SKJH_TemplateRefreshMutex);
    const DWORD64 manager = SKJH_GetTemplateTablesManager();
    if (!manager) return false;
    const auto now = std::chrono::steady_clock::now();
    decltype(g_SKJH_TemplateNames) previous;
    SKJH_TemplateCatalogStats previousStats;
    bool sameManager = false;
    {
        std::shared_lock<std::shared_mutex> lock(g_SKJH_TemplateMutex);
        const auto elapsed = now - g_SKJH_TemplateLastAttempt;
        if (!force && manager == g_SKJH_TemplateStats.manager &&
            (g_SKJH_TemplateStats.complete || elapsed < std::chrono::seconds(2))) {
            return g_SKJH_TemplateStats.tablesLoaded != 0;
        }
        sameManager = manager == g_SKJH_TemplateStats.manager;
        if (sameManager) {
            previous = g_SKJH_TemplateNames;
            previousStats = g_SKJH_TemplateStats;
        }
    }

    SKJH_LocalizationPointers localization;
    const bool localizationLoaded =
        SKJH_LoadLocalizationPointers(localization);
    const bool preservePreviousLocalization =
        !localizationLoaded && sameManager && previousStats.localizationLoaded;

    const std::array<SKJH_TemplateTableSpec,
                     static_cast<size_t>(SKJH_TemplateTable::Count)> specs = {{
        {SKJH_TemplateTable::Item, g_RuntimeOffsets.tablesItemConfig,
         g_RuntimeOffsets.templateBeanId,
         g_RuntimeOffsets.itemNameL10nIndex,
         {g_RuntimeOffsets.itemConstName, g_RuntimeOffsets.itemDropModelPath,
          g_RuntimeOffsets.itemIconPath}},
        {SKJH_TemplateTable::Treasure, g_RuntimeOffsets.tablesTreasureBox,
         g_RuntimeOffsets.templateBeanId,
         g_RuntimeOffsets.treasureNameL10nIndex,
         {g_RuntimeOffsets.treasurePrefab, g_RuntimeOffsets.treasureConstName}},
        {SKJH_TemplateTable::Ore, g_RuntimeOffsets.tablesGatherOre,
         g_RuntimeOffsets.templateBeanId,
         g_RuntimeOffsets.oreNameL10nIndex,
         {g_RuntimeOffsets.oreResourcePath, g_RuntimeOffsets.oreGatherType}},
        {SKJH_TemplateTable::Collectable, g_RuntimeOffsets.tablesGatherCollect,
         g_RuntimeOffsets.templateBeanId,
         g_RuntimeOffsets.collectNameL10nIndex,
         {g_RuntimeOffsets.collectResourcePath, g_RuntimeOffsets.collectGatherType}},
        {SKJH_TemplateTable::Tree, g_RuntimeOffsets.tablesGatherTree,
         g_RuntimeOffsets.templateBeanId, g_RuntimeOffsets.treeNameL10nIndex,
         {g_RuntimeOffsets.treeResourcePath, 0}},
        {SKJH_TemplateTable::Vehicle, g_RuntimeOffsets.tablesVehicleInfo,
         g_RuntimeOffsets.templateBeanId,
         g_RuntimeOffsets.vehicleNameL10nIndex,
         {g_RuntimeOffsets.vehicleConstName, g_RuntimeOffsets.vehiclePrefabPath}},
        {SKJH_TemplateTable::Corpse, g_RuntimeOffsets.tablesCorpse,
         g_RuntimeOffsets.templateBeanId, g_RuntimeOffsets.corpseNameL10nIndex,
         {g_RuntimeOffsets.corpseResourcePath, 0}},
        {SKJH_TemplateTable::Monster, g_RuntimeOffsets.tablesMonster,
         g_RuntimeOffsets.templateBeanId,
         g_RuntimeOffsets.monsterNameL10nIndex,
         {g_RuntimeOffsets.monsterDisplayName, g_RuntimeOffsets.monsterPrefabPath}},
        {SKJH_TemplateTable::Npc, g_RuntimeOffsets.tablesNpc,
         g_RuntimeOffsets.templateBeanId, g_RuntimeOffsets.npcNameL10nIndex,
         {g_RuntimeOffsets.npcModelPath, 0}},
        {SKJH_TemplateTable::Building, g_RuntimeOffsets.tablesBuildingCore,
         g_RuntimeOffsets.templateBuildingId, g_RuntimeOffsets.buildingTextL10nIndex,
         {g_RuntimeOffsets.buildingName, 0}},
        {SKJH_TemplateTable::Elevator, g_RuntimeOffsets.tablesElevator,
         g_RuntimeOffsets.elevatorTemplateId, g_RuntimeOffsets.elevatorNameL10nIndex,
         {g_RuntimeOffsets.elevatorModelPath, 0}},
        {SKJH_TemplateTable::ElevatorPart, g_RuntimeOffsets.tablesElevatorPart,
         g_RuntimeOffsets.elevatorPartId, 0,
         {g_RuntimeOffsets.elevatorPartModelPath, 0}},
        {SKJH_TemplateTable::CaveLift, g_RuntimeOffsets.tablesCaveLift,
         g_RuntimeOffsets.caveLiftId, g_RuntimeOffsets.caveLiftNameL10nIndex,
         {g_RuntimeOffsets.caveLiftModelPath, 0}},
    }};

    decltype(g_SKJH_TemplateNames) loaded;
    SKJH_TemplateCatalogStats stats;
    stats.manager = manager;
    stats.localizationLoaded =
        localizationLoaded || preservePreviousLocalization;
    stats.localizationEntries = localizationLoaded
        ? localization.size() : previousStats.localizationEntries;
    if (preservePreviousLocalization)
        stats.localizedNames = previousStats.localizedNames;
    for (const auto& spec : specs) {
        auto& names = loaded[static_cast<size_t>(spec.kind)];
        size_t rowCount = 0;
        size_t localizedCount = 0;
        const bool tableComplete = SKJH_LoadTemplateTable(
            manager, spec, localization, names, &rowCount, &localizedCount);
        stats.rowsPerTable[static_cast<size_t>(spec.kind)] = rowCount;
        if (tableComplete) {
            ++stats.tablesLoaded;
        }
        if (preservePreviousLocalization) {
            const auto& oldNames = previous[static_cast<size_t>(spec.kind)];
            if (!oldNames.empty()) names = oldNames;
        } else if (!tableComplete && sameManager) {
            const auto& oldNames = previous[static_cast<size_t>(spec.kind)];
            if (!oldNames.empty()) names = oldNames;
        }
        stats.localizedNames += localizedCount;
        if (tableComplete && spec.kind == SKJH_TemplateTable::Treasure) {
            std::unordered_map<std::string, size_t> duplicates;
            for (const auto& entry : names) ++duplicates[entry.second];
            for (auto& entry : names) {
                if (duplicates[entry.second] > 1)
                    entry.second += " #" + std::to_string(entry.first);
            }
        }
        stats.namesPerTable[static_cast<size_t>(spec.kind)] = names.size();
        stats.namesLoaded += names.size();
    }
    stats.complete =
        stats.tablesLoaded == specs.size() && stats.localizationLoaded;
    {
        std::lock_guard<std::shared_mutex> lock(g_SKJH_TemplateMutex);
        g_SKJH_TemplateNames.swap(loaded);
        g_SKJH_TemplateStats = stats;
        g_SKJH_TemplateLastAttempt = std::chrono::steady_clock::now();
    }
    return stats.tablesLoaded != 0;
}

inline SKJH_TemplateCatalogStats SKJH_GetTemplateCatalogStats() {
    std::shared_lock<std::shared_mutex> lock(g_SKJH_TemplateMutex);
    return g_SKJH_TemplateStats;
}

inline std::string SKJH_LookupTemplateName(SKJH_TemplateTable table, int64_t id) {
    if (!id) return {};
    std::shared_lock<std::shared_mutex> lock(g_SKJH_TemplateMutex);
    const auto& names = g_SKJH_TemplateNames[static_cast<size_t>(table)];
    const auto found = names.find(id);
    return found == names.end() ? std::string{} : found->second;
}

inline const char* SKJH_GetTemplateTableName(SKJH_TemplateTable table) {
    switch (table) {
        case SKJH_TemplateTable::Item:        return "ItemConfig";
        case SKJH_TemplateTable::Treasure:    return "TreasureBox";
        case SKJH_TemplateTable::Ore:         return "GatherResourcesOre";
        case SKJH_TemplateTable::Collectable: return "GatherResourcesCollectable";
        case SKJH_TemplateTable::Tree:        return "GatherResourcesTree";
        case SKJH_TemplateTable::Vehicle:     return "VehicleInfo";
        case SKJH_TemplateTable::Corpse:      return "Corpse";
        case SKJH_TemplateTable::Monster:     return "Monster";
        case SKJH_TemplateTable::Npc:         return "NPC";
        case SKJH_TemplateTable::Building:    return "BuildingCore";
        case SKJH_TemplateTable::Elevator:    return "ElevatorInteractive";
        case SKJH_TemplateTable::ElevatorPart:return "ElevatorInteractiveComponent";
        case SKJH_TemplateTable::CaveLift:    return "CaveLiftConfig";
        default:                              return "";
    }
}

inline std::string SKJH_FindTemplateNameAny(int64_t id,
                                            SKJH_TemplateTable* matchedTable = nullptr) {
    if (matchedTable) *matchedTable = SKJH_TemplateTable::Count;
    if (!id) return {};
    std::shared_lock<std::shared_mutex> lock(g_SKJH_TemplateMutex);
    for (size_t index = 0; index < g_SKJH_TemplateNames.size(); ++index) {
        const auto found = g_SKJH_TemplateNames[index].find(id);
        if (found == g_SKJH_TemplateNames[index].end()) continue;
        if (matchedTable) *matchedTable = static_cast<SKJH_TemplateTable>(index);
        return found->second;
    }
    return {};
}

inline std::string SKJH_GetTemplateDisplayName(int type, int64_t templateId,
                                               const std::string& className) {
    if (!templateId) return {};
    std::string name;
    switch (type) {
        case SKJH_BOX:
            name = SKJH_LookupTemplateName(SKJH_TemplateTable::Treasure, templateId);
            if (name.empty())
                name = SKJH_LookupTemplateName(SKJH_TemplateTable::Item, templateId);
            break;
        case SKJH_CORPSE:
            name = SKJH_LookupTemplateName(SKJH_TemplateTable::Corpse, templateId);
            break;
        case SKJH_LOOT:
            name = SKJH_LookupTemplateName(SKJH_TemplateTable::Item, templateId);
            break;
        case SKJH_ORE:
            name = SKJH_LookupTemplateName(SKJH_TemplateTable::Ore, templateId);
            break;
        case SKJH_COLLECT:
            name = SKJH_LookupTemplateName(SKJH_TemplateTable::Collectable, templateId);
            break;
        case SKJH_TREE:
            name = SKJH_LookupTemplateName(SKJH_TemplateTable::Tree, templateId);
            break;
        case SKJH_VEHICLE:
            name = SKJH_LookupTemplateName(SKJH_TemplateTable::Vehicle, templateId);
            if (name.empty())
                name = SKJH_LookupTemplateName(SKJH_TemplateTable::Item, templateId);
            break;
        case SKJH_MONSTER:
            name = SKJH_LookupTemplateName(SKJH_TemplateTable::Monster, templateId);
            break;
        case SKJH_PART:
            if (className.find("CaveLift") != std::string::npos) {
                name = SKJH_LookupTemplateName(SKJH_TemplateTable::CaveLift, templateId);
            } else if (className.find("Elevator") != std::string::npos) {
                name = SKJH_LookupTemplateName(SKJH_TemplateTable::Elevator, templateId);
                if (name.empty())
                    name = SKJH_LookupTemplateName(SKJH_TemplateTable::ElevatorPart, templateId);
            }
            if (name.empty())
                name = SKJH_LookupTemplateName(SKJH_TemplateTable::Building, templateId);
            break;
        case SKJH_NPC:
            name = SKJH_LookupTemplateName(SKJH_TemplateTable::Npc, templateId);
            break;
        default:
            break;
    }
    return name;
}

struct SKJH_TemplateChoice {
    int64_t id = 0;
    std::string name;
    SKJH_TemplateTable table = SKJH_TemplateTable::Count;
};

// UI 使用短时目录快照，避免在 ImGui 搜索/排序期间持有目录共享锁。
inline std::vector<SKJH_TemplateChoice>
SKJH_GetTemplateChoicesForType(int type) {
    std::array<SKJH_TemplateTable, 5> tables{};
    size_t tableCount = 0;
    auto add = [&](SKJH_TemplateTable table) {
        tables[tableCount++] = table;
    };
    switch (type) {
        case SKJH_LOOT:
            add(SKJH_TemplateTable::Item);
            break;
        case SKJH_BOX:
            add(SKJH_TemplateTable::Treasure);
            add(SKJH_TemplateTable::Item);
            break;
        case SKJH_CORPSE:
            add(SKJH_TemplateTable::Corpse);
            break;
        case SKJH_ORE:
            add(SKJH_TemplateTable::Ore);
            break;
        case SKJH_COLLECT:
            add(SKJH_TemplateTable::Collectable);
            break;
        case SKJH_TREE:
            add(SKJH_TemplateTable::Tree);
            break;
        case SKJH_VEHICLE:
            add(SKJH_TemplateTable::Vehicle);
            add(SKJH_TemplateTable::Item);
            break;
        case SKJH_MONSTER:
            add(SKJH_TemplateTable::Monster);
            break;
        case SKJH_NPC:
            add(SKJH_TemplateTable::Npc);
            break;
        case SKJH_PART:
            add(SKJH_TemplateTable::CaveLift);
            add(SKJH_TemplateTable::Elevator);
            add(SKJH_TemplateTable::ElevatorPart);
            add(SKJH_TemplateTable::Building);
            break;
        default:
            return {};
    }

    std::unordered_map<int64_t, SKJH_TemplateChoice> merged;
    {
        std::shared_lock<std::shared_mutex> lock(g_SKJH_TemplateMutex);
        for (size_t tableIndex = 0; tableIndex < tableCount; ++tableIndex) {
            const SKJH_TemplateTable table = tables[tableIndex];
            const auto& names =
                g_SKJH_TemplateNames[static_cast<size_t>(table)];
            for (const auto& entry : names) {
                if (entry.first <= 0 || entry.second.empty()) continue;
                merged.emplace(entry.first,
                    SKJH_TemplateChoice{entry.first, entry.second, table});
            }
        }
    }

    std::vector<SKJH_TemplateChoice> result;
    result.reserve(merged.size());
    for (auto& entry : merged) result.push_back(std::move(entry.second));
    std::sort(result.begin(), result.end(),
        [](const SKJH_TemplateChoice& left,
           const SKJH_TemplateChoice& right) {
            if (left.name != right.name) return left.name < right.name;
            return left.id < right.id;
        });
    return result;
}
