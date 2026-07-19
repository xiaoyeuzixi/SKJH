#pragma once
/*
 * main.h — SKJH (失控进化) 游戏逻辑模块
 * 重新设计: 控件布局贴合生存射击游戏玩法
 * 实体类型: 玩家/怪物/部件/矿石/物资箱/尸体/领地/树木/NPC
 * 全中文界面, 所有对象名称统一中文呈现
 */
#include "Mem.h"
#include "Offset.h"
#include "GameMatrix.h"
#include "Throttler.h"
#include "ImGui/imgui.h"
#include "ESPUtils.h"
#include "BoneEnum.h"
#include "DMAKey.h"
#include "TemplateCatalog.h"
#include "PlayerIntel.h"
#include "AimDevice.h"
#include <vector>
#include <atomic>
#include <array>
#include <chrono>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <string>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")

// ===================== 语言 & 全局 =====================
inline int g_lang = 0;  // 0=中文 1=English
#define L(cn, en) (g_lang ? (en) : (cn))

// ===================== SKJH 相机数据 =====================
struct SKJH_Cam {
    FVector camLoc;
    FVector camRot;
    FVector localPos;
    FVector right;
    FVector up;
    FVector forward;
    float   camFov;
    bool    valid;
};

// 将 SKJH_Cam 转换为 SKJH_CameraData (供 SKJH_W2S 使用)
inline SKJH_CameraData ToCameraData(const SKJH_Cam& c) {
    SKJH_CameraData d;
    d.camLoc = c.camLoc;
    d.camRot = c.camRot;
    d.localPos = c.localPos;
    d.right = c.right;
    d.up = c.up;
    d.forward = c.forward;
    d.camFov = c.camFov;
    d.valid  = c.valid;
    return d;
}

// ===================== 实体 ESP 数据 =====================
struct SKJH_EntityEntry {
    DWORD64 entity;
    DWORD64 klass;
    int64_t entityId;
    int     type;       // SKJH_EntityType
    int32_t classHash;
    int64_t templateId;
    int32_t spawnType;
    std::string className;
    std::string displayName;
    FVector pos;
    float   hp;
    float   maxHp;
    float   distance;
    bool    hasBones;
    FVector bones[BONE_COUNT];
    bool    boneValid[BONE_COUNT];
    std::chrono::steady_clock::time_point boneUpdatedAt{};
    std::chrono::steady_clock::time_point entityUpdatedAt{};
    bool    isLocalPlayer;
    SKJH_PlayerIntel playerIntel;
    std::chrono::steady_clock::time_point playerIntelUpdatedAt{};
};

inline constexpr std::chrono::milliseconds SKJH_ENTITY_TTL{250};
// A full dictionary pass can miss an actor for a frame while DMA is busy.
// Keep players briefly so their fixed-position boxes do not blink.
inline constexpr std::chrono::milliseconds SKJH_PLAYER_TTL{1500};
// A full Transform pass can take longer than one entity refresh when DMA is
// busy. Keep the last complete sample long enough to avoid box-anchor flicker.
inline constexpr std::chrono::milliseconds SKJH_BONE_TTL{1500};
inline constexpr std::chrono::milliseconds SKJH_PLAYER_POSITION_TTL{750};

inline bool SKJH_AreBonesFresh(
    const SKJH_EntityEntry& entity,
    std::chrono::steady_clock::time_point now) {
    return entity.hasBones &&
        entity.boneUpdatedAt != std::chrono::steady_clock::time_point{} &&
        now - entity.boneUpdatedAt <= SKJH_BONE_TTL;
}

inline void SKJH_ClearBones(SKJH_EntityEntry& entity) {
    entity.hasBones = false;
    entity.boneUpdatedAt = {};
    for (int bone = 0; bone < BONE_COUNT; ++bone) {
        entity.bones[bone] = {};
        entity.boneValid[bone] = false;
    }
}

// ===================== 线程安全数据容器 =====================
inline std::shared_mutex g_CamMutex;
inline SKJH_Cam g_Camera;

inline std::shared_mutex g_DataMutex;
inline std::shared_ptr<std::vector<SKJH_EntityEntry>> g_Entities =
    std::make_shared<std::vector<SKJH_EntityEntry>>();

// Player positions are refreshed independently from the expensive full entity
// scan. Keeping this small map separate prevents position updates from
// cloning the entity list while the renderer is taking a snapshot.
struct SKJH_PlayerPositionSample {
    DWORD64 entity = 0;
    int32_t classHash = 0;
    FVector position{};
    std::chrono::steady_clock::time_point sampledAt{};
};
using SKJH_PlayerPositionMap =
    std::unordered_map<int64_t, SKJH_PlayerPositionSample>;
inline std::shared_mutex g_PlayerPositionMutex;
inline std::shared_ptr<const SKJH_PlayerPositionMap> g_PlayerPositions =
    std::make_shared<SKJH_PlayerPositionMap>();

inline int64_t SKJH_GetEntitySnapshotKey(int64_t entityId, DWORD64 entity) {
    // Entity IDs can transiently read as zero. User-mode entity pointers are
    // stable for the life of an actor and provide a collision-free fallback.
    return entityId != 0 ? entityId : -static_cast<int64_t>(entity);
}

inline std::atomic<DWORD64> g_LocalPlayer{0};
inline std::atomic<int64_t> g_LocalPlayerId{0};
inline std::atomic_bool g_Running{false};

// ===================== 屏幕尺寸 =====================
inline std::atomic<int> g_ScreenW{1920};
inline std::atomic<int> g_ScreenH{1080};

// ===================== ESP 开关 =====================
inline bool g_HideUI = false;
inline int  g_HotkeyVK = VK_F5;
inline int  g_ItemsHotkeyVK = VK_F9;
inline int  g_BindingHotkey = 0;

// ── 通用显示开关 ──
inline bool g_ShowBox      = true;
// Keep skeleton samples for actor anchors and target selection, without
// rendering skeleton lines in the overlay.
inline bool g_ShowSkeleton = false;
inline bool g_ShowHealth   = true;
inline bool g_ShowWeapon   = true;
// Inventory sampling stays active for diagnostics, but backpack text is
// intentionally hidden by default in the ESP overlay.
inline bool g_ShowBackpack = false;
inline bool g_ShowDistance = true;
inline bool g_ShowName     = true;
inline bool g_ShowRays     = false;   // 射线默认关, 避免视觉干扰
inline bool g_DrawSelf     = false;
inline bool g_ShowEntityId = false;   // 是否显示实体ID
inline bool g_ShowThreatWarn = true;  // 威胁警告
inline bool g_ShowItems = true;

// 每个分类可选启用模板白名单。未启用时显示整类；启用后只显示
// g_EnabledTemplateIds 中勾选的模板，无模板 ID 的同类实体也会隐藏。
inline std::array<bool, SKJH_TYPE_COUNT> g_TemplateFilterActive{};
inline std::array<std::unordered_set<int64_t>, SKJH_TYPE_COUNT>
    g_EnabledTemplateIds{};

inline bool SKJH_IsTemplateEnabled(int type, int64_t templateId) {
    if (type < 0 || type >= SKJH_TYPE_COUNT) return true;
    // Players and monsters have no loot template selection in the UI. Never
    // let a persisted item whitelist hide an actor box.
    if (type == SKJH_PLAYER || type == SKJH_MONSTER) return true;
    if (!g_TemplateFilterActive[type]) return true;
    if (templateId <= 0) return false;
    return
        g_EnabledTemplateIds[type].find(templateId) !=
            g_EnabledTemplateIds[type].end();
}

enum class SKJH_AimPriority : int {
    Crosshair = 0,
    WorldDistance = 1,
    LowHealth = 2,
};

enum class SKJH_AimActivation : int {
    Hold = 0,
    Toggle = 1,
    Continuous = 2,
};

enum class SKJH_AimFovShape : int {
    Circle = 0,
    Rectangle = 1,
};

struct SKJH_AimConfig {
    bool enabled = false;
    bool showFov = true;
    bool showTargetLine = true;
    bool showTargetMarker = true;
    bool boneFallback = true;
    bool lockTarget = true;
    SKJH_AimPriority priority = SKJH_AimPriority::Crosshair;
    SKJH_AimActivation activation = SKJH_AimActivation::Hold;
    SKJH_AimFovShape fovShape = SKJH_AimFovShape::Circle;
    int activationKey = VK_RBUTTON;
    int preferredBone = BONE_HEAD;
    float fovPixels = 180.0f;
    float maxDistance = 250.0f;
    float smoothX = 6.0f;
    float smoothY = 6.0f;
    float deadZone = 1.0f;
    int maximumStep = 48;
    int targetHoldMs = 220;
    SKJH::Aim::AimDeviceConfig device{};
};

struct SKJH_AimState {
    bool active = false;
    bool outputReady = false;
    bool targetValid = false;
    bool usedBoneFallback = false;
    int64_t targetEntityId = 0;
    DWORD64 targetEntity = 0;
    int32_t targetClassHash = 0;
    int targetBone = -1;
    FVector targetWorld{};
    FVector2D targetScreen{};
    float screenDistance = 0.0f;
    float worldDistance = 0.0f;
    std::chrono::steady_clock::time_point sampledAt{};
};

inline std::mutex g_AimConfigMutex;
inline SKJH_AimConfig g_AimConfig;
inline std::shared_mutex g_AimStateMutex;
inline SKJH_AimState g_AimState;
inline std::mutex g_AimDeviceMutex;
// Serialise connection transitions so the UI and the aim worker cannot
// replace the configured device at the same time.
inline std::mutex g_AimDeviceConnectMutex;
inline std::shared_ptr<SKJH::Aim::IAimDevice> g_AimDevice;
inline std::atomic_bool g_AimDeviceReconnectRequested{false};
inline std::atomic_bool g_AimActivationDown{false};
inline std::atomic_bool g_AimToggleActive{false};

// ── 按类型过滤 (每种类型可独立开关) ──
inline bool g_TypeEnabled[SKJH_TYPE_COUNT] = {
    true,  // UNKNOWN — 未知实体默认显示
    true,  // PLAYER
    true,  // MONSTER
    true,  // PART
    true,  // ORE
    true,  // BOX
    false, // TERRITORY — 领地默认不显示
    false, // TREE — 树木默认不显示
    true,  // VEHICLE
    false, // NPC
    false, // SYSTEM
    true,  // LOOT
    true,  // COLLECT
    true,  // CORPSE
};

// ── 按类型最大距离 ──
inline int g_TypeMaxDist[SKJH_TYPE_COUNT] = {
    200,   // UNKNOWN
    500,   // PLAYER
    300,   // MONSTER
    200,   // PART
    150,   // ORE
    200,   // BOX
    300,   // TERRITORY
    100,   // TREE
    400,   // VEHICLE
    200,   // NPC
    50,    // SYSTEM
    200,   // LOOT
    150,   // COLLECT
    200,   // CORPSE
};

// ── 全局最大距离 (额外限制) ──
inline int  g_MaxDist = 800;

inline bool g_FpsLimitEnabled = false;
inline int  g_FpsLimit = 60;

inline int SleepESP()    { return 35; }
inline int SleepCamera() { return 8; }
inline int SleepBones()  { return 16; }
inline int SleepPlayerIntel() { return 80; }
inline int SleepPlayerPositions() { return 25; }

// ===================== 统计数据 =====================
inline int g_TypeCount[SKJH_TYPE_COUNT] = {};
inline int g_VisibleCount = 0;
inline int g_ThreatLevel  = 0;  // 0=安全 1=注意 2=危险 3=极度危险

// ===================== 虚拟键名查询 =====================
inline const char* VKName(int vk) {
    if (vk >= VK_F1 && vk <= VK_F12) { static char b[4]; snprintf(b,4,"F%d",vk-VK_F1+1); return b; }
    switch (vk) {
        case VK_LBUTTON: return u8"鼠标左键"; case VK_RBUTTON: return u8"鼠标右键";
        case VK_MBUTTON: return u8"鼠标中键"; case VK_CONTROL: return "Ctrl";
        case VK_MENU: return "Alt"; case VK_SHIFT: return "Shift";
        case VK_TAB: return "Tab"; case VK_SPACE: return u8"空格";
        default: {
            static char k[2]; k[0]=(char)MapVirtualKeyA(vk,MAPVK_VK_TO_CHAR); k[1]=0;
            return (k[0]>='A'&&k[0]<='Z')||(k[0]>='0'&&k[0]<='9') ? k : "...";
        }
    }
}

// ===================== 配置保存/加载 =====================
// 正式配置使用当前用户注册表，程序目录不生成外置 cfg 数据文件。
inline constexpr const char* SKJH_CONFIG_REGISTRY_PATH =
    R"(Software\SKJH\DMAConsole)";

inline bool SKJH_RegWriteDword(HKEY key, const char* name, DWORD value) {
    return RegSetValueExA(key, name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
}

inline bool SKJH_RegReadDword(HKEY key, const char* name, DWORD& value) {
    DWORD type = 0;
    DWORD size = sizeof(value);
    return RegQueryValueExA(key, name, nullptr, &type,
        reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
        type == REG_DWORD && size == sizeof(value);
}

inline bool SKJH_RegWriteString(
    HKEY key, const char* name, const std::string& value) {
    if (value.size() > 4096) return false;
    return RegSetValueExA(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>(value.size() + 1)) == ERROR_SUCCESS;
}

inline bool SKJH_RegReadString(
    HKEY key, const char* name, std::string& value, DWORD maximum = 4096) {
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExA(key, name, nullptr, &type, nullptr, &size) !=
            ERROR_SUCCESS ||
        type != REG_SZ || size == 0 || size > maximum) {
        return false;
    }
    std::vector<char> buffer(size + 1, 0);
    if (RegQueryValueExA(key, name, nullptr, &type,
            reinterpret_cast<BYTE*>(buffer.data()), &size) != ERROR_SUCCESS) {
        return false;
    }
    buffer.back() = 0;
    value.assign(buffer.data());
    return true;
}

inline bool SaveGlobalConfig() {
    HKEY key = nullptr;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, SKJH_CONFIG_REGISTRY_PATH, 0,
            nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
            &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    bool ok = true;
    const auto setBool = [&](const char* name, bool value) {
        ok = SKJH_RegWriteDword(key, name, value ? 1u : 0u) && ok;
    };
    const auto setInt = [&](const char* name, int value) {
        ok = SKJH_RegWriteDword(key, name, static_cast<DWORD>(value)) && ok;
    };
    ok = SKJH_RegWriteDword(key, "ConfigVersion", 4) && ok;
    setInt("DisplayMode", g_DisplayMode);
    setInt("MaxDist", g_MaxDist);
    setInt("HotkeyVK", g_HotkeyVK);
    setInt("ItemsHotkeyVK", g_ItemsHotkeyVK);
    setBool("ShowBox", g_ShowBox);
    setBool("ShowSkeleton", false);
    setBool("ShowHealth", g_ShowHealth);
    setBool("ShowWeapon", g_ShowWeapon);
    setBool("ShowBackpack", g_ShowBackpack);
    setBool("ShowDistance", g_ShowDistance);
    setBool("ShowName", g_ShowName);
    setBool("ShowRays", g_ShowRays);
    setBool("DrawSelf", g_DrawSelf);
    setBool("ShowEntityId", g_ShowEntityId);
    setBool("ShowThreatWarn", g_ShowThreatWarn);
    setBool("ShowItems", g_ShowItems);
    setBool("FpsLimitEnabled", g_FpsLimitEnabled);
    setInt("FpsLimit", g_FpsLimit);

    for (int type = 0; type < SKJH_TYPE_COUNT; ++type) {
        char name[64];
        snprintf(name, sizeof(name), "TypeEnabled_%d", type);
        setBool(name, g_TypeEnabled[type]);
        snprintf(name, sizeof(name), "TypeMaxDist_%d", type);
        setInt(name, g_TypeMaxDist[type]);
        snprintf(name, sizeof(name), "TemplateFilterActive_%d", type);
        setBool(name, g_TemplateFilterActive[type]);

        std::vector<int64_t> ids(
            g_EnabledTemplateIds[type].begin(),
            g_EnabledTemplateIds[type].end());
        std::sort(ids.begin(), ids.end());
        snprintf(name, sizeof(name), "TemplateEnabled_%d", type);
        if (ids.empty()) {
            const LSTATUS deleted = RegDeleteValueA(key, name);
            ok = (deleted == ERROR_SUCCESS || deleted == ERROR_FILE_NOT_FOUND) && ok;
        } else {
            ok = RegSetValueExA(key, name, 0, REG_BINARY,
                reinterpret_cast<const BYTE*>(ids.data()),
                static_cast<DWORD>(ids.size() * sizeof(int64_t))) ==
                ERROR_SUCCESS && ok;
        }
    }

    SKJH_AimConfig aim;
    {
        std::lock_guard<std::mutex> lock(g_AimConfigMutex);
        aim = g_AimConfig;
    }
    setBool("AimEnabled", aim.enabled);
    setBool("AimShowFov", aim.showFov);
    setBool("AimShowTargetLine", aim.showTargetLine);
    setBool("AimShowTargetMarker", aim.showTargetMarker);
    setBool("AimBoneFallback", aim.boneFallback);
    setBool("AimLockTarget", aim.lockTarget);
    setInt("AimPriority", static_cast<int>(aim.priority));
    setInt("AimActivation", static_cast<int>(aim.activation));
    setInt("AimFovShape", static_cast<int>(aim.fovShape));
    setInt("AimActivationKey", aim.activationKey);
    setInt("AimPreferredBone", aim.preferredBone);
    setInt("AimFovPixels100", static_cast<int>(aim.fovPixels * 100.0f));
    setInt("AimMaxDistance100", static_cast<int>(aim.maxDistance * 100.0f));
    setInt("AimSmoothX100", static_cast<int>(aim.smoothX * 100.0f));
    setInt("AimSmoothY100", static_cast<int>(aim.smoothY * 100.0f));
    setInt("AimDeadZone100", static_cast<int>(aim.deadZone * 100.0f));
    setInt("AimMaximumStep", aim.maximumStep);
    setInt("AimTargetHoldMs", aim.targetHoldMs);
    setInt("AimDeviceKind", static_cast<int>(aim.device.kind));
    setInt("AimDeviceBaud", static_cast<int>(aim.device.baudRate));
    setInt("AimDeviceTimeout", static_cast<int>(aim.device.writeTimeoutMs));
    setInt("AimDeviceReceiveTimeout",
        static_cast<int>(aim.device.receiveTimeoutMs));
    setInt("AimDeviceNetworkPort",
        static_cast<int>(aim.device.networkPort));
    setBool("AimDeviceDtr", aim.device.enableDtr);
    setBool("AimDeviceRts", aim.device.enableRts);
    setInt("AimDeviceRawHidVid", aim.device.rawHidVid);
    setInt("AimDeviceRawHidPid", aim.device.rawHidPid);
    setInt("AimDeviceRawHidUsagePage", aim.device.rawHidUsagePage);
    setInt("AimDeviceRawHidUsage", aim.device.rawHidUsage);
    setInt("AimDeviceRawHidInterface",
        static_cast<int>(aim.device.rawHidInterfaceIndex));
    setInt("AimDeviceRawHidReportBytes",
        aim.device.rawHidExpectedReportBytes);
    setInt("AimDeviceRawHidReportId", aim.device.rawHidReportId);
    setInt("AimDeviceRawHidButtonsOffset",
        aim.device.rawHidButtonsOffset);
    setInt("AimDeviceRawHidXOffset", aim.device.rawHidXOffset);
    setInt("AimDeviceRawHidYOffset", aim.device.rawHidYOffset);
    setInt("AimDeviceRawHidAxisBytes", aim.device.rawHidAxisBytes);
    setInt("AimDeviceRawHidTransferMode",
        static_cast<int>(aim.device.rawHidTransferMode));
    setBool("AimDeviceRawHidLittleEndian",
        aim.device.rawHidLittleEndian);
    ok = SKJH_RegWriteString(key, "AimDevicePort",
        aim.device.serialPort) && ok;
    ok = SKJH_RegWriteString(key, "AimDeviceTemplate",
        aim.device.genericMoveTemplate) && ok;
    ok = SKJH_RegWriteString(key, "AimDeviceNetworkIp",
        aim.device.networkIp) && ok;
    ok = SKJH_RegWriteString(key, "AimDeviceNetworkUuid",
        aim.device.networkUuid) && ok;
    if (aim.device.rawHidReportTemplate.empty()) {
        const LSTATUS deleted =
            RegDeleteValueA(key, "AimDeviceRawHidTemplate");
        ok = (deleted == ERROR_SUCCESS ||
              deleted == ERROR_FILE_NOT_FOUND) && ok;
    } else {
        ok = RegSetValueExA(
            key, "AimDeviceRawHidTemplate", 0, REG_BINARY,
            aim.device.rawHidReportTemplate.data(),
            static_cast<DWORD>(
                aim.device.rawHidReportTemplate.size())) ==
            ERROR_SUCCESS && ok;
    }

    RegCloseKey(key);
    return ok;
}

inline bool LoadGlobalConfig() {
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, SKJH_CONFIG_REGISTRY_PATH, 0,
            KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    const auto getInt = [&](const char* name, int& value) {
        DWORD stored = 0;
        if (SKJH_RegReadDword(key, name, stored))
            value = static_cast<int>(stored);
    };
    const auto getBool = [&](const char* name, bool& value) {
        DWORD stored = 0;
        if (SKJH_RegReadDword(key, name, stored)) value = stored != 0;
    };
    getInt("DisplayMode", g_DisplayMode);
    g_DisplayMode = (g_DisplayMode == 0) ? 0 : 1;
    getInt("MaxDist", g_MaxDist);
    g_MaxDist = (std::clamp)(g_MaxDist, 50, 3000);
    getInt("HotkeyVK", g_HotkeyVK);
    getInt("ItemsHotkeyVK", g_ItemsHotkeyVK);
    if (g_HotkeyVK < 1 || g_HotkeyVK > 255) g_HotkeyVK = VK_F5;
    if (g_ItemsHotkeyVK < 1 || g_ItemsHotkeyVK > 255)
        g_ItemsHotkeyVK = VK_F9;
    getBool("ShowBox", g_ShowBox);
    // Do not restore legacy skeleton-line settings. Bone data remains
    // available to the actor box and target-selection paths.
    g_ShowSkeleton = false;
    getBool("ShowHealth", g_ShowHealth);
    getBool("ShowWeapon", g_ShowWeapon);
    // Do not revive the retired backpack overlay from an older registry
    // configuration. The underlying inventory reader remains available.
    g_ShowBackpack = false;
    getBool("ShowDistance", g_ShowDistance);
    getBool("ShowName", g_ShowName);
    getBool("ShowRays", g_ShowRays);
    getBool("DrawSelf", g_DrawSelf);
    getBool("ShowEntityId", g_ShowEntityId);
    getBool("ShowThreatWarn", g_ShowThreatWarn);
    getBool("ShowItems", g_ShowItems);
    getBool("FpsLimitEnabled", g_FpsLimitEnabled);
    getInt("FpsLimit", g_FpsLimit);
    g_FpsLimit = (std::clamp)(g_FpsLimit, 30, 200);

    for (int type = 0; type < SKJH_TYPE_COUNT; ++type) {
        char name[64];
        snprintf(name, sizeof(name), "TypeEnabled_%d", type);
        getBool(name, g_TypeEnabled[type]);
        snprintf(name, sizeof(name), "TypeMaxDist_%d", type);
        getInt(name, g_TypeMaxDist[type]);
        g_TypeMaxDist[type] = (std::clamp)(g_TypeMaxDist[type], 10, 2000);
        snprintf(name, sizeof(name), "TemplateFilterActive_%d", type);
        getBool(name, g_TemplateFilterActive[type]);

        g_EnabledTemplateIds[type].clear();
        snprintf(name, sizeof(name), "TemplateEnabled_%d", type);
        DWORD valueType = 0;
        DWORD size = 0;
        if (RegQueryValueExA(key, name, nullptr, &valueType, nullptr, &size) ==
                ERROR_SUCCESS &&
            valueType == REG_BINARY && size > 0 && size <= 1024 * 1024 &&
            size % sizeof(int64_t) == 0) {
            std::vector<int64_t> ids(size / sizeof(int64_t));
            if (RegQueryValueExA(key, name, nullptr, &valueType,
                    reinterpret_cast<BYTE*>(ids.data()), &size) ==
                    ERROR_SUCCESS) {
                for (const int64_t id : ids) {
                    if (id > 0) g_EnabledTemplateIds[type].insert(id);
                }
            }
        }
    }

    SKJH_AimConfig aim;
    {
        std::lock_guard<std::mutex> lock(g_AimConfigMutex);
        aim = g_AimConfig;
    }
    // Never restore an enabled aim state from a previous registry value.
    aim.enabled = false;
    getBool("AimShowFov", aim.showFov);
    getBool("AimShowTargetLine", aim.showTargetLine);
    getBool("AimShowTargetMarker", aim.showTargetMarker);
    getBool("AimBoneFallback", aim.boneFallback);
    getBool("AimLockTarget", aim.lockTarget);
    int integerValue = static_cast<int>(aim.priority);
    getInt("AimPriority", integerValue);
    aim.priority = static_cast<SKJH_AimPriority>((std::clamp)(integerValue, 0, 2));
    integerValue = static_cast<int>(aim.activation);
    getInt("AimActivation", integerValue);
    aim.activation = static_cast<SKJH_AimActivation>((std::clamp)(integerValue, 0, 2));
    integerValue = static_cast<int>(aim.fovShape);
    getInt("AimFovShape", integerValue);
    aim.fovShape = static_cast<SKJH_AimFovShape>((std::clamp)(integerValue, 0, 1));
    getInt("AimActivationKey", aim.activationKey);
    if (aim.activationKey < 1 || aim.activationKey > 255)
        aim.activationKey = VK_RBUTTON;
    getInt("AimPreferredBone", aim.preferredBone);
    aim.preferredBone = (std::clamp)(aim.preferredBone, 0, BONE_COUNT - 1);
    integerValue = static_cast<int>(aim.fovPixels * 100.0f);
    getInt("AimFovPixels100", integerValue);
    aim.fovPixels = (std::clamp)(integerValue / 100.0f, 20.0f, 800.0f);
    integerValue = static_cast<int>(aim.maxDistance * 100.0f);
    getInt("AimMaxDistance100", integerValue);
    aim.maxDistance = (std::clamp)(integerValue / 100.0f, 10.0f, 1000.0f);
    integerValue = static_cast<int>(aim.smoothX * 100.0f);
    getInt("AimSmoothX100", integerValue);
    aim.smoothX = (std::clamp)(integerValue / 100.0f, 1.0f, 30.0f);
    integerValue = static_cast<int>(aim.smoothY * 100.0f);
    getInt("AimSmoothY100", integerValue);
    aim.smoothY = (std::clamp)(integerValue / 100.0f, 1.0f, 30.0f);
    integerValue = static_cast<int>(aim.deadZone * 100.0f);
    getInt("AimDeadZone100", integerValue);
    aim.deadZone = (std::clamp)(integerValue / 100.0f, 0.0f, 20.0f);
    getInt("AimMaximumStep", aim.maximumStep);
    aim.maximumStep = (std::clamp)(aim.maximumStep, 1, 127);
    getInt("AimTargetHoldMs", aim.targetHoldMs);
    aim.targetHoldMs = (std::clamp)(aim.targetHoldMs, 0, 1500);
    integerValue = static_cast<int>(aim.device.kind);
    getInt("AimDeviceKind", integerValue);
    {
        const auto candidate =
            static_cast<SKJH::Aim::AimDeviceKind>(integerValue);
        aim.device.kind = SKJH::Aim::AimDeviceRegistry::Find(candidate)
            ? candidate : SKJH::Aim::AimDeviceKind::None;
    }
    integerValue = static_cast<int>(aim.device.baudRate);
    getInt("AimDeviceBaud", integerValue);
    aim.device.baudRate = static_cast<uint32_t>(
        (std::clamp)(integerValue, 1200, 4000000));
    integerValue = static_cast<int>(aim.device.writeTimeoutMs);
    getInt("AimDeviceTimeout", integerValue);
    aim.device.writeTimeoutMs = static_cast<uint32_t>(
        (std::clamp)(integerValue, 1, 5000));
    integerValue = static_cast<int>(aim.device.receiveTimeoutMs);
    getInt("AimDeviceReceiveTimeout", integerValue);
    aim.device.receiveTimeoutMs = static_cast<uint32_t>(
        (std::clamp)(integerValue, 1, 5000));
    integerValue = static_cast<int>(aim.device.networkPort);
    getInt("AimDeviceNetworkPort", integerValue);
    aim.device.networkPort = static_cast<uint32_t>(
        (std::clamp)(integerValue, 1, 65535));
    getBool("AimDeviceDtr", aim.device.enableDtr);
    getBool("AimDeviceRts", aim.device.enableRts);
    integerValue = aim.device.rawHidVid;
    getInt("AimDeviceRawHidVid", integerValue);
    aim.device.rawHidVid = static_cast<uint16_t>(
        (std::clamp)(integerValue, 0, 0xFFFF));
    integerValue = aim.device.rawHidPid;
    getInt("AimDeviceRawHidPid", integerValue);
    aim.device.rawHidPid = static_cast<uint16_t>(
        (std::clamp)(integerValue, 0, 0xFFFF));
    integerValue = aim.device.rawHidUsagePage;
    getInt("AimDeviceRawHidUsagePage", integerValue);
    aim.device.rawHidUsagePage = static_cast<uint16_t>(
        (std::clamp)(integerValue, 0, 0xFFFF));
    integerValue = aim.device.rawHidUsage;
    getInt("AimDeviceRawHidUsage", integerValue);
    aim.device.rawHidUsage = static_cast<uint16_t>(
        (std::clamp)(integerValue, 0, 0xFFFF));
    integerValue = static_cast<int>(aim.device.rawHidInterfaceIndex);
    getInt("AimDeviceRawHidInterface", integerValue);
    aim.device.rawHidInterfaceIndex = static_cast<uint32_t>(
        (std::clamp)(integerValue, 0, 255));
    integerValue = aim.device.rawHidExpectedReportBytes;
    getInt("AimDeviceRawHidReportBytes", integerValue);
    aim.device.rawHidExpectedReportBytes = static_cast<uint16_t>(
        (std::clamp)(integerValue, 0, 512));
    integerValue = aim.device.rawHidReportId;
    getInt("AimDeviceRawHidReportId", integerValue);
    aim.device.rawHidReportId = static_cast<uint8_t>(
        (std::clamp)(integerValue, 0, 255));
    integerValue = aim.device.rawHidButtonsOffset;
    getInt("AimDeviceRawHidButtonsOffset", integerValue);
    aim.device.rawHidButtonsOffset = static_cast<uint16_t>(
        (std::clamp)(integerValue, 0, 0xFFFF));
    integerValue = aim.device.rawHidXOffset;
    getInt("AimDeviceRawHidXOffset", integerValue);
    aim.device.rawHidXOffset = static_cast<uint16_t>(
        (std::clamp)(integerValue, 0, 511));
    integerValue = aim.device.rawHidYOffset;
    getInt("AimDeviceRawHidYOffset", integerValue);
    aim.device.rawHidYOffset = static_cast<uint16_t>(
        (std::clamp)(integerValue, 0, 511));
    integerValue = aim.device.rawHidAxisBytes;
    getInt("AimDeviceRawHidAxisBytes", integerValue);
    aim.device.rawHidAxisBytes =
        integerValue == 2 ? static_cast<uint8_t>(2)
                          : static_cast<uint8_t>(1);
    integerValue =
        static_cast<int>(aim.device.rawHidTransferMode);
    getInt("AimDeviceRawHidTransferMode", integerValue);
    aim.device.rawHidTransferMode =
        static_cast<SKJH::Aim::RawHidTransferMode>(
            (std::clamp)(integerValue, 0, 2));
    getBool("AimDeviceRawHidLittleEndian",
        aim.device.rawHidLittleEndian);
    SKJH_RegReadString(key, "AimDevicePort", aim.device.serialPort, 64);
    SKJH_RegReadString(
        key, "AimDeviceTemplate", aim.device.genericMoveTemplate, 512);
    SKJH_RegReadString(
        key, "AimDeviceNetworkIp", aim.device.networkIp, 64);
    SKJH_RegReadString(
        key, "AimDeviceNetworkUuid", aim.device.networkUuid, 64);
    aim.device.rawHidReportTemplate.clear();
    {
        DWORD valueType = 0;
        DWORD size = 0;
        if (RegQueryValueExA(
                key, "AimDeviceRawHidTemplate", nullptr,
                &valueType, nullptr, &size) == ERROR_SUCCESS &&
            valueType == REG_BINARY && size > 0 && size <= 512) {
            aim.device.rawHidReportTemplate.resize(size);
            if (RegQueryValueExA(
                    key, "AimDeviceRawHidTemplate", nullptr,
                    &valueType,
                    aim.device.rawHidReportTemplate.data(),
                    &size) != ERROR_SUCCESS) {
                aim.device.rawHidReportTemplate.clear();
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_AimConfigMutex);
        g_AimConfig = aim;
    }
    g_AimActivationDown.store(false);
    g_AimToggleActive.store(false);
    {
        std::lock_guard<std::shared_mutex> lock(g_AimStateMutex);
        g_AimState = {};
    }

    RegCloseKey(key);
    return true;
}

// ===================== 容器预分配 =====================
inline void ReserveContainers() {
}

// ═══════════════════════════════════════
//  线程1: 相机 (零延迟)
// ═══════════════════════════════════════
inline void ThreadCamera() {
    Throttler throttler;
    std::chrono::steady_clock::time_point lastValid{};
    while (g_Running.load()) {
        if (!mem.base) {
            throttler.sleepUntilNext(std::chrono::milliseconds(50));
            continue;
        }
        const SKJH_CameraData camera = SKJH_ReadCamera();
        if (camera.valid) {
            lastValid = std::chrono::steady_clock::now();
            const DWORD64 localPlayer = SKJH_GetLocalPlayer();
            g_LocalPlayer.store(localPlayer);
            int64_t localPlayerId = 0;
            if (localPlayer) {
                mem.Read(localPlayer + g_RuntimeOffsets.entityEntityId,
                         &localPlayerId, sizeof(localPlayerId));
                const DWORD64 klass = mem.Read<DWORD64>(localPlayer);
                if (klass) g_SKJH_PlayerKlass.store(klass);
            }
            g_LocalPlayerId.store(localPlayerId > 0 ? localPlayerId : 0);
            SKJH_Cam snapshot{};
            snapshot.camLoc = camera.camLoc;
            snapshot.camRot = camera.camRot;
            snapshot.localPos = camera.localPos;
            snapshot.right = camera.right;
            snapshot.up = camera.up;
            snapshot.forward = camera.forward;
            snapshot.camFov = camera.camFov;
            snapshot.valid = true;
            std::lock_guard<std::shared_mutex> lock(g_CamMutex);
            g_Camera = snapshot;
        } else if (lastValid != std::chrono::steady_clock::time_point{} &&
                   std::chrono::steady_clock::now() - lastValid >
                       std::chrono::seconds(1)) {
            std::lock_guard<std::shared_mutex> lock(g_CamMutex);
            g_Camera.valid = false;
            g_LocalPlayer.store(0);
            g_LocalPlayerId.store(0);
        }
        throttler.sleepUntilNext(std::chrono::milliseconds(SleepCamera()));
    }
}

// ═══════════════════════════════════════
//  线程2: 实体遍历 (遍历字典 + 属性系统读取)
// ═══════════════════════════════════════
inline void ThreadEntities() {
    Throttler throttler;
    while (g_Running.load()) {
        try {
            if (!mem.base) {
                throttler.sleepUntilNext(std::chrono::milliseconds(100));
                continue;
            }
            std::vector<SKJH_Entity> source = SKJH_EnumerateEntities();

            std::shared_ptr<std::vector<SKJH_EntityEntry>> previous;
            {
                std::shared_lock<std::shared_mutex> lock(g_DataMutex);
                previous = g_Entities;
            }
            std::unordered_map<int64_t, const SKJH_EntityEntry*> previousById;
            if (previous) {
                previousById.reserve(previous->size());
                for (const auto& entity : *previous)
                    previousById.emplace(
                        SKJH_GetEntitySnapshotKey(
                            entity.entityId, entity.entity),
                        &entity);
            }

            auto next = std::make_shared<std::vector<SKJH_EntityEntry>>();
            next->reserve(source.size() + (previous ? previous->size() : 0));
            std::unordered_set<int64_t> seenEntityIds;
            seenEntityIds.reserve(source.size());
            int typeCount[SKJH_TYPE_COUNT] = {};
            int threatPlayers = 0;
            SKJH_Cam camera{};
            {
                std::shared_lock<std::shared_mutex> lock(g_CamMutex);
                camera = g_Camera;
            }
            const DWORD64 localPlayer = g_LocalPlayer.load();
            const int64_t localPlayerId = g_LocalPlayerId.load();
            const auto now = std::chrono::steady_clock::now();
            for (const auto& item : source) {
                SKJH_EntityEntry entity{};
                entity.entity = item.entityPtr;
                entity.klass = item.klass;
                entity.entityId = item.entityId;
                entity.type = (item.type >= 0 && item.type < SKJH_TYPE_COUNT)
                    ? item.type : SKJH_UNKNOWN;
                entity.classHash = item.classHash;
                entity.templateId = item.templateId;
                entity.spawnType = item.spawnType;
                entity.className = item.className;
                entity.entityUpdatedAt = now;
                entity.displayName = SKJH_GetTemplateDisplayName(
                    entity.type, entity.templateId, entity.className);
                if (entity.displayName.empty()) {
                    entity.displayName = SKJH_GetEntityClassLabel(
                        entity.className.c_str(), entity.type);
                }
                entity.pos = item.pos;
                entity.hp = item.hp;
                entity.maxHp = item.maxHp;
                entity.isLocalPlayer = item.entityPtr == localPlayer ||
                    (localPlayerId && item.entityId == localPlayerId);
                const auto entityKey = SKJH_GetEntitySnapshotKey(
                    entity.entityId, entity.entity);
                const auto old = previousById.find(entityKey);
                const bool sameEntity = old != previousById.end() &&
                    old->second->entity == entity.entity;
                const bool restorePlayerClassification = sameEntity &&
                    entity.type == SKJH_UNKNOWN &&
                    old->second->type == SKJH_PLAYER &&
                    now - old->second->entityUpdatedAt <= SKJH_PLAYER_TTL &&
                    (!Mem::IsUserAddress(entity.klass) ||
                     entity.classHash == 0 || entity.className.empty());
                if (restorePlayerClassification) {
                    entity.type = old->second->type;
                    if (!Mem::IsUserAddress(entity.klass))
                        entity.klass = old->second->klass;
                    entity.classHash = old->second->classHash;
                    entity.templateId = old->second->templateId;
                    entity.spawnType = old->second->spawnType;
                    entity.className = old->second->className;
                    entity.displayName = old->second->displayName;
                }
                if (sameEntity && old->second->classHash == entity.classHash) {
                    entity.playerIntel = old->second->playerIntel;
                    entity.playerIntelUpdatedAt =
                        old->second->playerIntelUpdatedAt;
                    if (SKJH_AreBonesFresh(*old->second, now)) {
                        entity.hasBones = true;
                        entity.boneUpdatedAt = old->second->boneUpdatedAt;
                        for (int bone = 0; bone < BONE_COUNT; ++bone) {
                            entity.bones[bone] = old->second->bones[bone];
                            entity.boneValid[bone] = old->second->boneValid[bone];
                        }
                    }
                }
                if (camera.valid) {
                    const float dx = entity.pos.X - camera.localPos.X;
                    const float dy = entity.pos.Y - camera.localPos.Y;
                    const float dz = entity.pos.Z - camera.localPos.Z;
                    entity.distance = std::sqrt(dx*dx + dy*dy + dz*dz);
                    if (entity.type == SKJH_PLAYER && !entity.isLocalPlayer &&
                        entity.distance < 100.0f) ++threatPlayers;
                }
                ++typeCount[entity.type];
                seenEntityIds.insert(entityKey);
                next->push_back(std::move(entity));
            }
            if (previous) {
                for (const auto& oldEntity : *previous) {
                    const auto retentionTtl = oldEntity.type == SKJH_PLAYER
                        ? SKJH_PLAYER_TTL : SKJH_ENTITY_TTL;
                    if (seenEntityIds.find(SKJH_GetEntitySnapshotKey(
                            oldEntity.entityId, oldEntity.entity)) !=
                            seenEntityIds.end() ||
                        now - oldEntity.entityUpdatedAt > retentionTtl)
                        continue;
                    SKJH_EntityEntry retained = oldEntity;
                    if (!SKJH_AreBonesFresh(retained, now))
                        SKJH_ClearBones(retained);
                    ++typeCount[retained.type];
                    if (retained.type == SKJH_PLAYER && !retained.isLocalPlayer &&
                        retained.distance < 100.0f) ++threatPlayers;
                    next->push_back(std::move(retained));
                }
            }

            std::lock_guard<std::shared_mutex> lock(g_DataMutex);
            const auto publishAt = std::chrono::steady_clock::now();
            if (g_Entities) {
                std::unordered_map<int64_t, const SKJH_EntityEntry*> latestById;
                latestById.reserve(g_Entities->size());
                for (const auto& latest : *g_Entities)
                    latestById.emplace(
                        SKJH_GetEntitySnapshotKey(
                            latest.entityId, latest.entity),
                        &latest);

                for (auto& entity : *next) {
                    const auto found = latestById.find(
                        SKJH_GetEntitySnapshotKey(
                            entity.entityId, entity.entity));
                    if (found != latestById.end() &&
                        found->second->entity == entity.entity &&
                        found->second->classHash == entity.classHash) {
                        const auto& latest = *found->second;
                        if (latest.playerIntelUpdatedAt >
                            entity.playerIntelUpdatedAt) {
                            entity.playerIntel = latest.playerIntel;
                            entity.playerIntelUpdatedAt =
                                latest.playerIntelUpdatedAt;
                            if (latest.playerIntelUpdatedAt >
                                    entity.entityUpdatedAt &&
                                std::isfinite(latest.hp) &&
                                std::isfinite(latest.maxHp) &&
                                latest.maxHp > 0.0f) {
                                entity.hp = latest.hp;
                                entity.maxHp = latest.maxHp;
                            }
                        }
                        if (latest.boneUpdatedAt > entity.boneUpdatedAt &&
                            SKJH_AreBonesFresh(latest, publishAt)) {
                            entity.hasBones = true;
                            entity.boneUpdatedAt = latest.boneUpdatedAt;
                            for (int bone = 0; bone < BONE_COUNT; ++bone) {
                                entity.bones[bone] = latest.bones[bone];
                                entity.boneValid[bone] = latest.boneValid[bone];
                            }
                        }
                    }
                    if (!SKJH_AreBonesFresh(entity, publishAt))
                        SKJH_ClearBones(entity);
                    entity.isLocalPlayer =
                        entity.entity == localPlayer ||
                        (localPlayerId &&
                         entity.entityId == localPlayerId);
                }
            }
            threatPlayers = 0;
            for (const auto& entity : *next) {
                if (entity.type == SKJH_PLAYER && !entity.isLocalPlayer &&
                    entity.distance < 100.0f) {
                    ++threatPlayers;
                }
            }
            g_Entities = std::move(next);
            for (int type = 0; type < SKJH_TYPE_COUNT; ++type)
                g_TypeCount[type] = typeCount[type];
            g_ThreatLevel = threatPlayers == 0 ? 0 :
                (threatPlayers <= 1 ? 1 : (threatPlayers <= 3 ? 2 : 3));
        } catch (const std::exception&) {
        } catch (...) {
        }
        throttler.sleepUntilNext(std::chrono::milliseconds(SleepESP()));
    }
}

// Refresh only player roots at a steady rate.  This path is intentionally
// independent from the large entity snapshot so a position update cannot
// stall drawing through g_DataMutex contention.
inline void ThreadPlayerPositions() {
    Throttler throttler;
    while (g_Running.load()) {
        try {
            std::shared_ptr<std::vector<SKJH_EntityEntry>> entities;
            {
                std::shared_lock<std::shared_mutex> lock(g_DataMutex);
                entities = g_Entities;
            }

            std::shared_ptr<const SKJH_PlayerPositionMap> previous;
            {
                std::shared_lock<std::shared_mutex> lock(g_PlayerPositionMutex);
                previous = g_PlayerPositions;
            }

            auto next = std::make_shared<SKJH_PlayerPositionMap>();
            if (entities) {
                next->reserve((std::min)(entities->size(), size_t{128}));
                const auto sampledAt = std::chrono::steady_clock::now();
                for (const auto& entity : *entities) {
                    if (!g_Running.load()) break;
                    if (entity.type != SKJH_PLAYER ||
                        !Mem::IsUserAddress(entity.entity) ||
                        !Mem::IsUserAddress(entity.klass)) {
                        continue;
                    }
                    const auto entityKey = SKJH_GetEntitySnapshotKey(
                        entity.entityId, entity.entity);

                    const FVector position =
                        SKJH_GetPosition(entity.entity, entity.klass);
                    const bool positionValid = SKJH_IsFiniteVector(position) &&
                        (position.X != 0.0f || position.Y != 0.0f ||
                         position.Z != 0.0f);
                    if (positionValid) {
                        (*next)[entityKey] = {
                            entity.entity, entity.classHash, position, sampledAt};
                        continue;
                    }

                    // Retain the most recent valid root through a transient
                    // DMA miss. The entity scan retains the same player, so
                    // this avoids an otherwise visible single-frame blink.
                    if (!previous) continue;
                    const auto old = previous->find(entityKey);
                    if (old == previous->end() ||
                        old->second.entity != entity.entity ||
                        old->second.classHash != entity.classHash ||
                        sampledAt - old->second.sampledAt >
                            SKJH_PLAYER_POSITION_TTL) {
                        continue;
                    }
                    (*next)[entityKey] = old->second;
                }
            }

            {
                std::lock_guard<std::shared_mutex> lock(g_PlayerPositionMutex);
                g_PlayerPositions = std::move(next);
            }
        } catch (const std::exception&) {
        } catch (...) {
        }
        throttler.sleepUntilNext(
            std::chrono::milliseconds(SleepPlayerPositions()));
    }
}

// ═══════════════════════════════════════
//  线程3: 骨骼世界坐标更新
// ═══════════════════════════════════════
inline SKJH_AimConfig SKJH_GetAimConfigSnapshot();

inline void ThreadBones() {
    Throttler throttler;
    struct BoneSnapshot {
        std::array<FVector, BONE_COUNT> positions{};
        std::array<bool, BONE_COUNT> valid{};
        DWORD64 entity = 0;
        int32_t classHash = 0;
        FVector sourcePosition{};
        std::chrono::steady_clock::time_point sampledAt{};
    };
    constexpr size_t kPublishBatchSize = 4;
    const auto publishUpdates = [](std::unordered_map<int64_t, BoneSnapshot>& updates) {
        if (updates.empty()) return;
        std::lock_guard<std::shared_mutex> lock(g_DataMutex);
        if (!g_Entities) {
            updates.clear();
            return;
        }
        auto merged = std::make_shared<std::vector<SKJH_EntityEntry>>(*g_Entities);
        const auto mergedAt = std::chrono::steady_clock::now();
        for (auto& entity : *merged) {
            const auto update = updates.find(entity.entityId);
            if (update == updates.end() ||
                update->second.entity != entity.entity ||
                update->second.classHash != entity.classHash ||
                mergedAt - update->second.sampledAt > SKJH_BONE_TTL ||
                entity.boneUpdatedAt > update->second.sampledAt) {
                continue;
            }
            const float dx = entity.pos.X - update->second.sourcePosition.X;
            const float dy = entity.pos.Y - update->second.sourcePosition.Y;
            const float dz = entity.pos.Z - update->second.sourcePosition.Z;
            if (dx*dx + dy*dy + dz*dz > 40000.0f) continue;
            entity.hasBones = true;
            entity.boneUpdatedAt = update->second.sampledAt;
            for (int bone = 0; bone < BONE_COUNT; ++bone) {
                entity.bones[bone] = update->second.positions[bone];
                entity.boneValid[bone] = update->second.valid[bone];
            }
        }
        g_Entities = std::move(merged);
        updates.clear();
    };
    while (g_Running.load()) {
        if (!SKJH_GetAimConfigSnapshot().enabled) {
            throttler.sleepUntilNext(std::chrono::milliseconds(150));
            continue;
        }
        std::shared_ptr<std::vector<SKJH_EntityEntry>> data;
        { std::shared_lock<std::shared_mutex> lk(g_DataMutex); data = g_Entities; }
        if (!data || data->empty()) {
            throttler.sleepUntilNext(std::chrono::milliseconds(50));
            continue;
        }
        const auto goMap = SKJH_ReadEntityGoMap();
        std::unordered_map<int64_t, BoneSnapshot> updates;
        updates.reserve(kPublishBatchSize);
        for (const auto& entity : *data) {
            if (!g_Running.load()) break;
            if (!SKJH_HasBones(entity.type) || !entity.entityId) continue;
            const auto go = goMap.find(entity.entityId);
            if (go == goMap.end()) continue;
            SKJH_BoneData readBones[BONE_COUNT];
            // Never replace a known-good snapshot with a partial Transform
            // read. A transient DMA miss must retain the previous complete
            // anchors instead of making the actor box jump or disappear.
            if (SKJH_ReadPlayerBones(go->second, entity.entity, entity.pos,
                                     readBones) != BONE_COUNT)
                continue;
            BoneSnapshot snapshot;
            snapshot.entity = entity.entity;
            snapshot.classHash = entity.classHash;
            snapshot.sourcePosition = entity.pos;
            for (int bone = 0; bone < BONE_COUNT; ++bone) {
                snapshot.positions[bone] = readBones[bone].worldPos;
                snapshot.valid[bone] = readBones[bone].valid;
            }
            snapshot.sampledAt = std::chrono::steady_clock::now();
            updates.emplace(entity.entityId, std::move(snapshot));
            if (updates.size() >= kPublishBatchSize) publishUpdates(updates);
        }
        publishUpdates(updates);
        throttler.sleepUntilNext(std::chrono::milliseconds(SleepBones()));
    }
}

// Player weapon state changes quickly; inventory content is sampled less often.
inline void ThreadPlayerIntel() {
    Throttler throttler;
    std::unordered_map<int64_t, DWORD64> weaponCache;
    auto nextInventorySample = std::chrono::steady_clock::now();

    struct IntelUpdate {
        int64_t entityId = 0;
        DWORD64 entity = 0;
        DWORD64 klass = 0;
        int32_t classHash = 0;
        bool isLocalPlayer = false;
        int64_t currentWeaponId = 0;
        float hp = 0.0f;
        float maxHp = 0.0f;
        SKJH_PlayerIntel intel;
    };

    while (g_Running.load()) {
        try {
            std::shared_ptr<std::vector<SKJH_EntityEntry>> data;
            {
                std::shared_lock<std::shared_mutex> lock(g_DataMutex);
                data = g_Entities;
            }

            std::vector<IntelUpdate> updates;
            std::unordered_set<int64_t> unresolvedWeaponIds;
            std::unordered_set<int64_t> activeWeaponIds;
            if (data) {
                updates.reserve(data->size());
                for (const auto& entity : *data) {
                    if (entity.type != SKJH_PLAYER ||
                        !Mem::IsUserAddress(entity.entity) ||
                        !Mem::IsUserAddress(entity.klass)) {
                        continue;
                    }
                    IntelUpdate update;
                    update.entityId = entity.entityId;
                    update.entity = entity.entity;
                    update.klass = entity.klass;
                    update.classHash = entity.classHash;
                    update.isLocalPlayer = entity.isLocalPlayer;
                    update.intel = entity.playerIntel;
                    update.hp = SKJH_GetHp(update.entity, update.klass);
                    update.maxHp =
                        SKJH_GetMaxHp(update.entity, update.klass);
                    SKJH_GetPropertyValueByName(
                        update.entity, update.klass, "CurrentWeaponId",
                        update.currentWeaponId);
                    if (update.currentWeaponId > 0) {
                        activeWeaponIds.insert(update.currentWeaponId);
                        if (weaponCache.find(update.currentWeaponId) ==
                            weaponCache.end()) {
                            unresolvedWeaponIds.insert(update.currentWeaponId);
                        }
                    }
                    updates.push_back(std::move(update));
                }
            }

            for (auto it = weaponCache.begin(); it != weaponCache.end();) {
                if (activeWeaponIds.find(it->first) == activeWeaponIds.end())
                    it = weaponCache.erase(it);
                else
                    ++it;
            }
            if (!unresolvedWeaponIds.empty()) {
                auto resolved =
                    SKJH_FindEntityPointersById(unresolvedWeaponIds);
                for (const auto& pair : resolved)
                    weaponCache[pair.first] = pair.second;
            }

            const auto sampledAt = std::chrono::steady_clock::now();
            const bool sampleInventory = false;
            for (auto& update : updates) {
                DWORD64 weapon = 0;
                const auto cached = weaponCache.find(update.currentWeaponId);
                if (cached != weaponCache.end()) weapon = cached->second;
                if (!SKJH_ReadPlayerWeapon(
                        update.entity, update.klass, update.intel, weapon) &&
                    update.currentWeaponId > 0) {
                    weaponCache.erase(update.currentWeaponId);
                }
                if (sampleInventory) {
                    SKJH_ReadPlayerInventory(
                        update.entity, update.intel, update.isLocalPlayer);
                }
            }
            if (sampleInventory)
                nextInventorySample = sampledAt + std::chrono::milliseconds(1500);

            if (!updates.empty()) {
                std::unordered_map<int64_t, const IntelUpdate*> byId;
                byId.reserve(updates.size());
                for (const auto& update : updates)
                    byId[update.entityId] = &update;

                std::lock_guard<std::shared_mutex> lock(g_DataMutex);
                if (g_Entities) {
                    auto merged =
                        std::make_shared<std::vector<SKJH_EntityEntry>>(*g_Entities);
                    for (auto& entity : *merged) {
                        const auto found = byId.find(entity.entityId);
                        if (found == byId.end() ||
                            found->second->entity != entity.entity ||
                            found->second->classHash != entity.classHash) {
                            continue;
                        }
                        if (std::isfinite(found->second->hp) &&
                            std::isfinite(found->second->maxHp) &&
                            found->second->maxHp > 0.0f) {
                            entity.hp = found->second->hp;
                            entity.maxHp = found->second->maxHp;
                        }
                        const DWORD64 localPlayer =
                            g_LocalPlayer.load();
                        const int64_t localPlayerId =
                            g_LocalPlayerId.load();
                        entity.isLocalPlayer =
                            entity.entity == localPlayer ||
                            (localPlayerId &&
                             entity.entityId == localPlayerId);
                        entity.playerIntel = found->second->intel;
                        entity.playerIntelUpdatedAt = sampledAt;
                    }
                    g_Entities = std::move(merged);
                }
            }
        } catch (const std::exception&) {
        } catch (...) {
        }
        throttler.sleepUntilNext(
            std::chrono::milliseconds(SleepPlayerIntel()));
    }
}

inline void ThreadTemplateCatalog() {
    Throttler throttler;
    int retryDelayMs = 250;
    while (g_Running.load()) {
        bool haveEntities = false;
        {
            std::shared_lock<std::shared_mutex> lock(g_DataMutex);
            haveEntities = g_Entities && !g_Entities->empty();
        }
        if (!haveEntities) {
            throttler.sleepUntilNext(std::chrono::milliseconds(100));
            continue;
        }
        try {
            if (SKJH_RefreshTemplateCatalog()) return;
        } catch (const std::exception&) {
        } catch (...) {
        }
        throttler.sleepUntilNext(std::chrono::milliseconds(retryDelayMs));
        retryDelayMs = (std::min)(retryDelayMs * 2, 5000);
    }
}

inline SKJH_AimConfig SKJH_GetAimConfigSnapshot() {
    std::lock_guard<std::mutex> lock(g_AimConfigMutex);
    return g_AimConfig;
}

inline SKJH_AimState SKJH_GetAimStateSnapshot() {
    std::shared_lock<std::shared_mutex> lock(g_AimStateMutex);
    return g_AimState;
}

inline std::shared_ptr<SKJH::Aim::IAimDevice> SKJH_GetAimDeviceSnapshot() {
    std::lock_guard<std::mutex> lock(g_AimDeviceMutex);
    return g_AimDevice;
}

inline bool SKJH_HasConfiguredAimOutput(const SKJH_AimConfig& config) {
    const auto* descriptor =
        SKJH::Aim::AimDeviceRegistry::Find(config.device.kind);
    return descriptor && descriptor->capabilities.supported &&
        descriptor->capabilities.relativeMovement;
}

inline bool SKJH_ConnectConfiguredAimDevice() {
    std::lock_guard<std::mutex> connectLock(g_AimDeviceConnectMutex);
    const SKJH_AimConfig config = SKJH_GetAimConfigSnapshot();
    if (!SKJH_HasConfiguredAimOutput(config)) {
        g_AimDeviceReconnectRequested.store(false);
        return false;
    }
    g_AimDeviceReconnectRequested.store(true);
    std::shared_ptr<SKJH::Aim::IAimDevice> previous;
    {
        std::lock_guard<std::mutex> lock(g_AimDeviceMutex);
        previous = std::move(g_AimDevice);
    }
    if (previous) previous->Disconnect();
    auto created = SKJH::Aim::AimDeviceRegistry::Create(config.device);
    if (!created) return false;
    std::shared_ptr<SKJH::Aim::IAimDevice> device(std::move(created));
    const bool connected = device->Connect(config.device);
    {
        std::lock_guard<std::mutex> lock(g_AimDeviceMutex);
        g_AimDevice = std::move(device);
    }
    return connected;
}

inline void SKJH_DisconnectAimDevice() {
    g_AimDeviceReconnectRequested.store(false);
    std::lock_guard<std::mutex> connectLock(g_AimDeviceConnectMutex);
    std::shared_ptr<SKJH::Aim::IAimDevice> device;
    {
        std::lock_guard<std::mutex> lock(g_AimDeviceMutex);
        device = std::move(g_AimDevice);
    }
    if (device) device->Disconnect();
}

inline bool SKJH_LoadGlobalConfigAndApply() {
    const auto previous = SKJH_GetAimDeviceSnapshot();
    const bool reconnect =
        previous && previous->Status().IsConnected();
    if (!LoadGlobalConfig()) return false;

    g_AimActivationDown.store(false);
    g_AimToggleActive.store(false);
    SKJH_DisconnectAimDevice();
    if (reconnect) SKJH_ConnectConfiguredAimDevice();
    return true;
}

struct SKJH_AimCandidate {
    int64_t entityId = 0;
    DWORD64 entity = 0;
    int32_t classHash = 0;
    int bone = -1;
    FVector world{};
    FVector2D screen{};
    float screenDistance = 0.0f;
    float worldDistance = 0.0f;
    float healthRatio = 1.0f;
    bool fallback = false;
};

inline bool SKJH_AimCandidateMatches(
    const SKJH_AimCandidate& candidate, const SKJH_AimState& state) {
    return candidate.entityId == state.targetEntityId &&
        candidate.entity == state.targetEntity &&
        candidate.classHash == state.targetClassHash;
}

inline float SKJH_AimCandidateScore(
    const SKJH_AimCandidate& candidate, SKJH_AimPriority priority,
    float fovPixels) {
    switch (priority) {
        case SKJH_AimPriority::WorldDistance:
            return candidate.worldDistance +
                candidate.screenDistance / (std::max)(fovPixels, 1.0f);
        case SKJH_AimPriority::LowHealth:
            return candidate.healthRatio * 10000.0f +
                candidate.screenDistance;
        case SKJH_AimPriority::Crosshair:
        default:
            return candidate.screenDistance;
    }
}

inline bool SKJH_SelectAimBone(
    const SKJH_EntityEntry& entity, const SKJH_CameraData& camera,
    const SKJH_AimConfig& config, int screenWidth, int screenHeight,
    SKJH_AimCandidate& candidate) {
    constexpr std::chrono::milliseconds kAimBoneTtl{300};
    const auto now = std::chrono::steady_clock::now();
    if (!entity.hasBones ||
        entity.boneUpdatedAt == std::chrono::steady_clock::time_point{} ||
        now - entity.boneUpdatedAt > kAimBoneTtl) {
        return false;
    }

    const std::array<int, BONE_COUNT> fallbackOrder{
        BONE_HEAD, BONE_NECK, BONE_BODY, BONE_SPINE,
        BONE_LEFT_FOOT, BONE_RIGHT_FOOT};
    std::array<int, BONE_COUNT> order{};
    size_t count = 0;
    order[count++] = (std::clamp)(
        config.preferredBone, 0, BONE_COUNT - 1);
    if (config.boneFallback) {
        for (const int bone : fallbackOrder) {
            if (bone != order[0]) order[count++] = bone;
        }
    }

    const float centerX = screenWidth * 0.5f;
    const float centerY = screenHeight * 0.5f;
    for (size_t index = 0; index < count; ++index) {
        const int bone = order[index];
        if (!entity.boneValid[bone]) continue;
        FVector2D screen{};
        if (!SKJH_W2S(entity.bones[bone], camera,
                screenWidth, screenHeight, screen)) {
            continue;
        }
        const float deltaX = screen.X - centerX;
        const float deltaY = screen.Y - centerY;
        const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
        const bool insideFov = config.fovShape == SKJH_AimFovShape::Circle
            ? distance <= config.fovPixels
            : std::fabs(deltaX) <= config.fovPixels &&
                std::fabs(deltaY) <= config.fovPixels;
        if (!insideFov) continue;

        candidate.bone = bone;
        candidate.world = entity.bones[bone];
        candidate.screen = screen;
        candidate.screenDistance = distance;
        candidate.fallback = bone != order[0];
        return true;
    }
    return false;
}

inline void ThreadAimAssist() {
    Throttler throttler;
    SKJH_AimState previous;
    float residualX = 0.0f;
    float residualY = 0.0f;
    bool autoConnectArmed = false;
    std::chrono::steady_clock::time_point nextReconnectAttempt{};

    while (g_Running.load()) {
        const SKJH_AimConfig config = SKJH_GetAimConfigSnapshot();
        SKJH_AimState next;
        next.sampledAt = std::chrono::steady_clock::now();
        if (!config.enabled) {
            {
                std::lock_guard<std::shared_mutex> lock(g_AimStateMutex);
                g_AimState = next;
            }
            previous = next;
            residualX = residualY = 0.0f;
            autoConnectArmed = false;
            throttler.sleepUntilNext(std::chrono::milliseconds(100));
            continue;
        }

        if (!autoConnectArmed && SKJH_HasConfiguredAimOutput(config)) {
            // A saved supported device becomes active as soon as the user
            // enables aim. Explicit Disconnect still suppresses reconnects.
            g_AimDeviceReconnectRequested.store(true);
            autoConnectArmed = true;
            nextReconnectAttempt = {};
        }

        switch (config.activation) {
            case SKJH_AimActivation::Continuous:
                next.active = true;
                break;
            case SKJH_AimActivation::Toggle:
                next.active = g_AimToggleActive.load();
                break;
            case SKJH_AimActivation::Hold:
            default:
                next.active = g_AimActivationDown.load();
                break;
        }

        auto outputDevice = SKJH_GetAimDeviceSnapshot();
        SKJH::Aim::AimDeviceCapabilities outputCapabilities{};
        if (outputDevice) {
            outputCapabilities = outputDevice->Capabilities();
            next.outputReady =
                outputCapabilities.supported &&
                outputCapabilities.relativeMovement &&
                outputDevice->Status().IsConnected();
        }

        const auto reconnectNow = std::chrono::steady_clock::now();
        if (g_AimDeviceReconnectRequested.load() &&
            SKJH_HasConfiguredAimOutput(config) && !next.outputReady &&
            (nextReconnectAttempt == std::chrono::steady_clock::time_point{} ||
             reconnectNow >= nextReconnectAttempt)) {
            // Connection attempts can block for a device timeout. Keep them
            // bounded and avoid retrying every 4 ms after a transport fault.
            nextReconnectAttempt =
                reconnectNow + std::chrono::milliseconds(1500);
            SKJH_ConnectConfiguredAimDevice();
            outputDevice = SKJH_GetAimDeviceSnapshot();
            outputCapabilities = {};
            next.outputReady = false;
            if (outputDevice) {
                outputCapabilities = outputDevice->Capabilities();
                next.outputReady =
                    outputCapabilities.supported &&
                    outputCapabilities.relativeMovement &&
                    outputDevice->Status().IsConnected();
            }
        }

        std::shared_ptr<std::vector<SKJH_EntityEntry>> entities;
        SKJH_Cam camera;
        {
            std::shared_lock<std::shared_mutex> lock(g_DataMutex);
            entities = g_Entities;
        }
        {
            std::shared_lock<std::shared_mutex> lock(g_CamMutex);
            camera = g_Camera;
        }

        const int screenWidth = g_ScreenW;
        const int screenHeight = g_ScreenH;
        std::optional<SKJH_AimCandidate> best;
        std::optional<SKJH_AimCandidate> locked;
        if (entities && camera.valid && screenWidth > 0 && screenHeight > 0) {
            const SKJH_CameraData cameraData = ToCameraData(camera);
            for (const auto& entity : *entities) {
                if (entity.type != SKJH_PLAYER || entity.isLocalPlayer ||
                    entity.entityId == 0 || entity.hp <= 0.0f) {
                    continue;
                }
                SKJH_AimCandidate candidate;
                candidate.entityId = entity.entityId;
                candidate.entity = entity.entity;
                candidate.classHash = entity.classHash;
                if (!SKJH_SelectAimBone(
                        entity, cameraData, config,
                        screenWidth, screenHeight, candidate)) {
                    continue;
                }
                const float dx = candidate.world.X - camera.localPos.X;
                const float dy = candidate.world.Y - camera.localPos.Y;
                const float dz = candidate.world.Z - camera.localPos.Z;
                candidate.worldDistance = std::sqrt(dx*dx + dy*dy + dz*dz);
                if (!std::isfinite(candidate.worldDistance) ||
                    candidate.worldDistance > config.maxDistance) {
                    continue;
                }
                candidate.healthRatio =
                    entity.maxHp > 0.0f
                        ? (std::clamp)(entity.hp / entity.maxHp, 0.0f, 1.0f)
                        : 1.0f;

                if (config.lockTarget && next.active && previous.active &&
                    SKJH_AimCandidateMatches(candidate, previous)) {
                    locked = candidate;
                }
                if (!best ||
                    SKJH_AimCandidateScore(candidate, config.priority,
                        config.fovPixels) <
                    SKJH_AimCandidateScore(*best, config.priority,
                        config.fovPixels)) {
                    best = candidate;
                }
            }
        }

        const bool retainMissingTarget =
            config.lockTarget && next.active && previous.active &&
            previous.targetValid && !locked &&
            config.targetHoldMs > 0 &&
            next.sampledAt - previous.sampledAt <=
                std::chrono::milliseconds(config.targetHoldMs);
        if (locked) best = locked;
        else if (retainMissingTarget) best.reset();
        if (best) {
            next.targetValid = true;
            next.targetEntityId = best->entityId;
            next.targetEntity = best->entity;
            next.targetClassHash = best->classHash;
            next.targetBone = best->bone;
            next.targetWorld = best->world;
            next.targetScreen = best->screen;
            next.screenDistance = best->screenDistance;
            next.worldDistance = best->worldDistance;
            next.usedBoneFallback = best->fallback;

            const bool sameIdentity = SKJH_AimCandidateMatches(*best, previous);
            if (!sameIdentity) residualX = residualY = 0.0f;
            if (next.active) {
                const float centerX = screenWidth * 0.5f;
                const float centerY = screenHeight * 0.5f;
                float deltaX = next.targetScreen.X - centerX;
                float deltaY = next.targetScreen.Y - centerY;
                if (std::fabs(deltaX) <= config.deadZone) deltaX = 0.0f;
                if (std::fabs(deltaY) <= config.deadZone) deltaY = 0.0f;

                residualX += deltaX / (std::max)(config.smoothX, 1.0f);
                residualY += deltaY / (std::max)(config.smoothY, 1.0f);
                int moveX = static_cast<int>(std::round(residualX));
                int moveY = static_cast<int>(std::round(residualY));
                residualX -= static_cast<float>(moveX);
                residualY -= static_cast<float>(moveY);
                moveX = (std::clamp)(
                    moveX, -config.maximumStep, config.maximumStep);
                moveY = (std::clamp)(
                    moveY, -config.maximumStep, config.maximumStep);

                if (outputDevice && next.outputReady &&
                    (moveX != 0 || moveY != 0)) {
                    const int minimum = outputCapabilities.minimumDelta;
                    const int maximum = outputCapabilities.maximumDelta;
                    if (minimum < maximum) {
                        moveX = (std::clamp)(moveX, minimum, maximum);
                        moveY = (std::clamp)(moveY, minimum, maximum);
                        if (!outputDevice->Move(moveX, moveY)) {
                            // A failed write must not leave the worker in a
                            // fast failing loop. Recreate the adapter after
                            // the reconnect cooldown on the next pass.
                            outputDevice->Disconnect();
                            next.outputReady = false;
                            nextReconnectAttempt =
                                std::chrono::steady_clock::now() +
                                std::chrono::milliseconds(1500);
                        }
                    }
                }
            }
        } else {
            residualX = residualY = 0.0f;
        }

        {
            std::lock_guard<std::shared_mutex> lock(g_AimStateMutex);
            g_AimState = next;
        }
        if (next.targetValid || !retainMissingTarget) previous = next;
        if (!next.active) residualX = residualY = 0.0f;
        throttler.sleepUntilNext(std::chrono::milliseconds(4));
    }
}

// ═══════════════════════════════════════
//  启动工作线程
// ═══════════════════════════════════════
inline void GameStart(std::thread& t0, std::thread& t1, std::thread& t2,
                      std::thread& t3, std::thread& t4, std::thread& t5,
                      std::thread& t6) {
    g_Running.store(true);
    {
        std::lock_guard<std::shared_mutex> lock(g_PlayerPositionMutex);
        g_PlayerPositions = std::make_shared<SKJH_PlayerPositionMap>();
    }
    t0 = std::thread(ThreadCamera);
    t1 = std::thread(ThreadEntities);
    t2 = std::thread(ThreadPlayerPositions);
    t3 = std::thread(ThreadBones);
    t4 = std::thread(ThreadPlayerIntel);
    t5 = std::thread(ThreadTemplateCatalog);
    t6 = std::thread(ThreadAimAssist);
}

inline void GameStop(std::thread& t0, std::thread& t1, std::thread& t2,
                     std::thread& t3, std::thread& t4, std::thread& t5,
                     std::thread& t6) {
    g_Running.store(false);
    if (t0.joinable()) t0.join();
    if (t1.joinable()) t1.join();
    if (t2.joinable()) t2.join();
    if (t3.joinable()) t3.join();
    if (t4.joinable()) t4.join();
    if (t5.joinable()) t5.join();
    if (t6.joinable()) t6.join();
    SKJH_DisconnectAimDevice();
}

// ═══════════════════════════════════════
//  ESP 绘制 — 按实体类型差异化渲染
// ═══════════════════════════════════════

// 获取实体颜色 (IM_COL32)
inline ImU32 SKJH_GetTypeColor32(int type) {
    float c[4];
    SKJH_GetEntityColor(type, c);
    return IM_COL32((int)(c[0]*255), (int)(c[1]*255), (int)(c[2]*255), (int)(c[3]*255));
}

// 获取实体最大显示距离
inline int SKJH_GetTypeMaxDist(int type) {
    if (type < 0 || type >= SKJH_TYPE_COUNT) return 200;
    int d = g_TypeMaxDist[type];
    return (d < g_MaxDist) ? d : g_MaxDist;
}

inline void DrawESP() {
    auto* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;
    const int sw = g_ScreenW;
    const int sh = g_ScreenH;

    const SKJH_AimConfig aimConfig = SKJH_GetAimConfigSnapshot();
    const SKJH_AimState aimState = SKJH_GetAimStateSnapshot();
    if (aimConfig.enabled && aimConfig.showFov && sw > 0 && sh > 0) {
        const ImVec2 center(sw * 0.5f, sh * 0.5f);
        const ImU32 fovColor = aimState.active
            ? IM_COL32(167, 139, 250, 210)
            : IM_COL32(125, 105, 160, 145);
        if (aimConfig.fovShape == SKJH_AimFovShape::Circle) {
            dl->AddCircle(center, aimConfig.fovPixels, fovColor, 96, 1.25f);
        } else {
            dl->AddRect(
                ImVec2(center.x - aimConfig.fovPixels,
                       center.y - aimConfig.fovPixels),
                ImVec2(center.x + aimConfig.fovPixels,
                       center.y + aimConfig.fovPixels),
                fovColor, 0.0f, 0, 1.25f);
        }
    }

    std::shared_ptr<std::vector<SKJH_EntityEntry>> data;
    int threatLevel = 0;
    {
        std::shared_lock<std::shared_mutex> lk(g_DataMutex);
        data = g_Entities;
        threatLevel = g_ThreatLevel;
    }
    std::shared_ptr<const SKJH_PlayerPositionMap> playerPositions;
    {
        std::shared_lock<std::shared_mutex> lock(g_PlayerPositionMutex);
        playerPositions = g_PlayerPositions;
    }
    if (!data || data->empty()) {
        g_VisibleCount = 0;
        return;
    }

    SKJH_Cam cam;
    { std::shared_lock<std::shared_mutex> lk(g_CamMutex); cam = g_Camera; }
    if (!cam.valid) {
        g_VisibleCount = 0;
        return;
    }
    const SKJH_CameraData cameraData = ToCameraData(cam);
    const auto positionNow = std::chrono::steady_clock::now();

    const ImU32 kOutline = IM_COL32(0, 0, 0, 220);
    char buf[256];

    // 威胁警告 (屏幕顶部)
    if (g_ShowThreatWarn && threatLevel > 0) {
        const char* warnText = nullptr;
        ImU32 warnColor = 0;
        switch (threatLevel) {
            case 1: warnText = u8"⚠ 附近有敌人";     warnColor = IM_COL32(255, 200, 0, 255); break;
            case 2: warnText = u8"⚠⚠ 敌人较多";      warnColor = IM_COL32(255, 120, 0, 255); break;
            case 3: warnText = u8"⚠⚠⚠ 极度危险!";   warnColor = IM_COL32(255, 50, 50, 255); break;
        }
        if (warnText) {
            float tw = CalcTextWidth(warnText);
            float wx = sw * 0.5f - tw * 0.5f;
            float wy = 40.f;
            // 背景
            dl->AddRectFilled(ImVec2(wx - 12, wy - 4), ImVec2(wx + tw + 12, wy + 24),
                              IM_COL32(0, 0, 0, 180), 4.f);
            StrokeText(dl, warnText, ImVec2(wx, wy), kOutline, warnColor);
        }
    }

    // 按距离排序 (近的优先绘制在上层)
    struct SKJH_DrawCandidate {
        const SKJH_EntityEntry* entity = nullptr;
        FVector renderPosition{};
        float distanceSquared = 0.0f;
    };
    const auto resolveRenderPosition =
        [&](const SKJH_EntityEntry& entity) {
            FVector position = entity.pos;
            if (entity.type != SKJH_PLAYER || !playerPositions)
                return position;
            const auto sample = playerPositions->find(
                SKJH_GetEntitySnapshotKey(entity.entityId, entity.entity));
            if (sample != playerPositions->end() &&
                sample->second.entity == entity.entity &&
                sample->second.classHash == entity.classHash &&
                positionNow - sample->second.sampledAt <=
                    SKJH_PLAYER_POSITION_TTL) {
                position = sample->second.position;
            }
            return position;
        };
    static thread_local std::vector<SKJH_DrawCandidate> sortedEntities;
    sortedEntities.clear();
    if (sortedEntities.capacity() < data->size())
        sortedEntities.reserve(data->size());
    for (auto& e : *data) {
        if (!g_DrawSelf && e.isLocalPlayer) continue;
        if (e.type < 0 || e.type >= SKJH_TYPE_COUNT) continue;
        if (!g_TypeEnabled[e.type]) continue;
        if (!g_ShowItems && SKJH_IsLootType(e.type)) continue;
        if (!SKJH_IsTemplateEnabled(e.type, e.templateId)) continue;
        const FVector renderPosition = resolveRenderPosition(e);
        const float maxDistance =
            static_cast<float>(SKJH_GetTypeMaxDist(e.type));
        const float dx = renderPosition.X - cam.localPos.X;
        const float dy = renderPosition.Y - cam.localPos.Y;
        const float dz = renderPosition.Z - cam.localPos.Z;
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz) ||
            distanceSquared > maxDistance * maxDistance) {
            continue;
        }
        sortedEntities.push_back({&e, renderPosition, distanceSquared});
    }
    const auto actorBegin = std::partition(
        sortedEntities.begin(), sortedEntities.end(),
        [](const SKJH_DrawCandidate& candidate) {
            const int type = candidate.entity->type;
            return type != SKJH_PLAYER && type != SKJH_MONSTER;
        });
    std::sort(actorBegin, sortedEntities.end(),
        [](const SKJH_DrawCandidate& a, const SKJH_DrawCandidate& b) {
            return a.distanceSquared > b.distanceSquared;
        });

    int visibleCount = 0;
    for (const auto& candidate : sortedEntities) {
        const auto& e = *candidate.entity;

        // Keep actor box anchors independent from the asynchronous bone pass.
        // Entity position is the stable transform root; the fixed Y offset is
        // the actor height. This prevents a partial bone read from making a
        // valid player box jump or disappear between frames.
        const FVector& renderPosition = candidate.renderPosition;
        const float dist = sqrtf(candidate.distanceSquared);

        // 按类型最大距离过滤
        int maxD = SKJH_GetTypeMaxDist(e.type);
        if (dist > maxD) continue;

        // Project without screen bounds first, then clip the visible geometry.
        FVector2D screenBot{};
        const bool bottomProjectionValid = SKJH_W2SUnclipped(
            renderPosition, cameraData, sw, sh, screenBot);
        const bool bottomProjected = bottomProjectionValid &&
            SKJH_IsScreenPointNear(screenBot, sw, sh);

        // Use the transform root plus a fixed height for every actor box.
        // Do not substitute head/foot bones here: that data is refreshed on a
        // separate DMA thread and is intentionally not a draw precondition.
        const float headHeight =
            (e.type == SKJH_PLAYER || e.type == SKJH_MONSTER) ? 1.8f : 0.5f;
        const FVector headPos{renderPosition.X,
                              renderPosition.Y + headHeight,
                              renderPosition.Z};
        FVector2D screenTop{};
        const bool topProjectionValid = SKJH_W2SUnclipped(
            headPos, cameraData, sw, sh, screenTop);
        const bool topProjected = topProjectionValid &&
            SKJH_IsScreenPointNear(screenTop, sw, sh);
        const bool anchorsProjected = bottomProjectionValid && topProjectionValid;
        FVector2D clippedTop = screenTop;
        FVector2D clippedBottom = screenBot;
        bool visibleAnchorSegment = false;
        if (anchorsProjected) {
            visibleAnchorSegment = SKJH_ClipScreenLine(
                clippedTop, clippedBottom, sw, sh);
        }
        const float anchorHeight = anchorsProjected
            ? fabsf(screenTop.Y - screenBot.Y) : 0.0f;
        const FVector2D markerScreen = anchorsProjected
            ? FVector2D{screenBot.X, screenBot.Y - anchorHeight * 0.5f}
            : screenBot;
        const bool markerVisible = bottomProjectionValid &&
            SKJH_IsScreenPointNear(markerScreen, sw, sh, 8.0f);
        const bool actor = e.type == SKJH_PLAYER || e.type == SKJH_MONSTER;
        if (actor) {
            const bool visibleAnchorPoint =
                (bottomProjectionValid && bottomProjected) ||
                (topProjectionValid && topProjected);
            if (!visibleAnchorSegment && !visibleAnchorPoint)
                continue;
        } else if (!markerVisible) {
            continue;
        }

        float actorBoxLeft = 0.0f;
        float actorBoxRight = 0.0f;
        float actorBoxTop = 0.0f;
        float actorBoxBottom = 0.0f;
        bool actorBoxVisible = false;
        if (actor) {
            if (visibleAnchorSegment) {
                const float width = anchorHeight * 0.4f;
                const float centerX = (clippedTop.X + clippedBottom.X) * 0.5f;
                actorBoxLeft = (std::max)(0.0f, centerX - width * 0.5f);
                actorBoxRight = (std::min)(static_cast<float>(sw), centerX + width * 0.5f);
                actorBoxTop = (std::max)(0.0f,
                    (std::min)(clippedTop.Y, clippedBottom.Y));
                actorBoxBottom = (std::min)(static_cast<float>(sh),
                    (std::max)(clippedTop.Y, clippedBottom.Y));
            } else {
                // Near-plane clipping can leave exactly one fixed anchor
                // visible. Draw a bounded fallback instead of dropping the
                // actor box for that frame.
                const FVector2D anchor = topProjected ? screenTop : screenBot;
                constexpr float kFallbackHeight = 40.0f;
                constexpr float kFallbackWidth = 16.0f;
                actorBoxLeft = (std::max)(0.0f,
                    anchor.X - kFallbackWidth * 0.5f);
                actorBoxRight = (std::min)(static_cast<float>(sw),
                    anchor.X + kFallbackWidth * 0.5f);
                if (topProjected) {
                    actorBoxTop = (std::max)(0.0f, anchor.Y);
                    actorBoxBottom = (std::min)(static_cast<float>(sh),
                        anchor.Y + kFallbackHeight);
                } else {
                    actorBoxTop = (std::max)(0.0f,
                        anchor.Y - kFallbackHeight);
                    actorBoxBottom = (std::min)(static_cast<float>(sh), anchor.Y);
                }
            }
            actorBoxVisible = actorBoxRight - actorBoxLeft >= 1.0f &&
                actorBoxBottom - actorBoxTop >= 1.0f;
        }

        ImU32 entityColor = SKJH_GetTypeColor32(e.type);
        const char* typeLabel = g_lang && !e.className.empty()
            ? e.className.c_str()
            : (!e.displayName.empty() ? e.displayName.c_str() : SKJH_GetEntityShortName(e.type));
        ++visibleCount;

        // ═══ 按类型差异化绘制 ═══

        if (e.type == SKJH_PLAYER || e.type == SKJH_MONSTER) {
            // ── 玩家/怪物: 完整方框 + 骨骼 + 血条 ──

            // 方框 (带描边)
            if (g_ShowBox && actorBoxVisible) {
                // 外描边
                dl->AddRect(ImVec2(actorBoxLeft - 1, actorBoxTop - 1),
                            ImVec2(actorBoxRight + 1, actorBoxBottom + 1),
                            kOutline, 0, 0, 1.f);
                dl->AddRect(ImVec2(actorBoxLeft, actorBoxTop),
                            ImVec2(actorBoxRight, actorBoxBottom),
                            entityColor, 0, 0, 1.5f);
            }

            // 射线
            if (g_ShowRays && topProjected) {
                dl->AddLine(ImVec2(sw / 2.f, (float)sh), ImVec2(screenTop.X, screenTop.Y),
                            IM_COL32(0, 0, 0, 96) /* 半透明黑射线 */, 1.f);
            }

            // 血条 (竖直, 在方框左侧)
            if (g_ShowHealth && e.maxHp > 0 && actorBoxVisible) {
                const float barX = actorBoxLeft >= 8.0f
                    ? actorBoxLeft - 6.0f
                    : (std::min)(static_cast<float>(sw) - 5.0f, actorBoxRight + 3.0f);
                float hpRatio = e.hp / e.maxHp;
                if (hpRatio < 0) hpRatio = 0;
                if (hpRatio > 1) hpRatio = 1;
                const float visibleHeight = actorBoxBottom - actorBoxTop;
                const float fillH = visibleHeight * hpRatio;

                dl->AddRectFilled(ImVec2(barX - 2, actorBoxTop - 2),
                                  ImVec2(barX + 5, actorBoxBottom + 2),
                                  IM_COL32(0, 0, 0, 200));
                dl->AddRectFilled(ImVec2(barX, actorBoxTop),
                                  ImVec2(barX + 3, actorBoxBottom),
                                  IM_COL32(40, 40, 40, 220));
                ImU32 hpColor;
                if (hpRatio > 0.6f)      hpColor = IM_COL32(0, 220, 0, 255);
                else if (hpRatio > 0.3f) hpColor = IM_COL32(255, 200, 0, 255);
                else                     hpColor = IM_COL32(255, 50, 50, 255);
                dl->AddRectFilled(ImVec2(barX, actorBoxBottom - fillH),
                                  ImVec2(barX + 3, actorBoxBottom), hpColor);
            }

            // 类型标签 (头顶)
            if (g_ShowName && topProjected) {
                snprintf(buf, sizeof(buf), u8"%s", typeLabel);
                StrokeText(dl, buf,
                    ImVec2(screenTop.X - CalcTextWidth(buf) * 0.5f, screenTop.Y - 18),
                    kOutline, entityColor);
            }

            // 距离 (脚底)
            float textY = screenBot.Y + 3;
            const auto drawActorInfoLine =
                [&](const char* text, ImU32 color) {
                    if (!bottomProjected || !text || !text[0] ||
                        textY > static_cast<float>(sh) - 14.0f) {
                        return;
                    }
                    const float width = CalcTextWidth(text);
                    float x = screenBot.X - width * 0.5f;
                    if (width < static_cast<float>(sw) - 4.0f)
                        x = (std::max)(2.0f,
                            (std::min)(x, static_cast<float>(sw) - width - 2.0f));
                    else
                        x = 2.0f;
                    StrokeText(dl, text, ImVec2(x, textY), kOutline, color);
                    textY += 15.0f;
                };
            if (g_ShowDistance && bottomProjected) {
                snprintf(buf, sizeof(buf), u8"%.0fm", dist);
                drawActorInfoLine(buf, IM_COL32(255, 255, 255, 220));
            }

            // 血量数字
            if (g_ShowHealth && e.maxHp > 0 && bottomProjected) {
                snprintf(buf, sizeof(buf), u8"血量:%.0f/%.0f", e.hp, e.maxHp);
                drawActorInfoLine(buf, IM_COL32(255, 255, 255, 220));
            }

            if (e.type == SKJH_PLAYER && g_ShowWeapon &&
                e.playerIntel.weaponValid) {
                snprintf(buf, sizeof(buf), u8"武器:%s",
                    e.playerIntel.weaponName.c_str());
                drawActorInfoLine(buf, IM_COL32(196, 181, 253, 235));
            }

            // 实体ID
            if (g_ShowEntityId && (topProjected || bottomProjected)) {
                snprintf(buf, sizeof(buf), u8"ID:%lld", (long long)e.entityId);
                const FVector2D& idAnchor = topProjected ? screenTop : screenBot;
                StrokeText(dl, buf,
                    ImVec2(idAnchor.X - CalcTextWidth(buf) * 0.5f,
                           topProjected ? screenTop.Y - 33 : screenBot.Y + 3),
                    kOutline, IM_COL32(180, 180, 180, 200));
            }

        } else {
            // ── 其他实体: 简化标记 + 距离 ──

            if (SKJH_IsLootType(e.type)) {
                // 物资箱/尸体/资源: 菱形标记
                float cx = markerScreen.X;
                float cy = markerScreen.Y;
                float sz = 6.f;
                // 描边
                dl->AddQuad(ImVec2(cx, cy - sz - 1), ImVec2(cx + sz + 1, cy),
                            ImVec2(cx, cy + sz + 1), ImVec2(cx - sz - 1, cy),
                            kOutline);
                dl->AddQuad(ImVec2(cx, cy - sz), ImVec2(cx + sz, cy),
                            ImVec2(cx, cy + sz), ImVec2(cx - sz, cy),
                            entityColor);

                // 名称与距离是独立开关。
                if (g_ShowName || g_ShowDistance) {
                    if (g_ShowName && g_ShowDistance)
                        snprintf(buf, sizeof(buf), u8"%s %.0fm", typeLabel, dist);
                    else if (g_ShowName)
                        snprintf(buf, sizeof(buf), u8"%s", typeLabel);
                    else
                        snprintf(buf, sizeof(buf), u8"%.0fm", dist);
                    StrokeText(dl, buf,
                        ImVec2(cx - CalcTextWidth(buf) * 0.5f, cy + sz + 4),
                        kOutline, entityColor);
                }
            } else if (e.type == SKJH_PART || e.type == SKJH_VEHICLE) {
                // 部件/载具: 小方框
                float sz = 5.f;
                float cx = markerScreen.X;
                float cy = markerScreen.Y;
                dl->AddRect(ImVec2(cx - sz - 1, cy - sz - 1),
                            ImVec2(cx + sz + 1, cy + sz + 1), kOutline);
                dl->AddRect(ImVec2(cx - sz, cy - sz),
                            ImVec2(cx + sz, cy + sz), entityColor);

                // 血条 (部件可能有血量)
                if (g_ShowHealth && e.maxHp > 0) {
                    float hpRatio = e.hp / e.maxHp;
                    if (hpRatio < 0) hpRatio = 0;
                    if (hpRatio > 1) hpRatio = 1;
                    float barW = sz * 2.f;
                    dl->AddRectFilled(ImVec2(cx - sz, cy + sz + 2),
                                      ImVec2(cx + sz, cy + sz + 5),
                                      IM_COL32(0, 0, 0, 200));
                    ImU32 hpC = (hpRatio > 0.5f) ? IM_COL32(0, 200, 0, 255) :
                                (hpRatio > 0.25f) ? IM_COL32(255, 200, 0, 255) :
                                IM_COL32(255, 50, 50, 255);
                    dl->AddRectFilled(ImVec2(cx - sz, cy + sz + 2),
                                      ImVec2(cx - sz + barW * hpRatio, cy + sz + 5), hpC);
                }

                if (g_ShowName || g_ShowDistance) {
                    if (g_ShowName && g_ShowDistance)
                        snprintf(buf, sizeof(buf), u8"%s %.0fm", typeLabel, dist);
                    else if (g_ShowName)
                        snprintf(buf, sizeof(buf), u8"%s", typeLabel);
                    else
                        snprintf(buf, sizeof(buf), u8"%.0fm", dist);
                    StrokeText(dl, buf,
                        ImVec2(cx - CalcTextWidth(buf) * 0.5f, cy + sz + 8),
                        kOutline, entityColor);
                }
            } else {
                // 其他类型: 小圆点 + 文字
                float cx = markerScreen.X;
                float cy = markerScreen.Y;
                dl->AddCircleFilled(ImVec2(cx, cy), 4.f, kOutline);
                dl->AddCircleFilled(ImVec2(cx, cy), 3.f, entityColor);

                if (g_ShowName || g_ShowDistance) {
                    if (g_ShowName && g_ShowDistance)
                        snprintf(buf, sizeof(buf), u8"%s %.0fm", typeLabel, dist);
                    else if (g_ShowName)
                        snprintf(buf, sizeof(buf), u8"%s", typeLabel);
                    else
                        snprintf(buf, sizeof(buf), u8"%.0fm", dist);
                    StrokeText(dl, buf,
                        ImVec2(cx - CalcTextWidth(buf) * 0.5f, cy + 8),
                        kOutline, entityColor);
                }
            }
        }
    }

    g_VisibleCount = visibleCount;

    // 屏幕中心准星指示
    float cx = sw * 0.5f, cy = sh * 0.5f;
    dl->AddLine(ImVec2(cx - 8, cy), ImVec2(cx + 8, cy), IM_COL32(255, 255, 255, 160), 1.f);
    dl->AddLine(ImVec2(cx, cy - 8), ImVec2(cx, cy + 8), IM_COL32(255, 255, 255, 160), 1.f);

    if (aimConfig.enabled && aimState.targetValid) {
        const ImVec2 target(aimState.targetScreen.X, aimState.targetScreen.Y);
        const ImU32 targetColor = aimState.usedBoneFallback
            ? IM_COL32(255, 190, 70, 245)
            : IM_COL32(167, 139, 250, 245);
        if (aimConfig.showTargetLine) {
            dl->AddLine(ImVec2(cx, cy), target,
                IM_COL32(0, 0, 0, 180), 3.0f);
            dl->AddLine(ImVec2(cx, cy), target, targetColor, 1.25f);
        }
        if (aimConfig.showTargetMarker) {
            constexpr float radius = 10.0f;
            constexpr float corner = 5.0f;
            dl->AddLine(ImVec2(target.x - radius, target.y - radius),
                        ImVec2(target.x - radius + corner, target.y - radius),
                        targetColor, 2.0f);
            dl->AddLine(ImVec2(target.x - radius, target.y - radius),
                        ImVec2(target.x - radius, target.y - radius + corner),
                        targetColor, 2.0f);
            dl->AddLine(ImVec2(target.x + radius, target.y - radius),
                        ImVec2(target.x + radius - corner, target.y - radius),
                        targetColor, 2.0f);
            dl->AddLine(ImVec2(target.x + radius, target.y - radius),
                        ImVec2(target.x + radius, target.y - radius + corner),
                        targetColor, 2.0f);
            dl->AddLine(ImVec2(target.x - radius, target.y + radius),
                        ImVec2(target.x - radius + corner, target.y + radius),
                        targetColor, 2.0f);
            dl->AddLine(ImVec2(target.x - radius, target.y + radius),
                        ImVec2(target.x - radius, target.y + radius - corner),
                        targetColor, 2.0f);
            dl->AddLine(ImVec2(target.x + radius, target.y + radius),
                        ImVec2(target.x + radius - corner, target.y + radius),
                        targetColor, 2.0f);
            dl->AddLine(ImVec2(target.x + radius, target.y + radius),
                        ImVec2(target.x + radius, target.y + radius - corner),
                        targetColor, 2.0f);
        }
    }
}
