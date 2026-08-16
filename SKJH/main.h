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
#include <fstream>
#include <iomanip>
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
    bool    isBot;
    bool    botKnown;
    std::string className;
    std::string displayName;
    FVector pos;
    float   hp;
    float   maxHp;
    float   distance;
    bool    hasBones;
    FVector bones[BONE_COUNT];
    bool    boneValid[BONE_COUNT];
    std::chrono::steady_clock::time_point boneSampledAt[BONE_COUNT]{};
    FVector boneSampleRoots[BONE_COUNT]{};
    bool    boneSampleRootValid[BONE_COUNT]{};
    std::chrono::steady_clock::time_point boneUpdatedAt{};
    std::chrono::steady_clock::time_point entityUpdatedAt{};
    bool    isLocalPlayer;
    SKJH_PlayerIntel playerIntel;
    std::chrono::steady_clock::time_point playerIntelUpdatedAt{};
};

// A zero HP value is meaningful only when the entity exposed a valid,
// positive maximum HP. Keep unknown health data visible so a transient DMA
// read failure does not make a live player flicker out of the overlay.
// NPCs are intentionally excluded: the active NPCEntity schema has no
// Hp/MaxHp pair, so probing guessed property IDs can produce false values.
inline bool SKJH_ShouldHideDeadActor(const SKJH_EntityEntry& entity) {
    if (entity.type != SKJH_PLAYER)
        return false;
    return std::isfinite(entity.hp) && std::isfinite(entity.maxHp) &&
        entity.maxHp > 0.0f && entity.hp <= 0.0f;
}

inline constexpr std::chrono::milliseconds SKJH_ENTITY_TTL{250};
// A full dictionary pass can miss an actor for a frame while DMA is busy.
// Keep players briefly so their fixed-position boxes do not blink.
inline constexpr std::chrono::milliseconds SKJH_PLAYER_TTL{1500};
// Adapt the display age to measured full-pass cost. It decays gradually so a
// single fast pass does not discard actors missed by a transient DMA short read.
inline std::atomic<int64_t> g_BoneFreshnessMs{2500};
inline constexpr std::chrono::milliseconds SKJH_PLAYER_POSITION_TTL{500};
// Keep the last accepted RootBone visible through a short DMA gap instead of
// falling back to a lagging network property for one or two frames.
inline constexpr std::chrono::milliseconds SKJH_PLAYER_ROOT_LAST_GOOD_TTL{500};

inline std::chrono::milliseconds SKJH_BoneFreshnessWindow() {
    return std::chrono::milliseconds(g_BoneFreshnessMs.load(
        std::memory_order_acquire));
}

inline bool SKJH_IsBoneFresh(
    const SKJH_EntityEntry& entity, int bone,
    std::chrono::steady_clock::time_point now,
    std::chrono::milliseconds freshness = SKJH_BoneFreshnessWindow()) {
    return bone >= 0 && bone < BONE_COUNT && entity.hasBones &&
        entity.boneValid[bone] &&
        entity.boneSampledAt[bone] !=
            std::chrono::steady_clock::time_point{} &&
        now - entity.boneSampledAt[bone] <= freshness;
}

inline bool SKJH_AreBonesFresh(
    const SKJH_EntityEntry& entity,
    std::chrono::steady_clock::time_point now) {
    if (!entity.hasBones) return false;
    for (int bone = 0; bone < BONE_COUNT; ++bone) {
        if (!SKJH_IsBoneFresh(entity, bone, now)) return false;
    }
    return true;
}

// Partial freshness: enough valid bones to render a visible skeleton. The
// six legacy direct-anchor points (head, torso, spine, feet, neck) are always
// available from ClientPlayerGo fields; requiring all 20 animation points
// made the skeleton permanently invisible when the transform-tree binding
// failed on a game update.
inline constexpr int SKJH_MIN_BONES_FOR_RENDER = 6;
inline int SKJH_FreshBoneCount(
    const SKJH_EntityEntry& entity,
    std::chrono::steady_clock::time_point now) {
    if (!entity.hasBones) return 0;
    int count = 0;
    for (int bone = 0; bone < BONE_COUNT; ++bone) {
        if (SKJH_IsBoneFresh(entity, bone, now)) ++count;
    }
    return count;
}
inline bool SKJH_HasAnyFreshBones(
    const SKJH_EntityEntry& entity,
    std::chrono::steady_clock::time_point now) {
    return SKJH_FreshBoneCount(entity, now) >= SKJH_MIN_BONES_FOR_RENDER;
}

inline void SKJH_ClearBones(SKJH_EntityEntry& entity) {
    entity.hasBones = false;
    entity.boneUpdatedAt = {};
    for (int bone = 0; bone < BONE_COUNT; ++bone) {
        entity.bones[bone] = {};
        entity.boneValid[bone] = false;
        entity.boneSampledAt[bone] = {};
        entity.boneSampleRoots[bone] = {};
        entity.boneSampleRootValid[bone] = false;
    }
}

inline void SKJH_PruneExpiredBones(
    SKJH_EntityEntry& entity,
    std::chrono::steady_clock::time_point now) {
    // Only clear the pose when every bone has expired. A partial set (some
    // bones still fresh, some stale) is still drawable and should not be
    // discarded wholesale.
    int freshCount = 0;
    for (int bone = 0; bone < BONE_COUNT; ++bone) {
        if (SKJH_IsBoneFresh(entity, bone, now)) ++freshCount;
    }
    if (freshCount == 0) {
        SKJH_ClearBones(entity);
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

struct SKJH_PlayerRootSample {
    DWORD64 entity = 0;
    int32_t classHash = 0;
    FVector position{};
    FVector entityPosition{};
    float entityDelta = -1.0f;
    float frameDelta = -1.0f;
    uint32_t retainedPasses = 0;
    std::chrono::steady_clock::time_point sampledAt{};
};
using SKJH_PlayerRootMap = std::unordered_map<int64_t, SKJH_PlayerRootSample>;
inline std::shared_mutex g_PlayerRootMutex;
inline std::shared_ptr<const SKJH_PlayerRootMap> g_PlayerRoots =
    std::make_shared<SKJH_PlayerRootMap>();
inline std::atomic<int64_t> g_PlayerRootFreshnessMs{500};
inline std::atomic<int> g_PlayerRootLastMapCount{0};
inline std::atomic<int> g_PlayerRootLastValidCount{0};
inline std::atomic<int64_t> g_PlayerRootLastPassMs{0};
inline std::atomic<int> g_PlayerRootLastRequestCount{0};
inline std::atomic<int> g_PlayerRootLastAcceptedCount{0};
inline std::atomic<int> g_PlayerRootLastRetainedCount{0};
inline std::atomic<int> g_PlayerRootLastRejectedCount{0};
inline std::atomic<int> g_PlayerRootLastSpatialRejectCount{0};
inline std::atomic<int> g_PlayerRootLastMotionRejectCount{0};

inline std::chrono::milliseconds SKJH_PlayerRootFreshnessWindow() {
    return std::chrono::milliseconds(g_PlayerRootFreshnessMs.load(
        std::memory_order_acquire));
}

inline std::chrono::milliseconds SKJH_PlayerRootDisplayWindow() {
    const auto measured = SKJH_PlayerRootFreshnessWindow();
    return (std::max)(measured, SKJH_PLAYER_ROOT_LAST_GOOD_TTL);
}

inline int64_t SKJH_GetEntitySnapshotKey(int64_t entityId, DWORD64 entity) {
    // Entity IDs can transiently read as zero. User-mode entity pointers are
    // stable for the life of an actor and provide a collision-free fallback.
    return entityId != 0 ? entityId : -static_cast<int64_t>(entity);
}

// A zero class hash is a normal DMA short-read state, not an actor lifetime
// change. Entity pointer plus entity id remain the stable identity keys.
inline bool SKJH_ClassHashCompatible(int32_t first, int32_t second) {
    return first == 0 || second == 0 || first == second;
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
// Skeleton rendering is retired. Sampling stays disabled unless another
// feature explicitly opts in through these state variables.
inline bool g_ShowSkeleton = false;
inline bool g_ShowPlayerSkeleton = false;
inline bool g_ShowBotSkeleton = false;
inline std::atomic_bool g_SkeletonSamplingEnabled{false};
inline std::atomic<uint8_t> g_SkeletonCategoryMask{0};

inline void SKJH_UpdateSkeletonSamplingState() {
    g_ShowSkeleton = g_ShowPlayerSkeleton || g_ShowBotSkeleton;
    g_SkeletonSamplingEnabled.store(g_ShowSkeleton,
        std::memory_order_release);
    const uint8_t mask = (g_ShowPlayerSkeleton ? 0x1u : 0u) |
        (g_ShowBotSkeleton ? 0x2u : 0u);
    g_SkeletonCategoryMask.store(mask, std::memory_order_release);
}
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
    true,  // NPC — show NPCs by default
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

// Data workers are intentionally uncapped from the renderer, but a zero
// interval makes the DMA/VMM backend busy-spin when a pass completes quickly.
// These short minimum periods only apply between completed DMA passes; a pass
// that takes longer than its period is never delayed further.
inline constexpr int kEntityWorkerIntervalMs = 4;
inline constexpr int kCameraWorkerIntervalMs = 2;
inline constexpr int kBoneWorkerIntervalMs = 4;
inline constexpr int kPlayerPositionWorkerIntervalMs = 2;
inline constexpr int kPlayerRootWorkerIntervalMs = 8;

inline int SleepESP()    { return kEntityWorkerIntervalMs; }
inline int SleepCamera() { return kCameraWorkerIntervalMs; }
inline int SleepBones()  { return kBoneWorkerIntervalMs; }
inline int SleepPlayerIntel() { return 80; }
inline int SleepPlayerPositions() { return kPlayerPositionWorkerIntervalMs; }
inline int SleepPlayerRoots() { return kPlayerRootWorkerIntervalMs; }

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
    ok = SKJH_RegWriteDword(key, "ConfigVersion", 5) && ok;
    setInt("DisplayMode", g_DisplayMode);
    setInt("MaxDist", g_MaxDist);
    setInt("HotkeyVK", g_HotkeyVK);
    setInt("ItemsHotkeyVK", g_ItemsHotkeyVK);
    setBool("ShowBox", g_ShowBox);
    setBool("ShowSkeleton", g_ShowPlayerSkeleton || g_ShowBotSkeleton);
    setBool("PlayerSkeleton", g_ShowPlayerSkeleton);
    setBool("BotSkeleton", g_ShowBotSkeleton);
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
    // Skeleton rendering has been retired. Bone sampling remains available
    // only when a separate aim feature explicitly requests it.
    g_ShowSkeleton = false;
    g_ShowPlayerSkeleton = false;
    g_ShowBotSkeleton = false;
    SKJH_UpdateSkeletonSamplingState();
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

            // ── Bootstrap playerKlass from local player ──
            // The local player is always a PlayerEntity (or derived).
            // Reading its klass pointer directly avoids the unreliable
            // DMA class-name read that causes all entities to be UNKNOWN.
            if (!g_SKJH_PlayerKlass.load()) {
                const DWORD64 localPlayer = SKJH_GetLocalPlayer();
                if (Mem::IsUserAddress(localPlayer)) {
                    const DWORD64 localKlass = mem.Read<DWORD64>(localPlayer);
                    if (Mem::IsUserAddress(localKlass)) {
                        // Try to verify via class name first
                        if (SKJH_KlassIsOrDerivesFrom(localKlass, "PlayerEntity")) {
                            g_SKJH_PlayerKlass.store(localKlass);
                            // Also cache it
                            SKJH_KlassInfo info;
                            info.type = SKJH_PLAYER;
                            info.name = "PlayerEntity";
                            {
                                std::lock_guard<std::shared_mutex> lk(g_SKJH_KlassMutex);
                                g_SKJH_KlassMap.emplace(localKlass, info);
                            }
                        } else {
                            // Class name read failed but local player is
                            // definitely a player — trust the pointer.
                            g_SKJH_PlayerKlass.store(localKlass);
                            SKJH_KlassInfo info;
                            info.type = SKJH_PLAYER;
                            info.name = "PlayerEntity";
                            {
                                std::lock_guard<std::shared_mutex> lk(g_SKJH_KlassMutex);
                                g_SKJH_KlassMap.emplace(localKlass, info);
                            }
                        }
                    }
                }
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
                entity.isBot = item.isBot;
                entity.botKnown = item.botKnown;
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
                if (sameEntity && SKJH_ClassHashCompatible(
                        old->second->classHash, entity.classHash)) {
                    // Robot identity is immutable for the actor lifetime. Keep
                    // the last confirmed value through a transient DMA miss.
                    if (!entity.botKnown && old->second->botKnown) {
                        entity.isBot = old->second->isBot;
                        entity.botKnown = true;
                    }
                    entity.playerIntel = old->second->playerIntel;
                    entity.playerIntelUpdatedAt =
                        old->second->playerIntelUpdatedAt;
                    if (SKJH_HasAnyFreshBones(*old->second, now)) {
                        entity.hasBones = true;
                        entity.boneUpdatedAt = old->second->boneUpdatedAt;
                        for (int bone = 0; bone < BONE_COUNT; ++bone) {
                            entity.bones[bone] = old->second->bones[bone];
                            entity.boneValid[bone] = old->second->boneValid[bone];
                            entity.boneSampledAt[bone] =
                                old->second->boneSampledAt[bone];
                            entity.boneSampleRoots[bone] =
                                old->second->boneSampleRoots[bone];
                            entity.boneSampleRootValid[bone] =
                                old->second->boneSampleRootValid[bone];
                        }
                    }
                }
                if (camera.valid) {
                    const float dx = entity.pos.X - camera.localPos.X;
                    const float dy = entity.pos.Y - camera.localPos.Y;
                    const float dz = entity.pos.Z - camera.localPos.Z;
                    entity.distance = std::sqrt(dx*dx + dy*dy + dz*dz);
                    if (entity.type == SKJH_PLAYER && !entity.isLocalPlayer &&
                        !SKJH_ShouldHideDeadActor(entity) &&
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
                    SKJH_PruneExpiredBones(retained, now);
                    ++typeCount[retained.type];
                    if (retained.type == SKJH_PLAYER && !retained.isLocalPlayer &&
                        !SKJH_ShouldHideDeadActor(retained) &&
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
                        SKJH_ClassHashCompatible(
                            found->second->classHash, entity.classHash)) {
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
                            SKJH_HasAnyFreshBones(latest, publishAt)) {
                            entity.hasBones = true;
                            entity.boneUpdatedAt = latest.boneUpdatedAt;
                            for (int bone = 0; bone < BONE_COUNT; ++bone) {
                                entity.bones[bone] = latest.bones[bone];
                                entity.boneValid[bone] = latest.boneValid[bone];
                                entity.boneSampledAt[bone] =
                                    latest.boneSampledAt[bone];
                                entity.boneSampleRoots[bone] =
                                    latest.boneSampleRoots[bone];
                                entity.boneSampleRootValid[bone] =
                                    latest.boneSampleRootValid[bone];
                            }
                        }
                    }
                    SKJH_PruneExpiredBones(entity, publishAt);
                    entity.isLocalPlayer =
                        entity.entity == localPlayer ||
                        (localPlayerId &&
                         entity.entityId == localPlayerId);
                }
            }
            threatPlayers = 0;
            for (const auto& entity : *next) {
                if (entity.type == SKJH_PLAYER && !entity.isLocalPlayer &&
                    !SKJH_ShouldHideDeadActor(entity) &&
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
                    const auto sampledAt = std::chrono::steady_clock::now();
                    const bool positionValid = SKJH_IsFiniteVector(position) &&
                        (position.X != 0.0f || position.Y != 0.0f ||
                         position.Z != 0.0f);
                    if (positionValid) {
                        if (previous) {
                            const auto old = previous->find(entityKey);
                            if (old != previous->end() &&
                                old->second.entity == entity.entity &&
                                SKJH_ClassHashCompatible(
                                    old->second.classHash, entity.classHash)) {
                                const float dx = position.X - old->second.position.X;
                                const float dy = position.Y - old->second.position.Y;
                                const float dz = position.Z - old->second.position.Z;
                                const float displacementSq = dx*dx + dy*dy + dz*dz;
                                const float elapsed = (std::max)(0.001f,
                                    std::chrono::duration<float>(
                                        sampledAt - old->second.sampledAt).count());
                                // Entity properties advance in network-sized
                                // steps. Only reject an implausible pointer/
                                // dataset glitch; accepting normal multi-metre
                                // steps keeps the overlay in sync with motion.
                                const float maximumDisplacement =
                                    (std::max)(25.0f, elapsed * 250.0f);
                                if (std::isfinite(displacementSq) &&
                                    displacementSq > maximumDisplacement *
                                        maximumDisplacement &&
                                    sampledAt - old->second.sampledAt <
                                        std::chrono::milliseconds(100)) {
                                    (*next)[entityKey] = old->second;
                                    continue;
                                }
                            }
                        }
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
                        !SKJH_ClassHashCompatible(
                            old->second.classHash, entity.classHash) ||
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
        std::array<std::chrono::steady_clock::time_point,
                   BONE_COUNT> boneSampledAt{};
        std::array<FVector, BONE_COUNT> sampledRoots{};
        std::array<bool, BONE_COUNT> sampledRootValid{};
        DWORD64 entity = 0;
        int32_t classHash = 0;
        FVector sourcePosition{};
        std::chrono::steady_clock::time_point sampledAt{};
    };
    struct BoneLastGood {
        std::array<FVector, BONE_COUNT> positions{};
        std::array<std::chrono::steady_clock::time_point,
                   BONE_COUNT> sampledAt{};
        std::array<FVector, BONE_COUNT> sampledRoots{};
        std::array<bool, BONE_COUNT> sampledRootValid{};
        DWORD64 entity = 0;
        int32_t classHash = 0;
        std::chrono::steady_clock::time_point lastTouched{};
    };
    const auto publishUpdates = [](std::unordered_map<int64_t, BoneSnapshot>& updates) {
        if (updates.empty()) return;
        const auto applyUpdates = [&updates](
                std::vector<SKJH_EntityEntry>& entities) {
            const auto mergedAt = std::chrono::steady_clock::now();
            for (auto& entity : entities) {
                const auto update = updates.find(SKJH_GetEntitySnapshotKey(
                    entity.entityId, entity.entity));
                if (update == updates.end() ||
                    update->second.entity != entity.entity ||
                    !SKJH_ClassHashCompatible(
                        update->second.classHash, entity.classHash) ||
                    entity.boneUpdatedAt > update->second.sampledAt) {
                    continue;
                }
                const float dx = entity.pos.X - update->second.sourcePosition.X;
                const float dy = entity.pos.Y - update->second.sourcePosition.Y;
                const float dz = entity.pos.Z - update->second.sourcePosition.Z;
                if (dx*dx + dy*dy + dz*dz > 40000.0f) continue;
                bool allFresh = true;
                const auto freshness = SKJH_BoneFreshnessWindow();
                for (int bone = 0; bone < BONE_COUNT; ++bone) {
                    const bool fresh = update->second.valid[bone] &&
                        update->second.boneSampledAt[bone] !=
                            std::chrono::steady_clock::time_point{} &&
                        mergedAt - update->second.boneSampledAt[bone] <=
                            freshness;
                    entity.bones[bone] = fresh
                        ? update->second.positions[bone] : FVector{};
                    entity.boneValid[bone] = fresh;
                    entity.boneSampledAt[bone] = fresh
                        ? update->second.boneSampledAt[bone]
                        : std::chrono::steady_clock::time_point{};
                    entity.boneSampleRoots[bone] = fresh
                        ? update->second.sampledRoots[bone] : FVector{};
                    entity.boneSampleRootValid[bone] = fresh &&
                        update->second.sampledRootValid[bone];
                    allFresh = allFresh && fresh;
                }
                // Accept partial poses: a binding failure or a transient DMA
                // short read can leave some animation bones missing while the
                // six direct-anchor points remain valid. Publishing those is
                // strictly better than dropping the entire skeleton.
                int validCount = 0;
                for (int bone = 0; bone < BONE_COUNT; ++bone)
                    if (entity.boneValid[bone]) ++validCount;
                if (validCount < SKJH_MIN_BONES_FOR_RENDER) continue;
                entity.hasBones = true;
                entity.boneUpdatedAt = update->second.sampledAt;
            }
        };
        for (int attempt = 0; attempt < 2; ++attempt) {
            std::shared_ptr<std::vector<SKJH_EntityEntry>> base;
            {
                std::shared_lock<std::shared_mutex> lock(g_DataMutex);
                base = g_Entities;
            }
            if (!base) break;
            auto merged =
                std::make_shared<std::vector<SKJH_EntityEntry>>(*base);
            applyUpdates(*merged);
            {
                std::lock_guard<std::shared_mutex> lock(g_DataMutex);
                if (g_Entities != base) continue;
                g_Entities = std::move(merged);
            }
            updates.clear();
            return;
        }
        // An entity refresh won both optimistic races. Guarantee this pose
        // batch is not discarded; the uncommon fallback keeps the lock only
        // for one final copy-and-publish operation.
        {
            std::lock_guard<std::shared_mutex> lock(g_DataMutex);
            if (g_Entities) {
                auto merged =
                    std::make_shared<std::vector<SKJH_EntityEntry>>(*g_Entities);
                applyUpdates(*merged);
                g_Entities = std::move(merged);
            }
        }
        updates.clear();
    };
    std::unordered_map<int64_t, BoneLastGood> lastGoodByEntity;
    while (g_Running.load()) {
        const bool aimNeedsBones = SKJH_GetAimConfigSnapshot().enabled;
        const uint8_t skeletonCategoryMask = g_SkeletonCategoryMask.load(
            std::memory_order_acquire);
        std::shared_ptr<std::vector<SKJH_EntityEntry>> data;
        { std::shared_lock<std::shared_mutex> lk(g_DataMutex); data = g_Entities; }
        if (!data || data->empty()) {
            throttler.sleepUntilNext(std::chrono::milliseconds(50));
            continue;
        }
        const auto passStartedAt = std::chrono::steady_clock::now();
        const auto goMap = SKJH_ReadEntityGoMap();

        // RootBone is cheap compared with a complete pose. Publish every
        // player's interpolated root first, so rendering can translate the
        // most recent complete pose while the next pose batch is sampled.
        auto rootUpdates = std::make_shared<SKJH_PlayerRootMap>();
        rootUpdates->reserve(data->size());
        std::shared_ptr<const SKJH_PlayerRootMap> previousRoots;
        {
            std::shared_lock<std::shared_mutex> lock(g_PlayerRootMutex);
            previousRoots = g_PlayerRoots;
        }
        const auto rootPassStartedAt = std::chrono::steady_clock::now();
        struct RootPending {
            int64_t key = 0;
            DWORD64 entity = 0;
            int32_t classHash = 0;
            FVector entityPosition{};
        };
        std::vector<SKJH_PlayerRootBatchRequest> rootRequests;
        std::vector<RootPending> rootPending;
        rootRequests.reserve(data->size());
        rootPending.reserve(data->size());
        for (const auto& entity : *data) {
            if (!g_Running.load()) break;
            if (!SKJH_HasBones(entity.type) || !entity.entityId) continue;
            const int64_t key = SKJH_GetEntitySnapshotKey(
                entity.entityId, entity.entity);
            const auto go = goMap.find(entity.entityId);
            if (go == goMap.end()) continue;
            rootRequests.push_back({go->second, entity.entity});
            rootPending.push_back({key, entity.entity, entity.classHash,
                                   entity.pos});
        }
        std::vector<SKJH_PlayerRootBatchResult> rootResults;
        SKJH_ReadPlayerRootsBatch(rootRequests, rootResults);
        const auto rootBatchSampledAt = std::chrono::steady_clock::now();
        int rootAcceptedCount = 0;
        int rootRetainedCount = 0;
        int rootRejectedCount = 0;
        int rootSpatialRejectCount = 0;
        int rootMotionRejectCount = 0;
        for (size_t index = 0;
             index < rootPending.size() && index < rootResults.size(); ++index) {
            const RootPending& pending = rootPending[index];
            if (!rootResults[index].valid ||
                !SKJH_IsFiniteVector(rootResults[index].position)) {
                ++rootRejectedCount;
                continue;
            }

            const SKJH_PlayerRootSample* previousSample = nullptr;
            if (previousRoots) {
                const auto previous = previousRoots->find(pending.key);
                if (previous != previousRoots->end() &&
                    previous->second.entity == pending.entity &&
                    SKJH_ClassHashCompatible(
                        previous->second.classHash, pending.classHash) &&
                    SKJH_IsFiniteVector(previous->second.position)) {
                    previousSample = &previous->second;
                }
            }

            const FVector& candidate = rootResults[index].position;
            const FVector& property = pending.entityPosition;
            const float propertyDx = candidate.X - property.X;
            const float propertyDy = candidate.Y - property.Y;
            const float propertyDz = candidate.Z - property.Z;
            const float propertyDeltaSq = propertyDx * propertyDx +
                propertyDy * propertyDy + propertyDz * propertyDz;
            const bool propertyFinite = SKJH_IsFiniteVector(property) &&
                std::isfinite(propertyDeltaSq);
            // RootBone and the entity property are normally within a few
            // metres. This catches a finite but torn Transform hierarchy.
            const bool spatiallyCoherent = propertyFinite &&
                propertyDeltaSq <= 400.0f;

            bool motionCoherent = false;
            float frameDelta = -1.0f;
            if (previousSample) {
                const float dx = candidate.X - previousSample->position.X;
                const float dy = candidate.Y - previousSample->position.Y;
                const float dz = candidate.Z - previousSample->position.Z;
                const float deltaSq = dx * dx + dy * dy + dz * dz;
                frameDelta = std::sqrt((std::max)(0.0f, deltaSq));
                const float elapsed = (std::max)(0.001f,
                    std::chrono::duration<float>(
                        rootBatchSampledAt - previousSample->sampledAt).count());
                // Allow ordinary movement and a teleport corroborated by the
                // entity property, while rejecting an isolated DMA jump.
                const float maximumJump = (std::min)(50.0f,
                    (std::max)(5.0f, elapsed * 250.0f));
                motionCoherent = std::isfinite(deltaSq) &&
                    deltaSq <= maximumJump * maximumJump;
            }

            const bool accepted = previousSample
                ? (motionCoherent || spatiallyCoherent)
                : spatiallyCoherent;
            if (!accepted) {
                ++rootRejectedCount;
                if (!spatiallyCoherent) ++rootSpatialRejectCount;
                if (previousSample && !motionCoherent)
                    ++rootMotionRejectCount;
                continue;
            }

            SKJH_PlayerRootSample sample{};
            sample.entity = pending.entity;
            sample.classHash = pending.classHash;
            sample.position = candidate;
            sample.entityPosition = property;
            sample.entityDelta = propertyFinite
                ? std::sqrt((std::max)(0.0f, propertyDeltaSq)) : -1.0f;
            sample.frameDelta = frameDelta;
            sample.sampledAt = rootBatchSampledAt;
            rootUpdates->emplace(pending.key, std::move(sample));
            ++rootAcceptedCount;
        }
        for (const auto& entity : *data) {
            if (!g_Running.load()) break;
            if (!SKJH_HasBones(entity.type) || !entity.entityId) continue;
            const int64_t key = SKJH_GetEntitySnapshotKey(
                entity.entityId, entity.entity);
            if (rootUpdates->find(key) != rootUpdates->end()) continue;
            if (!previousRoots) continue;
            const auto previous = previousRoots->find(key);
            const auto retainAt = std::chrono::steady_clock::now();
            if (previous != previousRoots->end() &&
                previous->second.entity == entity.entity &&
                SKJH_ClassHashCompatible(
                    previous->second.classHash, entity.classHash) &&
                retainAt - previous->second.sampledAt <=
                    SKJH_PlayerRootDisplayWindow()) {
                SKJH_PlayerRootSample retained = previous->second;
                ++retained.retainedPasses;
                rootUpdates->emplace(key, std::move(retained));
                ++rootRetainedCount;
            }
        }
        const auto rootPassCompletedAt = std::chrono::steady_clock::now();
        const auto rootPassDurationMs = std::chrono::duration_cast<
            std::chrono::milliseconds>(
                rootPassCompletedAt - rootPassStartedAt).count();
        int rootValidCount = 0;
        for (const auto& sample : *rootUpdates) {
            if (sample.second.sampledAt !=
                std::chrono::steady_clock::time_point{}) {
                ++rootValidCount;
            }
        }
        g_PlayerRootLastMapCount.store(
            static_cast<int>(rootUpdates->size()), std::memory_order_release);
        g_PlayerRootLastValidCount.store(rootValidCount,
                                         std::memory_order_release);
        g_PlayerRootLastPassMs.store(rootPassDurationMs,
                                     std::memory_order_release);
        g_PlayerRootLastRequestCount.store(
            static_cast<int>(rootRequests.size()), std::memory_order_release);
        g_PlayerRootLastAcceptedCount.store(rootAcceptedCount,
                                            std::memory_order_release);
        g_PlayerRootLastRetainedCount.store(rootRetainedCount,
                                            std::memory_order_release);
        g_PlayerRootLastRejectedCount.store(rootRejectedCount,
                                            std::memory_order_release);
        g_PlayerRootLastSpatialRejectCount.store(rootSpatialRejectCount,
                                                 std::memory_order_release);
        g_PlayerRootLastMotionRejectCount.store(rootMotionRejectCount,
                                                std::memory_order_release);
        const int64_t measuredRootFreshnessMs = (std::clamp)(
            rootPassDurationMs * 2 + 50, int64_t{250}, int64_t{2000});
        const int64_t previousRootFreshnessMs =
            g_PlayerRootFreshnessMs.load(std::memory_order_acquire);
        g_PlayerRootFreshnessMs.store((std::max)(measuredRootFreshnessMs,
            previousRootFreshnessMs * 3 / 4), std::memory_order_release);
        {
            std::lock_guard<std::shared_mutex> lock(g_PlayerRootMutex);
            g_PlayerRoots = std::move(rootUpdates);
        }

        // RootBone is also the live actor anchor for ESP boxes. Keep this
        // lightweight pass running even when full skeleton rendering is off;
        // the expensive pose walk remains opt-in for skeleton/aim features.
        if (!aimNeedsBones &&
            !g_SkeletonSamplingEnabled.load(std::memory_order_acquire)) {
            throttler.sleepUntilNext(
                std::chrono::milliseconds(SleepPlayerRoots()));
            continue;
        }

        const size_t entityCount = data->size();
        std::unordered_map<int64_t, BoneSnapshot> updates;
        updates.reserve((std::min)(entityCount, size_t{256}));
        // Resolving a newly observed Transform hierarchy is substantially
        // slower than reading an already-cached pose. Publish each resolved
        // actor immediately so the overlay does not wait for every player in
        // the scene before drawing its first skeleton.
        constexpr size_t kBonePublishBatchSize = 1;
        size_t visited = 0;
        while (visited < entityCount) {
            const auto& entity = (*data)[visited];
            ++visited;
            if (!g_Running.load()) break;
            if (!SKJH_HasBones(entity.type) || !entity.entityId) continue;
            const bool renderNeedsBones = entity.botKnown
                ? (entity.isBot ? (skeletonCategoryMask & 0x2u) != 0
                                : (skeletonCategoryMask & 0x1u) != 0)
                : skeletonCategoryMask != 0;
            if (!aimNeedsBones && !renderNeedsBones) continue;
            const auto go = goMap.find(entity.entityId);
            if (go == goMap.end()) continue;
            SKJH_BoneData readBones[BONE_COUNT];
            FVector sampledRoot{};
            bool sampledRootValid = false;
            SKJH_ReadPlayerBones(go->second, entity.entity, entity.pos,
                                 readBones, &sampledRoot,
                                 &sampledRootValid);
            const auto sampledAt = std::chrono::steady_clock::now();

            // Publish only one coherent generation. Mixing six direct-anchor
            // points from this pass with fourteen cached animation points
            // produces a visibly torn pose and causes valid segments to be
            // rejected by the geometry checks below.
            int rawValidCount = 0;
            bool rawCoherent = true;
            for (int bone = 0; bone < BONE_COUNT; ++bone) {
                if (!readBones[bone].valid) continue;
                ++rawValidCount;
                rawCoherent = rawCoherent &&
                    SKJH_IsFiniteVector(readBones[bone].worldPos);
                if (sampledRootValid) {
                    const float dx = readBones[bone].worldPos.X - sampledRoot.X;
                    const float dy = readBones[bone].worldPos.Y - sampledRoot.Y;
                    const float dz = readBones[bone].worldPos.Z - sampledRoot.Z;
                    const float distanceSq = dx*dx + dy*dy + dz*dz;
                    rawCoherent = rawCoherent && std::isfinite(distanceSq) &&
                        distanceSq <= 25.0f;
                }
            }
            for (int first = 0; first < BONE_COUNT && rawCoherent; ++first) {
                if (!readBones[first].valid) continue;
                for (int second = first + 1; second < BONE_COUNT; ++second) {
                    if (!readBones[second].valid) continue;
                    const float dx = readBones[first].worldPos.X -
                        readBones[second].worldPos.X;
                    const float dy = readBones[first].worldPos.Y -
                        readBones[second].worldPos.Y;
                    const float dz = readBones[first].worldPos.Z -
                        readBones[second].worldPos.Z;
                    const float distanceSq = dx*dx + dy*dy + dz*dz;
                    if (!std::isfinite(distanceSq) || distanceSq > 25.0f) {
                        rawCoherent = false;
                        break;
                    }
                }
            }
            if (!rawCoherent || rawValidCount < SKJH_MIN_BONES_FOR_RENDER) {
                continue;
            }

            const int64_t entityKey = SKJH_GetEntitySnapshotKey(
                entity.entityId, entity.entity);
            BoneLastGood& cache = lastGoodByEntity[entityKey];
            if (cache.entity != entity.entity ||
                !SKJH_ClassHashCompatible(cache.classHash,
                                          entity.classHash)) {
                cache = {};
                cache.entity = entity.entity;
                cache.classHash = entity.classHash;
            }
            if (!cache.classHash && entity.classHash)
                cache.classHash = entity.classHash;
            cache.lastTouched = sampledAt;
            for (int bone = 0; bone < BONE_COUNT; ++bone) {
                if (!readBones[bone].valid) continue;
                cache.positions[bone] = readBones[bone].worldPos;
                cache.sampledAt[bone] = sampledAt;
                cache.sampledRoots[bone] = sampledRootValid
                    ? sampledRoot : FVector{};
                cache.sampledRootValid[bone] = sampledRootValid;
            }

            BoneSnapshot snapshot;
            snapshot.entity = entity.entity;
            snapshot.classHash = entity.classHash;
            snapshot.sourcePosition = entity.pos;
            const auto freshness = SKJH_BoneFreshnessWindow();
            int freshCount = 0;
            for (int bone = 0; bone < BONE_COUNT; ++bone) {
                const bool fresh = cache.sampledAt[bone] !=
                        std::chrono::steady_clock::time_point{} &&
                    sampledAt - cache.sampledAt[bone] <= freshness;
                snapshot.positions[bone] = fresh
                    ? cache.positions[bone] : FVector{};
                snapshot.valid[bone] = fresh;
                snapshot.boneSampledAt[bone] = fresh
                    ? cache.sampledAt[bone]
                    : std::chrono::steady_clock::time_point{};
                snapshot.sampledRoots[bone] = fresh
                    ? cache.sampledRoots[bone] : FVector{};
                snapshot.sampledRootValid[bone] = fresh &&
                    cache.sampledRootValid[bone];
                if (fresh) ++freshCount;
            }
            // Publish partial poses: at least the direct-anchor bones must be
            // fresh. This lets the skeleton render even when some animation
            // tree points are temporarily unavailable.
            if (freshCount < SKJH_MIN_BONES_FOR_RENDER) continue;
            snapshot.sampledAt = sampledAt;
            updates.emplace(entityKey, std::move(snapshot));
            if (updates.size() >= kBonePublishBatchSize) {
                publishUpdates(updates);
            }
        }
        for (auto cache = lastGoodByEntity.begin();
             cache != lastGoodByEntity.end();) {
            if (cache->second.lastTouched !=
                    std::chrono::steady_clock::time_point{} &&
                passStartedAt - cache->second.lastTouched >
                    std::chrono::seconds(30)) {
                cache = lastGoodByEntity.erase(cache);
            } else {
                ++cache;
            }
        }
        if (!updates.empty()) {
            const auto passCompletedAt = std::chrono::steady_clock::now();
            const auto passDurationMs = std::chrono::duration_cast<
                std::chrono::milliseconds>(passCompletedAt - passStartedAt).count();
            const int64_t measuredFreshnessMs = (std::clamp)(
                passDurationMs * 2 + 100,
                int64_t{1500}, int64_t{8000});
            const int64_t previousFreshnessMs = g_BoneFreshnessMs.load(
                std::memory_order_acquire);
            g_BoneFreshnessMs.store((std::max)(measuredFreshnessMs,
                previousFreshnessMs * 3 / 4), std::memory_order_release);
        }
        // Publish a possible tail batch after the per-player incremental
        // publishes above.
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
                            !SKJH_ClassHashCompatible(
                                found->second->classHash, entity.classHash)) {
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
    if (!entity.hasBones) return false;

    std::array<int, BONE_COUNT> order{};
    size_t count = 0;
    order[count++] = (std::clamp)(
        config.preferredBone, 0, BONE_COUNT - 1);
    if (config.boneFallback) {
        for (int bone = 0; bone < BONE_COUNT; ++bone) {
            if (bone != order[0]) order[count++] = bone;
        }
    }

    const float centerX = screenWidth * 0.5f;
    const float centerY = screenHeight * 0.5f;
    for (size_t index = 0; index < count; ++index) {
        const int bone = order[index];
        if (!SKJH_IsBoneFresh(entity, bone, now, kAimBoneTtl)) continue;
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
    {
        std::lock_guard<std::shared_mutex> lock(g_PlayerRootMutex);
        g_PlayerRoots = std::make_shared<SKJH_PlayerRootMap>();
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
    std::shared_ptr<const SKJH_PlayerPositionMap> playerPositions;
    {
        std::shared_lock<std::shared_mutex> lock(g_PlayerPositionMutex);
        playerPositions = g_PlayerPositions;
    }
    std::shared_ptr<const SKJH_PlayerRootMap> playerRoots;
    {
        std::shared_lock<std::shared_mutex> lock(g_PlayerRootMutex);
        playerRoots = g_PlayerRoots;
    }

    // ── Render diagnostic: capture rendering state every ~2s ──
    static int s_renderDiagFrame = 0;
    static auto s_renderDiagLastWrite = std::chrono::steady_clock::time_point{};
    ++s_renderDiagFrame;
    const auto renderDiagNow = std::chrono::steady_clock::now();
    const bool renderDiagDue = s_renderDiagFrame % 120 == 0 ||
        (s_renderDiagLastWrite == std::chrono::steady_clock::time_point{} &&
         s_renderDiagFrame > 10);
    if (renderDiagDue) {
        s_renderDiagLastWrite = renderDiagNow;
        SKJH_Cam diagCam{};
        { std::shared_lock<std::shared_mutex> lk(g_CamMutex); diagCam = g_Camera; }
        std::shared_ptr<std::vector<SKJH_EntityEntry>> diagData;
        { std::shared_lock<std::shared_mutex> lk(g_DataMutex); diagData = g_Entities; }
        std::ofstream diag("render_diag.json", std::ios::binary | std::ios::trunc);
        if (diag) {
            diag << std::fixed << std::setprecision(3);
            diag << "{\n";
            diag << "  \"frame\": " << s_renderDiagFrame << ",\n";
            diag << "  \"screen\": {\"w\": " << sw << ", \"h\": " << sh << "},\n";
            diag << "  \"camera\": {\"valid\": " << (diagCam.valid ? "true" : "false")
                 << ", \"pos\": [" << diagCam.camLoc.X << ',' << diagCam.camLoc.Y
                 << ',' << diagCam.camLoc.Z << "], \"fov\": " << diagCam.camFov
                 << ", \"forward\": [" << diagCam.forward.X << ',' << diagCam.forward.Y
                 << ',' << diagCam.forward.Z << "]},\n";
            diag << "  \"flags\": {\"showBox\": " << (g_ShowBox ? "true" : "false")
                 << ", \"showSkeleton\": " << (g_ShowSkeleton ? "true" : "false")
                 << ", \"showPlayerSkeleton\": " << (g_ShowPlayerSkeleton ? "true" : "false")
                 << ", \"showBotSkeleton\": " << (g_ShowBotSkeleton ? "true" : "false")
                 << ", \"typePlayerEnabled\": " << (g_TypeEnabled[SKJH_PLAYER] ? "true" : "false")
                 << ", \"typeMonsterEnabled\": " << (g_TypeEnabled[SKJH_MONSTER] ? "true" : "false")
                 << ", \"typeNpcEnabled\": " << (g_TypeEnabled[SKJH_NPC] ? "true" : "false")
                 << "},\n";
            diag << "  \"rootSampler\": {\"mapCount\": "
                 << g_PlayerRootLastMapCount.load(std::memory_order_acquire)
                 << ", \"validCount\": "
                 << g_PlayerRootLastValidCount.load(std::memory_order_acquire)
                 << ", \"requestCount\": "
                 << g_PlayerRootLastRequestCount.load(std::memory_order_acquire)
                 << ", \"acceptedCount\": "
                 << g_PlayerRootLastAcceptedCount.load(std::memory_order_acquire)
                 << ", \"retainedCount\": "
                 << g_PlayerRootLastRetainedCount.load(std::memory_order_acquire)
                 << ", \"rejectedCount\": "
                 << g_PlayerRootLastRejectedCount.load(std::memory_order_acquire)
                 << ", \"spatialRejectCount\": "
                 << g_PlayerRootLastSpatialRejectCount.load(std::memory_order_acquire)
                 << ", \"motionRejectCount\": "
                 << g_PlayerRootLastMotionRejectCount.load(std::memory_order_acquire)
                 << ", \"lastPassMs\": "
                 << g_PlayerRootLastPassMs.load(std::memory_order_acquire)
                 << ", \"freshnessMs\": "
                 << g_PlayerRootFreshnessMs.load(std::memory_order_acquire)
                 << ", \"displayWindowMs\": "
                 << SKJH_PlayerRootDisplayWindow().count()
                 << "},\n";
            const int diagEntityCount = diagData ? static_cast<int>(diagData->size()) : 0;
            int diagPlayerCount = 0, diagPlayersWithBones = 0, diagPlayersWithFreshBones = 0;
            int diagUnityRootCount = 0, diagPlayerPropertyCount = 0;
            diag << "  \"entities\": {\n";
            diag << "    \"total\": " << diagEntityCount << ",\n";
            diag << "    \"players\": [";
            bool firstPlayer = true;
            if (diagData) {
                for (const auto& e : *diagData) {
                    if (e.type != SKJH_PLAYER) continue;
                    ++diagPlayerCount;
                    FVector diagPosition = e.pos;
                    const char* diagPositionSource = "entity_property";
                    int64_t diagPositionAgeMs = -1;
                    float diagRootEntityDelta = -1.0f;
                    float diagRootFrameDelta = -1.0f;
                    uint32_t diagRootRetainedPasses = 0;
                    const auto diagKey = SKJH_GetEntitySnapshotKey(
                        e.entityId, e.entity);
                    if (playerRoots) {
                        const auto root = playerRoots->find(diagKey);
                        if (root != playerRoots->end() &&
                            root->second.entity == e.entity &&
                            SKJH_ClassHashCompatible(
                                root->second.classHash, e.classHash)) {
                            diagRootEntityDelta = root->second.entityDelta;
                            diagRootFrameDelta = root->second.frameDelta;
                            diagRootRetainedPasses = root->second.retainedPasses;
                            if (renderDiagNow - root->second.sampledAt <=
                                    SKJH_PlayerRootDisplayWindow()) {
                                diagPosition = root->second.position;
                                diagPositionSource = "unity_root";
                                diagPositionAgeMs = std::chrono::duration_cast<
                                    std::chrono::milliseconds>(
                                        renderDiagNow - root->second.sampledAt).count();
                            }
                        }
                    }
                    if (diagPositionSource[0] == 'e' && playerPositions) {
                        const auto sample = playerPositions->find(diagKey);
                        if (sample != playerPositions->end() &&
                            sample->second.entity == e.entity &&
                            SKJH_ClassHashCompatible(
                                sample->second.classHash, e.classHash) &&
                            renderDiagNow - sample->second.sampledAt <=
                                SKJH_PLAYER_POSITION_TTL) {
                            diagPosition = sample->second.position;
                            diagPositionSource = "player_property";
                            diagPositionAgeMs = std::chrono::duration_cast<
                                std::chrono::milliseconds>(
                                    renderDiagNow - sample->second.sampledAt).count();
                        }
                    }
                    if (diagPositionSource[0] == 'u') ++diagUnityRootCount;
                    if (diagPositionSource[0] == 'p') ++diagPlayerPropertyCount;
                    int validBones = 0, freshBones = 0;
                    for (int b = 0; b < BONE_COUNT; ++b) {
                        if (e.boneValid[b]) ++validBones;
                        if (SKJH_IsBoneFresh(e, b, renderDiagNow)) ++freshBones;
                    }
                    if (e.hasBones) ++diagPlayersWithBones;
                    if (freshBones >= SKJH_MIN_BONES_FOR_RENDER) ++diagPlayersWithFreshBones;
                    if (!firstPlayer) diag << ',';
                    firstPlayer = false;
                    diag << "\n      {\"id\": " << e.entityId
                         << ", \"pos\": [" << diagPosition.X << ',' << diagPosition.Y << ',' << diagPosition.Z << ']'
                         << ", \"entityPos\": [" << e.pos.X << ',' << e.pos.Y << ',' << e.pos.Z << ']'
                         << ", \"positionSource\": \"" << diagPositionSource << '"'
                         << ", \"positionAgeMs\": " << diagPositionAgeMs
                         << ", \"rootEntityDelta\": " << diagRootEntityDelta
                         << ", \"rootFrameDelta\": " << diagRootFrameDelta
                         << ", \"rootRetainedPasses\": " << diagRootRetainedPasses
                         << ", \"hasBones\": " << (e.hasBones ? "true" : "false")
                         << ", \"validBones\": " << validBones
                         << ", \"freshBones\": " << freshBones
                         << ", \"isBot\": " << (e.isBot ? "true" : "false")
                         << ", \"botKnown\": " << (e.botKnown ? "true" : "false")
                         << ", \"boneUpdatedAt\": " << std::chrono::duration_cast<std::chrono::milliseconds>(e.boneUpdatedAt.time_since_epoch()).count()
                         << ", \"firstBones\": [";
                    for (int b = 0; b < 3 && b < BONE_COUNT; ++b) {
                        if (b) diag << ',';
                        diag << "{\"name\": \"" << BoneNames[b] << '"'
                             << ", \"valid\": " << (e.boneValid[b] ? "true" : "false")
                             << ", \"pos\": [" << e.bones[b].X << ',' << e.bones[b].Y << ',' << e.bones[b].Z << "]}";
                    }
                    diag << "]}";
                }
            }
            diag << "\n    ]\n  },\n";
            // Type distribution and sample entities for debugging
            int typeCounts[SKJH_TYPE_COUNT] = {};
            if (diagData) {
                for (const auto& e : *diagData) {
                    if (e.type >= 0 && e.type < SKJH_TYPE_COUNT)
                        ++typeCounts[e.type];
                }
            }
            diag << "  \"typeDistribution\": {";
            const char* typeNames[] = {"UNKNOWN","PLAYER","MONSTER","PART","ORE","BOX","TERRITORY","TREE","VEHICLE","NPC","SYSTEM","LOOT","COLLECT","CORPSE"};
            for (int t = 0; t < SKJH_TYPE_COUNT; ++t) {
                if (t) diag << ',';
                diag << '"' << typeNames[t] << "\": " << typeCounts[t];
            }
            diag << "},\n";
            diag << "  \"sampleEntities\": [";
            int sampleCount = 0;
            if (diagData) {
                for (const auto& e : *diagData) {
                    if (sampleCount >= 10) break;
                    if (sampleCount) diag << ',';
                    diag << "\n    {\"id\": " << e.entityId
                         << ", \"type\": " << e.type
                         << ", \"class\": \"" << SKJH_JsonEscape(e.className) << '"'
                         << ", \"pos\": [" << e.pos.X << ',' << e.pos.Y << ',' << e.pos.Z << ']'
                         << ", \"entity\": \"" << SKJH_Hex(e.entity) << '"'
                         << ", \"klass\": \"" << SKJH_Hex(e.klass) << '"'
                         << "}";
                    ++sampleCount;
                }
            }
            diag << "\n  ],\n";
            diag << "  \"sdk\": {\"classNameOffset\": " << g_RuntimeOffsets.il2cppClassName
                 << ", \"classParentOffset\": " << g_RuntimeOffsets.il2cppClassParent
                 << ", \"staticFieldsOffset\": " << g_RuntimeOffsets.il2cppClassStaticFields
                 << ", \"mcTypeInfo\": \"" << SKJH_Hex(g_RuntimeOffsets.mcTypeInfo) << '"'
                 << ", \"entityManagerTypeInfo\": \"" << SKJH_Hex(g_RuntimeOffsets.entityManagerTypeInfo) << '"'
                 << ", \"klassCacheSize\": " << SKJH_GetKlassCacheSize()
                 << ", \"playerKlass\": \"" << SKJH_Hex(g_SKJH_PlayerKlass.load()) << '"'
                 << "},\n";
            diag << "  \"summary\": {\"players\": " << diagPlayerCount
                 << ", \"withBones\": " << diagPlayersWithBones
                 << ", \"withFreshBones\": " << diagPlayersWithFreshBones
                 << ", \"visibleCount\": " << g_VisibleCount
                 << ", \"unityRootCount\": " << diagUnityRootCount
                 << ", \"playerPropertyCount\": " << diagPlayerPropertyCount
                 << "}\n";
            diag << "}\n";
            diag.close();
        }
    }

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
            if (entity.type != SKJH_PLAYER)
                return position;
            const auto key = SKJH_GetEntitySnapshotKey(
                entity.entityId, entity.entity);
            if (playerRoots) {
                const auto root = playerRoots->find(key);
                if (root != playerRoots->end() &&
                    root->second.entity == entity.entity &&
                    SKJH_ClassHashCompatible(
                        root->second.classHash, entity.classHash) &&
                    positionNow - root->second.sampledAt <=
                        SKJH_PlayerRootDisplayWindow()) {
                    return root->second.position;
                }
            }
            if (playerPositions) {
                const auto sample = playerPositions->find(key);
                if (sample != playerPositions->end() &&
                    sample->second.entity == entity.entity &&
                    SKJH_ClassHashCompatible(
                        sample->second.classHash, entity.classHash) &&
                    positionNow - sample->second.sampledAt <=
                        SKJH_PLAYER_POSITION_TTL) {
                    return sample->second.position;
                }
            }
            return position;
        };
    static thread_local std::vector<SKJH_DrawCandidate> sortedEntities;
    sortedEntities.clear();
    if (sortedEntities.capacity() < data->size())
        sortedEntities.reserve(data->size());
    for (auto& e : *data) {
        if (SKJH_ShouldHideDeadActor(e)) continue;
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
            const bool skeletonEnabled =
                e.type == SKJH_PLAYER &&
                (e.botKnown
                    ? (e.isBot ? g_ShowBotSkeleton : g_ShowPlayerSkeleton)
                    : (g_ShowPlayerSkeleton || g_ShowBotSkeleton));
            if (skeletonEnabled && SKJH_HasAnyFreshBones(e, positionNow)) {
                std::array<FVector, BONE_COUNT> worldBones{};
                std::array<FVector2D, BONE_COUNT> screenBones{};
                std::array<bool, BONE_COUNT> worldValid{};
                std::array<bool, BONE_COUNT> screenValid{};
                FVector currentRoot{};
                bool currentRootValid = false;
                std::chrono::steady_clock::time_point currentRootSampledAt{};
                if (playerRoots) {
                    const auto root = playerRoots->find(
                        SKJH_GetEntitySnapshotKey(e.entityId, e.entity));
                    if (root != playerRoots->end() &&
                        root->second.entity == e.entity &&
                        SKJH_ClassHashCompatible(
                            root->second.classHash, e.classHash) &&
                        positionNow - root->second.sampledAt <=
                            SKJH_PlayerRootDisplayWindow()) {
                        currentRoot = root->second.position;
                        currentRootValid = SKJH_IsFiniteVector(currentRoot);
                        currentRootSampledAt = root->second.sampledAt;
                    }
                }

                for (int bone = 0; bone < BONE_COUNT; ++bone) {
                    if (!SKJH_IsBoneFresh(e, bone, positionNow)) continue;
                    worldBones[bone] = e.bones[bone];
                    // The fast root pass runs before the full pose pass. Never
                    // translate a newer pose with an older root sample; doing
                    // so makes the skeleton jump backward once per pass.
                    if (currentRootValid && e.boneSampleRootValid[bone] &&
                        currentRootSampledAt >= e.boneSampledAt[bone]) {
                        const FVector rootCompensation{
                            currentRoot.X - e.boneSampleRoots[bone].X,
                            currentRoot.Y - e.boneSampleRoots[bone].Y,
                            currentRoot.Z - e.boneSampleRoots[bone].Z};
                        const float compensationSq =
                            rootCompensation.X * rootCompensation.X +
                            rootCompensation.Y * rootCompensation.Y +
                            rootCompensation.Z * rootCompensation.Z;
                        if (SKJH_IsFiniteVector(rootCompensation) &&
                            compensationSq <= 400.0f) {
                            worldBones[bone].X += rootCompensation.X;
                            worldBones[bone].Y += rootCompensation.Y;
                            worldBones[bone].Z += rootCompensation.Z;
                        }
                    }
                    worldValid[bone] = SKJH_IsFiniteVector(worldBones[bone]);
                    screenValid[bone] = worldValid[bone] &&
                        SKJH_W2SUnclipped(worldBones[bone], cameraData,
                                          sw, sh, screenBones[bone]);
                }

                const auto worldDistanceSq = [&](int first, int second) {
                    const float x = worldBones[first].X - worldBones[second].X;
                    const float y = worldBones[first].Y - worldBones[second].Y;
                    const float z = worldBones[first].Z - worldBones[second].Z;
                    return x * x + y * y + z * z;
                };
                for (int index = 0; index < BoneConnectionCount; ++index) {
                    const int first = BoneConnections[index][0];
                    const int second = BoneConnections[index][1];
                    if (first < 0 || first >= BONE_COUNT || second < 0 ||
                        second >= BONE_COUNT || !worldValid[first] ||
                        !worldValid[second] || !screenValid[first] ||
                        !screenValid[second]) {
                        continue;
                    }
                    const float lengthSq = worldDistanceSq(first, second);
                    const bool shortTorsoSegment =
                        (first == BONE_HEAD && second == BONE_NECK) ||
                        (first == BONE_NECK && second == BONE_BODY) ||
                        (first == BONE_BODY && second == BONE_SPINE1) ||
                        (first == BONE_SPINE1 && second == BONE_SPINE) ||
                        (first == BONE_SPINE && second == BONE_PELVIS);
                    const float maximumLengthSq =
                        shortTorsoSegment ? 2.25f : 6.25f;
                    if (!std::isfinite(lengthSq) || lengthSq < 0.000025f ||
                        lengthSq > maximumLengthSq) {
                        continue;
                    }
                    FVector2D clippedFirst = screenBones[first];
                    FVector2D clippedSecond = screenBones[second];
                    if (!SKJH_ClipScreenLine(
                            clippedFirst, clippedSecond, sw, sh)) continue;
                    const ImVec2 firstPoint(clippedFirst.X, clippedFirst.Y);
                    const ImVec2 secondPoint(clippedSecond.X, clippedSecond.Y);
                    dl->AddLine(firstPoint, secondPoint, kOutline, 3.5f);
                    dl->AddLine(firstPoint, secondPoint, entityColor, 1.5f);
                }
            }

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
