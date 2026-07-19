#pragma once
/* Remote player GameObject lookup and real Unity Transform bone positions. */
#include "Mem.h"
#include "Offset.h"
#include "BoneEnum.h"
#include "SKJH_Entity.h"
#include "UnityTransform.h"
#include <algorithm>
#include <array>
#include <unordered_map>

struct SKJH_BoneData {
    FVector worldPos = {0, 0, 0};
    bool valid = false;
};

inline DWORD64 SKJH_GetMgrEntityGo() {
    const DWORD64 fields = SKJH_GetMcStaticFields();
    if (!fields) return 0;
    const DWORD64 manager = mem.Read<DWORD64>(fields + g_RuntimeOffsets.mcEntityGo);
    return Mem::IsUserAddress(manager) ? manager : 0;
}

inline std::unordered_map<int64_t, DWORD64> SKJH_ReadEntityGoMap(size_t limit = 20000) {
    std::unordered_map<int64_t, DWORD64> result;
    const DWORD64 manager = SKJH_GetMgrEntityGo();
    if (!manager) return result;
    const DWORD64 dictionary = mem.Read<DWORD64>(manager + g_RuntimeOffsets.mgrEntityGoGos);
    if (!Mem::IsUserAddress(dictionary)) return result;
    const DWORD64 entries = mem.Read<DWORD64>(dictionary + 0x18);
    const int32_t count = mem.Read<int32_t>(dictionary + 0x20);
    if (!Mem::IsUserAddress(entries) || count <= 0 || count > static_cast<int32_t>(limit))
        return result;
    const int32_t capacity = mem.Read<int32_t>(entries + 0x18);
    if (capacity <= 0 || capacity > static_cast<int32_t>(limit * 2)) return result;
    const int32_t scanCount = count < capacity ? count : capacity;
    result.reserve(scanCount);
    for (int32_t index = 0; index < scanCount; ++index) {
        const DWORD64 entry = entries + 0x20 + static_cast<DWORD64>(index) * 0x18;
        if (mem.Read<int32_t>(entry) < 0) continue;
        const int64_t entityId = mem.Read<int64_t>(entry + 0x08);
        const DWORD64 entityGo = mem.Read<DWORD64>(entry + 0x10);
        if (entityId && Mem::IsUserAddress(entityGo)) result.emplace(entityId, entityGo);
    }
    return result;
}

inline bool SKJH_KlassIsOrDerivesFrom(DWORD64 klass, const char* expected) {
    for (int depth = 0; depth < 10 && Mem::IsUserAddress(klass); ++depth) {
        if (SKJH_ReadKlassName(klass) == expected) return true;
        const DWORD64 parent = mem.Read<DWORD64>(klass + g_RuntimeOffsets.il2cppClassParent);
        if (parent == klass) break;
        klass = parent;
    }
    return false;
}

inline DWORD64 SKJH_PlayerBoneOffset(int bone) {
    switch (bone) {
        case BONE_HEAD:       return g_RuntimeOffsets.clientPlayerHead;
        case BONE_BODY:       return g_RuntimeOffsets.clientPlayerBody;
        case BONE_SPINE:      return g_RuntimeOffsets.clientPlayerSpine;
        case BONE_LEFT_FOOT:  return g_RuntimeOffsets.clientPlayerLeftFoot;
        case BONE_RIGHT_FOOT: return g_RuntimeOffsets.clientPlayerRightFoot;
        case BONE_NECK:       return g_RuntimeOffsets.clientPlayerNeck;
        default:              return 0;
    }
}

inline int SKJH_ReadPlayerBones(DWORD64 playerGo, DWORD64 entity,
                                const FVector& entityPosition,
                                SKJH_BoneData bones[BONE_COUNT]) {
    for (int i = 0; i < BONE_COUNT; ++i) bones[i] = {};
    if (!Mem::IsUserAddress(playerGo) || !Mem::IsUserAddress(entity)) return 0;
    const DWORD64 goKlass = mem.Read<DWORD64>(playerGo);
    if (!SKJH_KlassIsOrDerivesFrom(goKlass, "ClientPlayerGo")) return 0;
    DWORD64 backReference = 0;
    if (!mem.Read(playerGo + g_RuntimeOffsets.basePlayerGoEntity,
                  &backReference, sizeof(backReference)) ||
        !Mem::IsUserAddress(backReference)) return 0;
    if (backReference != entity) {
        int64_t expectedId = 0;
        int64_t backReferenceId = 0;
        if (!mem.Read(entity + g_RuntimeOffsets.entityEntityId,
                      &expectedId, sizeof(expectedId)) ||
            !mem.Read(backReference + g_RuntimeOffsets.entityEntityId,
                      &backReferenceId, sizeof(backReferenceId))) return 0;
        if (!expectedId || expectedId != backReferenceId) return 0;
    }

    int validCount = 0;
    for (int bone = 0; bone < BONE_COUNT; ++bone) {
        const DWORD64 managedTransform = mem.Read<DWORD64>(
            playerGo + SKJH_PlayerBoneOffset(bone));
        SKJH_UnityTransformResult transform = SKJH_ReadUnityTransform(managedTransform);
        if (!transform.valid) transform = SKJH_ReadUnityTransform(managedTransform);
        if (!transform.valid) continue;
        const float dx = transform.position.X - entityPosition.X;
        const float dy = transform.position.Y - entityPosition.Y;
        const float dz = transform.position.Z - entityPosition.Z;
        const float distanceSq = dx*dx + dy*dy + dz*dz;
        // Network entity properties can lag behind the interpolated GameObject
        // by tens of metres. The verified PlayerEntity back-reference is the
        // primary identity check; this wider bound only rejects wild pointers.
        if (!std::isfinite(distanceSq) || distanceSq > 40000.0f) continue;
        bones[bone].worldPos = transform.position;
        bones[bone].valid = true;
        ++validCount;
    }

    // A valid humanoid sample must include a torso chain and at least one foot.
    if (!bones[BONE_HEAD].valid || !bones[BONE_NECK].valid ||
        !bones[BONE_BODY].valid ||
        (!bones[BONE_LEFT_FOOT].valid && !bones[BONE_RIGHT_FOOT].valid)) {
        return 0;
    }
    // Validate the recovered skeleton as one compact humanoid hierarchy. This
    // catches corrupt Transform layouts without depending on a lagging entity
    // position.
    float maximumSeparationSq = 0.0f;
    for (int first = 0; first < BONE_COUNT; ++first) {
        if (!bones[first].valid) continue;
        for (int second = first + 1; second < BONE_COUNT; ++second) {
            if (!bones[second].valid) continue;
            const float dx = bones[first].worldPos.X - bones[second].worldPos.X;
            const float dy = bones[first].worldPos.Y - bones[second].worldPos.Y;
            const float dz = bones[first].worldPos.Z - bones[second].worldPos.Z;
            maximumSeparationSq = (std::max)(maximumSeparationSq,
                                              dx*dx + dy*dy + dz*dz);
        }
    }
    if (!std::isfinite(maximumSeparationSq) || maximumSeparationSq > 25.0f)
        return 0;
    return validCount;
}
