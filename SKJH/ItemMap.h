#pragma once
#include <cstring>
/*
 * ItemMap.h — SKJH (失控进化) 实体类型映射
 * 基于 DogeDebuggerMCP 实时内存分析
 * 所有实体名称统一中文呈现
 */

// ===================== 实体类型枚举 =====================
enum SKJH_EntityType {
    SKJH_UNKNOWN   = 0,
    SKJH_PLAYER    = 1,   // 玩家
    SKJH_MONSTER   = 2,   // 怪物 / 野生动物
    SKJH_PART      = 3,   // 建筑部件
    SKJH_ORE       = 4,   // 矿石资源
    SKJH_BOX       = 5,   // 物资箱 / 空投箱
    SKJH_TERRITORY = 6,   // 领地
    SKJH_TREE      = 7,   // 树木
    SKJH_VEHICLE   = 8,   // 载具
    SKJH_NPC       = 9,   // NPC / 商店
    SKJH_SYSTEM    = 10,  // 系统管理实体
    SKJH_LOOT      = 11,  // 地面掉落物 / 可拾取残骸
    SKJH_COLLECT   = 12,  // 可采集物
    // Append new values to preserve persisted TypeEnabled/TypeMaxDist indexes.
    SKJH_CORPSE    = 13,  // 动物/玩家尸体物资
};

// ===================== 实体类型总数 =====================
constexpr int SKJH_TYPE_COUNT = 14;

// ===================== 中文名称 =====================
inline const char* SKJH_GetEntityDisplayName(int type) {
    switch (type) {
        case SKJH_PLAYER:    return u8"玩家";
        case SKJH_MONSTER:   return u8"怪物";
        case SKJH_PART:      return u8"建筑部件";
        case SKJH_ORE:       return u8"矿石";
        case SKJH_BOX:       return u8"物资箱";
        case SKJH_TERRITORY: return u8"领地";
        case SKJH_TREE:      return u8"树木";
        case SKJH_VEHICLE:   return u8"载具";
        case SKJH_NPC:       return u8"商店NPC";
        case SKJH_SYSTEM:    return u8"系统实体";
        case SKJH_LOOT:      return u8"地面物资";
        case SKJH_COLLECT:   return u8"采集物";
        case SKJH_CORPSE:    return u8"尸体物资";
        default:             return u8"未知实体";
    }
}

// ===================== 简称 (ESP标签用) =====================
inline const char* SKJH_GetEntityShortName(int type) {
    switch (type) {
        case SKJH_PLAYER:    return u8"玩家";
        case SKJH_MONSTER:   return u8"怪";
        case SKJH_PART:      return u8"部件";
        case SKJH_ORE:       return u8"矿";
        case SKJH_BOX:       return u8"箱";
        case SKJH_TERRITORY: return u8"领地";
        case SKJH_TREE:      return u8"树";
        case SKJH_VEHICLE:   return u8"车";
        case SKJH_NPC:       return u8"NPC";
        case SKJH_SYSTEM:    return u8"系统";
        case SKJH_LOOT:      return u8"物资";
        case SKJH_COLLECT:   return u8"采集";
        case SKJH_CORPSE:    return u8"尸体";
        default:             return u8"?";
    }
}

// ===================== 默认颜色 (R,G,B,A 0~1) =====================
inline void SKJH_GetEntityColor(int type, float out[4]) {
    switch (type) {
        case SKJH_PLAYER:    // 红色 — 最高威胁
            out[0]=1.0f; out[1]=0.15f; out[2]=0.15f; out[3]=1.0f;
            break;
        case SKJH_MONSTER:   // 橙色 — 中等威胁
            out[0]=1.0f; out[1]=0.5f;  out[2]=0.0f;  out[3]=1.0f;
            break;
        case SKJH_PART:      // 绿色 — 建筑物
            out[0]=0.2f; out[1]=0.9f;  out[2]=0.3f;  out[3]=1.0f;
            break;
        case SKJH_ORE:       // 青色 — 矿石资源
            out[0]=0.0f; out[1]=0.8f;  out[2]=1.0f;  out[3]=1.0f;
            break;
        case SKJH_BOX:       // 金色 — 物资箱
            out[0]=1.0f; out[1]=0.8f;  out[2]=0.0f;  out[3]=1.0f;
            break;
        case SKJH_TERRITORY: // 紫色 — 领地
            out[0]=0.7f; out[1]=0.3f;  out[2]=1.0f;  out[3]=1.0f;
            break;
        case SKJH_TREE:      // 暗绿 — 树木
            out[0]=0.1f; out[1]=0.5f;  out[2]=0.2f;  out[3]=1.0f;
            break;
        case SKJH_VEHICLE:   // 浅蓝 — 载具
            out[0]=0.4f; out[1]=0.8f;  out[2]=1.0f;  out[3]=1.0f;
            break;
        case SKJH_NPC:       // 粉色 — NPC
            out[0]=1.0f; out[1]=0.4f;  out[2]=0.8f;  out[3]=1.0f;
            break;
        case SKJH_SYSTEM:    // 灰色 — 系统实体
            out[0]=0.4f; out[1]=0.4f;  out[2]=0.4f;  out[3]=0.6f;
            break;
        case SKJH_LOOT:      // 亮黄 — 地面物资
            out[0]=1.0f; out[1]=0.95f; out[2]=0.35f; out[3]=1.0f;
            break;
        case SKJH_COLLECT:   // 青绿 — 可采集物
            out[0]=0.2f; out[1]=1.0f;  out[2]=0.7f;  out[3]=1.0f;
            break;
        case SKJH_CORPSE:    // 玫红 — 尸体物资
            out[0]=0.95f; out[1]=0.45f; out[2]=0.55f; out[3]=1.0f;
            break;
        default:             // 深灰 — 未知
            out[0]=0.5f; out[1]=0.5f;  out[2]=0.5f;  out[3]=0.7f;
            break;
    }
}

// ===================== IL2CPP 类名 → 类型映射 =====================
// 用于运行时通过 klass 指针读取类名后匹配类型
inline int SKJH_ClassNameToType(const char* name) {
    if (!name || !name[0]) return SKJH_UNKNOWN;
    if (strcmp(name, "DayNightShiftEntity") == 0)
        return SKJH_SYSTEM;
    if (strstr(name, "AirdropControllerEntity") ||
        strstr(name, "TreeControlEntity") || strstr(name, "ShopControlEntity"))
        return SKJH_SYSTEM;
    // 先匹配具体物资和载具，避免父类/通用后缀误分类。
    if (strstr(name, "SceneItemEntity") || strstr(name, "PartDebrisEntity") ||
        strstr(name, "StorageDebrisEntity") || strstr(name, "ThrownEntity"))
        return SKJH_LOOT;
    if (strstr(name, "CollectableEntity") || strstr(name, "MushroomEntity"))
        return SKJH_COLLECT;
    if (strstr(name, "CorpseEntity"))
        return SKJH_CORPSE;
    if (strstr(name, "BoxEntity") || strstr(name, "AirdropEntity") ||
        strstr(name, "SafetyBoxEntity") || strstr(name, "TempCofferEntity"))
        return SKJH_BOX;
    if (strstr(name, "VehicleEntity") || strstr(name, "BaseVehicleEntity") ||
        strstr(name, "HorseEntity") || strstr(name, "CarEntity") ||
        strstr(name, "KatyushaEntity") || strstr(name, "TrainCarEntity") ||
        strstr(name, "ZiplineEntity") ||
        strstr(name, "ParachuteEntity") || strstr(name, "AirDropPlaneEntity"))
        return SKJH_VEHICLE;
    if (strstr(name, "PlayerEntity"))    return SKJH_PLAYER;
    if (strstr(name, "MonsterEntity"))   return SKJH_MONSTER;
    if (strstr(name, "PartEntity") || strstr(name, "ArrowTargetEntity") ||
        strstr(name, "IOEntity") || strstr(name, "ElevatorEntity") ||
        strstr(name, "CarshredderEntity") || strstr(name, "CaveLiftEntity") ||
        strstr(name, "InteractionEntity") || strstr(name, "TargetEntity") ||
        strstr(name, "TrainBarricadeEntity") || strstr(name, "TrapEntity") ||
        strstr(name, "DigEntity"))
        return SKJH_PART;
    if (strstr(name, "OreEntity"))       return SKJH_ORE;
    if (strstr(name, "TerritoryEntity")) return SKJH_TERRITORY;
    if (strstr(name, "TreeEntity")) return SKJH_TREE;
    if (strstr(name, "NPCEntity") || strstr(name, "ShopEntity"))
        return SKJH_NPC;
    if (strstr(name, "MonumentEntity") || strstr(name, "WildEntity") ||
        strstr(name, "BeeBuzzGroupEntity") || strstr(name, "BonusRocketEntity") ||
        strstr(name, "BulletEntity") || strstr(name, "DecalEntity") ||
        strstr(name, "EffectEntity") || strstr(name, "MagicFieldEntity") ||
        strstr(name, "MissileEntity") || strstr(name, "MonumentGroupEntity") ||
        strstr(name, "ObserverEntity") || strstr(name, "PlunderReportEntity") ||
        strstr(name, "RecordCheckEntity") || strstr(name, "RpcEntity") ||
        strstr(name, "RuleGraphDebugEntity") || strstr(name, "ServerInstanceEntity") ||
        strstr(name, "SpawnPersistentEntity") || strstr(name, "SwarmAIEntity") ||
        strstr(name, "TeamEntity") || strstr(name, "UgcLevelMgrEntity"))
        return SKJH_SYSTEM;
    // 系统管理类
    if (strstr(name, "Manager") || strstr(name, "Process") ||
        strstr(name, "Global")   || strstr(name, "Control") ||
        strstr(name, "Electric") || strstr(name, "Statistic"))
        return SKJH_SYSTEM;
    return SKJH_UNKNOWN;
}

// 具体实体标签。分类决定颜色/开关，标签用于区分同类物资。
inline const char* SKJH_GetEntityClassLabel(const char* name, int type) {
    if (!name || !name[0]) return SKJH_GetEntityShortName(type);
    if (strstr(name, "AirdropController")) return u8"空投控制器";
    if (strstr(name, "AirDropPlane"))  return u8"空投飞机";
    if (strstr(name, "Airdrop"))       return u8"空投物资";
    if (strstr(name, "SafetyBox"))     return u8"保险箱";
    if (strstr(name, "TempCoffer"))    return u8"临时储物箱";
    if (strstr(name, "Corpse"))        return u8"尸体物资";
    if (strstr(name, "StorageDebris")) return u8"残骸物资";
    if (strstr(name, "SceneItem"))     return u8"地面物资";
    if (strstr(name, "Collectable"))   return u8"可采集物";
    if (strstr(name, "Mushroom"))      return u8"蘑菇";
    if (strstr(name, "Ore"))           return u8"矿石";
    if (strstr(name, "Horse"))         return u8"马匹";
    if (strstr(name, "Katyusha"))      return u8"火箭载具";
    if (strstr(name, "TrainCar"))      return u8"列车";
    if (strstr(name, "ModularCar"))    return u8"模块载具";
    if (strstr(name, "Parachute"))     return u8"降落伞";
    if (strstr(name, "Zipline"))       return u8"滑索";
    if (strstr(name, "Elevator") || strstr(name, "CaveLift")) return u8"电梯";
    if (strstr(name, "Carshredder"))   return u8"碎车机";
    if (strstr(name, "TrainBarricade"))return u8"列车路障";
    if (strstr(name, "Trap"))          return u8"陷阱";
    if (strstr(name, "Target"))        return u8"靶标";
    if (strstr(name, "Interaction"))   return u8"交互设施";
    if (strstr(name, "Thrown"))        return u8"可拾取投掷物";
    if (strstr(name, "Shop"))          return u8"商店";
    if (strstr(name, "NPC"))           return u8"NPC";
    if (strstr(name, "Monument"))      return u8"地标";
    if (strstr(name, "Wild"))          return u8"野外区域";
    return SKJH_GetEntityShortName(type);
}

inline bool SKJH_IsLootType(int type) {
    return type == SKJH_BOX || type == SKJH_LOOT ||
           type == SKJH_COLLECT || type == SKJH_ORE ||
           type == SKJH_CORPSE;
}

// ===================== 默认显示距离 (米) =====================
inline int SKJH_GetDefaultMaxDist(int type) {
    switch (type) {
        case SKJH_PLAYER:    return 500;   // 玩家远距离可见
        case SKJH_MONSTER:   return 300;
        case SKJH_BOX:       return 200;   // 物资箱中距离
        case SKJH_ORE:       return 150;   // 矿石中距离
        case SKJH_PART:      return 200;
        case SKJH_VEHICLE:   return 400;
        case SKJH_TREE:      return 100;   // 树木近距离
        case SKJH_TERRITORY: return 300;
        case SKJH_NPC:       return 200;
        case SKJH_SYSTEM:    return 50;    // 系统实体不显示
        case SKJH_LOOT:      return 200;
        case SKJH_COLLECT:   return 150;
        case SKJH_CORPSE:    return 200;
        default:             return 200;
    }
}

// ===================== 是否显示骨骼 =====================
inline bool SKJH_HasBones(int type) {
    // MonsterGo exposes an Animator rather than ClientPlayerGo's fixed bone
    // fields; it needs a separate per-model schema.
    return type == SKJH_PLAYER;
}

// ===================== 是否显示血条 =====================
inline bool SKJH_HasHealth(int type) {
    return type == SKJH_PLAYER || type == SKJH_MONSTER || type == SKJH_PART;
}
