#pragma once
/* Remote player GameObject lookup and real Unity Transform bone positions. */
#include "Mem.h"
#include "Offset.h"
#include "BoneEnum.h"
#include "SKJH_Entity.h"
#include "UnityTransform.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct SKJH_BoneData {
    FVector worldPos = {0, 0, 0};
    bool valid = false;
};

inline DWORD64 SKJH_GetMgrEntityGo() {
    const DWORD64 fields = SKJH_GetMcStaticFields();
    if (!fields) return 0;
    DWORD64 manager = mem.ReadMeta<DWORD64>(fields + g_RuntimeOffsets.mcEntityGo);
    if (!Mem::IsUserAddress(manager))
        manager = mem.Read<DWORD64>(fields + g_RuntimeOffsets.mcEntityGo);
    return Mem::IsUserAddress(manager) ? manager : 0;
}

inline std::unordered_map<int64_t, DWORD64> SKJH_ReadEntityGoMap(size_t limit = 20000) {
    std::unordered_map<int64_t, DWORD64> result;
    const DWORD64 manager = SKJH_GetMgrEntityGo();
    if (!manager) return result;
    DWORD64 dictionary = mem.ReadMeta<DWORD64>(manager + g_RuntimeOffsets.mgrEntityGoGos);
    if (!Mem::IsUserAddress(dictionary))
        dictionary = mem.Read<DWORD64>(manager + g_RuntimeOffsets.mgrEntityGoGos);
    if (!Mem::IsUserAddress(dictionary)) return result;
    DWORD64 entries = mem.ReadMeta<DWORD64>(dictionary + 0x18);
    if (!Mem::IsUserAddress(entries))
        entries = mem.Read<DWORD64>(dictionary + 0x18);
    int32_t count = mem.ReadMeta<int32_t>(dictionary + 0x20);
    if (count <= 0 || count > static_cast<int32_t>(limit))
        count = mem.Read<int32_t>(dictionary + 0x20);
    if (!Mem::IsUserAddress(entries) || count <= 0 || count > static_cast<int32_t>(limit))
        return result;
    const int32_t capacity = mem.Read<int32_t>(entries + 0x18);
    if (capacity <= 0 || capacity > static_cast<int32_t>(limit * 2)) return result;
    const int32_t scanCount = count < capacity ? count : capacity;
    result.reserve(scanCount);
    constexpr size_t kEntrySize = 0x18;
    std::vector<unsigned char> rawEntries(
        static_cast<size_t>(scanCount) * kEntrySize);
    const DWORD64 firstEntry = entries + 0x20;
    const DWORD byteCount = static_cast<DWORD>(rawEntries.size());
    if (!mem.Read(firstEntry, rawEntries.data(), byteCount) &&
        !mem.ReadMeta(firstEntry, rawEntries.data(), byteCount)) {
        return result;
    }
    for (int32_t index = 0; index < scanCount; ++index) {
        const unsigned char* entry = rawEntries.data() +
            static_cast<size_t>(index) * kEntrySize;
        int32_t hashCode = -1;
        int64_t entityId = 0;
        DWORD64 entityGo = 0;
        std::memcpy(&hashCode, entry, sizeof(hashCode));
        std::memcpy(&entityId, entry + 0x08, sizeof(entityId));
        std::memcpy(&entityGo, entry + 0x10, sizeof(entityGo));
        if (hashCode < 0) continue;
        if (entityId && Mem::IsUserAddress(entityGo)) result.emplace(entityId, entityGo);
    }
    return result;
}

inline bool SKJH_KlassIsOrDerivesFrom(DWORD64 klass, const char* expected) {
    for (int depth = 0; depth < 10 && Mem::IsUserAddress(klass); ++depth) {
        if (SKJH_ReadKlassName(klass) == expected) return true;
        DWORD64 parent = mem.ReadMeta<DWORD64>(klass + g_RuntimeOffsets.il2cppClassParent);
        if (!Mem::IsUserAddress(parent))
            parent = mem.Read<DWORD64>(klass + g_RuntimeOffsets.il2cppClassParent);
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

inline bool SKJH_HasMatchingPlayerEntity(DWORD64 playerGo, DWORD64 entity) {
    if (!Mem::IsUserAddress(playerGo) || !Mem::IsUserAddress(entity)) return false;
    DWORD64 backReference = 0;
    // Use ReadMeta (VMM-cached) for structural back-reference — this pointer
    // is stable and NOCACHE reads fail intermittently under bus contention.
    if (!mem.ReadMeta(playerGo + g_RuntimeOffsets.basePlayerGoEntity,
                  &backReference, sizeof(backReference))) {
        if (!mem.Read(playerGo + g_RuntimeOffsets.basePlayerGoEntity,
                      &backReference, sizeof(backReference))) return false;
    }
    if (!Mem::IsUserAddress(backReference)) return false;
    if (backReference == entity) return true;
    int64_t expectedId = 0;
    int64_t backReferenceId = 0;
    return mem.Read(entity + g_RuntimeOffsets.entityEntityId,
                    &expectedId, sizeof(expectedId)) &&
        mem.Read(backReference + g_RuntimeOffsets.entityEntityId,
                 &backReferenceId, sizeof(backReferenceId)) &&
        expectedId && expectedId == backReferenceId;
}

inline bool SKJH_IsMatchingPlayerGo(DWORD64 playerGo, DWORD64 entity) {
    if (!SKJH_HasMatchingPlayerEntity(playerGo, entity)) return false;
    // Native class-name pages can be paged out or return a torn value while
    // the live object and its Transform wrappers remain resident. The entity
    // back-reference plus three independent real Transform anchors identifies
    // the matching player without making pose sampling depend on metadata.
    const uint32_t anchorOffsets[] = {
        g_RuntimeOffsets.basePlayerGoRootBone,
        g_RuntimeOffsets.clientPlayerHead,
        g_RuntimeOffsets.clientPlayerNeck,
    };
    for (const uint32_t offset : anchorOffsets) {
        DWORD64 managed = mem.ReadMeta<DWORD64>(playerGo + offset);
        if (!Mem::IsUserAddress(managed))
            managed = mem.Read<DWORD64>(playerGo + offset);
        if (!Mem::IsUserAddress(managed)) return false;
        DWORD64 cached = mem.ReadMeta<DWORD64>(
            managed + g_RuntimeOffsets.unityObjectCachedPtr);
        if (!Mem::IsUserAddress(cached))
            cached = mem.Read<DWORD64>(
                managed + g_RuntimeOffsets.unityObjectCachedPtr);
        if (!Mem::IsUserAddress(cached)) return false;
    }
    return true;
}

enum class SKJH_SkeletonSourceKind : uint8_t {
    SerializeTree = 0,
    OriginalPointCache = 1,
};

struct SKJH_PlayerSkeletonBinding {
    DWORD64 playerGo = 0;
    DWORD64 controller = 0;
    DWORD64 targetRootPoint = 0;
    DWORD64 serializeTree = 0;
    DWORD64 nodesArray = 0;
    DWORD64 transformsArray = 0;
    DWORD64 transformData = 0;
    DWORD64 localTransforms = 0;
    DWORD64 parentIndices = 0;
    // The current SDK can leave serializeBoneTree empty while the native
    // UnsafeBoneSkeletonTree and pointCache are already populated. Keep the
    // source identity in the binding so a transient rebuild invalidates the
    // whole pose instead of silently mixing generations.
    SKJH_SkeletonSourceKind sourceKind =
        SKJH_SkeletonSourceKind::SerializeTree;
    DWORD64 originalTree = 0;
    DWORD64 pointCache = 0;
    DWORD64 originalNodeData = 0;
    DWORD64 originalTransformIdData = 0;
    int32_t originalNodeCount = 0;
    int32_t originalTransformIdCount = 0;
    uint32_t nativeInstanceIdOffset = 0;
    std::array<int32_t, BONE_COUNT> originalBoneIndices{};
    // Source layout coordinates captured when the binding is resolved.  The
    // game may recycle the backing arrays in place during a small update, so
    // pointer/count checks alone are insufficient to detect a new generation.
    std::array<int32_t, BONE_COUNT> sourceNodeArrayIndices{};
    std::array<int32_t, BONE_COUNT> sourceBoneIndices{};
    std::array<int32_t, BONE_COUNT> sourceParentIndices{};
    std::array<DWORD64, BONE_COUNT> sourceNodePointers{};
    mutable ULONGLONG lastGenerationCheckMs = 0;
    uint32_t targetRootPointOffset = 0;
    bool targetRootPointFromPlayerGo = false;
    std::array<DWORD64, BONE_COUNT> managedTransforms{};
    std::array<int32_t, BONE_COUNT> transformIndices{};
    std::vector<int32_t> hierarchyIndices;
    std::unordered_map<int32_t, int32_t> parentByIndex;
    bool valid = false;
};

inline thread_local const char* g_SKJH_SkeletonBindingStage = "not_attempted";
inline thread_local std::array<const char*, 3>
    g_SKJH_SkeletonCandidateStages{{
        "not_attempted", "not_attempted", "not_attempted"}};

// Explicit bone-probe diagnostics only. Production rendering never reads or
// publishes these fields; they make DMA container-layout failures actionable
// without printing runtime data during normal startup.
struct SKJH_SkeletonCandidateProbe {
    DWORD64 targetRootPoint = 0;
    DWORD64 pointCache = 0;
    DWORD64 originalTree = 0;
    DWORD64 keyArray = 0;
    DWORD64 valueArray = 0;
    DWORD64 nodeData = 0;
    DWORD64 transformIdData = 0;
    int32_t keyCount = 0;
    int32_t valueCount = 0;
    int32_t nodeCount = 0;
    int32_t transformIdCount = 0;
    std::array<int32_t, 16> transformIdSample{};
    int32_t transformIdSampleCount = 0;
    int32_t duplicateTransformId = 0;
    int32_t duplicateTransformIdIndex = -1;
    int32_t duplicateTransformIdFirstIndex = -1;
    uint32_t selectedInstanceIdOffset = 0;
    int32_t selectedInstanceIdMatches = -1;
    int32_t originalRootInstanceId = 0;
    int32_t selectedRootInstanceId = 0;
    int32_t parentMismatchFirst = -1;
    int32_t parentMismatchSecond = -1;
    int32_t parentMismatchOriginalFirst = -1;
    int32_t parentMismatchOriginalSecond = -1;
    int32_t parentMismatchNativeFirst = -1;
    int32_t parentMismatchNativeSecond = -1;
    // ParentIndex has appeared as either a node-array index or a compact
    // BoneIndex in different generated tree versions. Probe both mappings and
    // publish the selected interpretation only after an exact match with the
    // oriented humanoid topology; Unity's native parent graph is diagnostic
    // because TransformAccess can flatten animated children.
    int32_t parentInterpretation = -1; // 0=node index, 1=bone index
    // These two counts compare each candidate map with the explicit humanoid
    // child->parent topology. parentNativeMismatchCount is a separate
    // diagnostic for Unity's (often flattened) TransformAccess graph.
    int32_t parentNodeIndexMismatchCount = -1;
    int32_t parentBoneIndexMismatchCount = -1;
    int32_t parentNativeMismatchCount = -1;
    int32_t parentRelationsCompared = 0;
    std::array<int32_t, BONE_COUNT> namedOriginalIndices{};
    std::array<int32_t, BONE_COUNT> namedNodeIndexParents{};
    std::array<int32_t, BONE_COUNT> namedBoneIndexParents{};
    std::array<int32_t, BONE_COUNT> namedNativeIndices{};
    std::array<int32_t, BONE_COUNT> namedNativeParents{};
};
inline thread_local SKJH_SkeletonCandidateProbe g_SKJH_SkeletonCandidateProbe{};

inline const SKJH_SkeletonCandidateProbe&
SKJH_GetSkeletonCandidateProbe() {
    return g_SKJH_SkeletonCandidateProbe;
}

inline const char* SKJH_GetSkeletonBindingStage() {
    return g_SKJH_SkeletonBindingStage;
}

inline const std::array<const char*, 3>& SKJH_GetSkeletonCandidateStages() {
    return g_SKJH_SkeletonCandidateStages;
}

inline bool SKJH_ReadSkeletonString(DWORD64 stringObject,
                                    std::string& output,
                                    int32_t maximumLength = 512) {
    output.clear();
    if (!Mem::IsUserAddress(stringObject)) return false;
    int32_t length = 0;
    if (!mem.Read(stringObject + 0x10, &length, sizeof(length)) ||
        length <= 0 || length > maximumLength) {
        return false;
    }
    std::vector<char16_t> characters(static_cast<size_t>(length));
    if (!mem.Read(stringObject + 0x14, characters.data(),
                  static_cast<DWORD>(characters.size() * sizeof(char16_t)))) {
        return false;
    }
    output.reserve(characters.size());
    for (const char16_t character : characters) {
        // All supported skeleton aliases are ASCII even though IL2CPP stores
        // the source name as UTF-16.
        if (!character || character > 0x7f) return false;
        output.push_back(static_cast<char>(character));
    }
    return true;
}

inline std::string SKJH_NormalizeSkeletonBoneName(const std::string& source) {
    const size_t separator = source.find_last_of("/\\");
    const size_t begin = separator == std::string::npos ? 0 : separator + 1;
    std::string normalized;
    normalized.reserve(source.size() - begin);
    for (size_t index = begin; index < source.size(); ++index) {
        const unsigned char character =
            static_cast<unsigned char>(source[index]);
        if (std::isalnum(character))
            normalized.push_back(static_cast<char>(std::tolower(character)));
    }
    if (normalized.rfind("bip001", 0) == 0)
        normalized.erase(0, 6);
    else if (normalized.rfind("bip01", 0) == 0)
        normalized.erase(0, 5);
    return normalized;
}

inline int SKJH_BoneIndexFromTreeName(const std::string& source) {
    const std::string name = SKJH_NormalizeSkeletonBoneName(source);
    struct BoneAlias {
        const char* name;
        int bone;
    };
    static constexpr BoneAlias aliases[] = {
        {"head", BONE_HEAD},
        {"neck", BONE_NECK},
        {"spine2", BONE_BODY},
        {"upperchest", BONE_BODY},
        {"spine", BONE_SPINE},
        {"pelvis", BONE_PELVIS},
        {"hips", BONE_PELVIS},
        {"spine1", BONE_SPINE1},

        {"lclavicle", BONE_LEFT_CLAVICLE},
        {"leftclavicle", BONE_LEFT_CLAVICLE},
        {"leftshoulder", BONE_LEFT_CLAVICLE},
        {"lupperarm", BONE_LEFT_UPPER_ARM},
        {"leftupperarm", BONE_LEFT_UPPER_ARM},
        {"lforearm", BONE_LEFT_FOREARM},
        {"leftforearm", BONE_LEFT_FOREARM},
        {"llowerarm", BONE_LEFT_FOREARM},
        {"lefthand", BONE_LEFT_HAND},
        {"lhand", BONE_LEFT_HAND},

        {"rclavicle", BONE_RIGHT_CLAVICLE},
        {"rightclavicle", BONE_RIGHT_CLAVICLE},
        {"rightshoulder", BONE_RIGHT_CLAVICLE},
        {"rupperarm", BONE_RIGHT_UPPER_ARM},
        {"rightupperarm", BONE_RIGHT_UPPER_ARM},
        {"rforearm", BONE_RIGHT_FOREARM},
        {"rightforearm", BONE_RIGHT_FOREARM},
        {"rlowerarm", BONE_RIGHT_FOREARM},
        {"righthand", BONE_RIGHT_HAND},
        {"rhand", BONE_RIGHT_HAND},

        {"lthigh", BONE_LEFT_THIGH},
        {"leftthigh", BONE_LEFT_THIGH},
        {"leftupperleg", BONE_LEFT_THIGH},
        {"lcalf", BONE_LEFT_CALF},
        {"leftcalf", BONE_LEFT_CALF},
        {"leftlowerleg", BONE_LEFT_CALF},
        {"lfoot", BONE_LEFT_FOOT},
        {"leftfoot", BONE_LEFT_FOOT},

        {"rthigh", BONE_RIGHT_THIGH},
        {"rightthigh", BONE_RIGHT_THIGH},
        {"rightupperleg", BONE_RIGHT_THIGH},
        {"rcalf", BONE_RIGHT_CALF},
        {"rightcalf", BONE_RIGHT_CALF},
        {"rightlowerleg", BONE_RIGHT_CALF},
        {"rfoot", BONE_RIGHT_FOOT},
        {"rightfoot", BONE_RIGHT_FOOT},
    };
    for (const auto& alias : aliases)
        if (name == alias.name) return alias.bone;
    return -1;
}

inline bool SKJH_ReadNativeTransformIdentity(DWORD64 managedTransform,
                                             DWORD64& nativeTransform,
                                             DWORD64& transformData,
                                             int32_t& transformIndex) {
    nativeTransform = 0;
    transformData = 0;
    transformIndex = -1;
    if (!Mem::IsUserAddress(managedTransform)) return false;
    nativeTransform = mem.Read<DWORD64>(
        managedTransform + g_RuntimeOffsets.unityObjectCachedPtr);
    if (!Mem::IsUserAddress(nativeTransform)) return false;
    transformData = mem.Read<DWORD64>(
        nativeTransform + g_RuntimeOffsets.unityNativeData);
    return Mem::IsUserAddress(transformData) &&
        mem.Read(nativeTransform + g_RuntimeOffsets.unityNativeIndex,
                 &transformIndex, sizeof(transformIndex)) &&
        transformIndex >= 0 && transformIndex <= 1000000;
}

inline bool SKJH_SameNativeTransform(DWORD64 firstManaged,
                                     DWORD64 secondManaged) {
    if (!Mem::IsUserAddress(firstManaged) ||
        !Mem::IsUserAddress(secondManaged)) {
        return false;
    }
    const DWORD64 firstNative = mem.Read<DWORD64>(
        firstManaged + g_RuntimeOffsets.unityObjectCachedPtr);
    const DWORD64 secondNative = mem.Read<DWORD64>(
        secondManaged + g_RuntimeOffsets.unityObjectCachedPtr);
    return Mem::IsUserAddress(firstNative) && firstNative == secondNative;
}

// ObjectPointComponent/UnsafeBoneSkeletonTree layouts are serialized in the
// 7.23 SDK dump, but these fields are intentionally kept local to the
// fallback. They are not used by the primary SerializeBoneSkeletonTree path.
namespace SKJH_SkeletonFallbackLayout {
constexpr uint32_t kPointCache = 0x30;
constexpr uint32_t kOriginalBoneTree = 0x40;
constexpr uint32_t kUnsafeBoneNodes = 0x18;
constexpr uint32_t kUnsafeTransformIds = 0x40;
constexpr uint32_t kUnsafeListData = 0x00;
constexpr uint32_t kUnsafeListLength = 0x08;
constexpr uint32_t kUnsafeListCapacity = 0x0c;
constexpr uint32_t kSerializableDictionaryKeys = 0x18;
constexpr uint32_t kSerializableDictionaryValues = 0x20;
constexpr uint32_t kIl2CppArrayLength = 0x18;
constexpr uint32_t kIl2CppArrayData = 0x20;
// UnityEngine.Component.cachedName. It is populated by the native name
// getter and is present on every Transform object in this SDK profile.
constexpr uint32_t kComponentCachedName = 0x20;
constexpr uint32_t kNativeInstanceIdCandidates[] = {
    0x08, 0x0c, 0x10, 0x18, 0x20,
};
} // namespace SKJH_SkeletonFallbackLayout

inline bool SKJH_ReadTransformCachedName(DWORD64 managedTransform,
                                         std::string& output) {
    output.clear();
    if (!Mem::IsUserAddress(managedTransform)) return false;
    DWORD64 cachedName = 0;
    if (!mem.Read(managedTransform +
                      SKJH_SkeletonFallbackLayout::kComponentCachedName,
                  &cachedName, sizeof(cachedName)) ||
        !Mem::IsUserAddress(cachedName)) {
        return false;
    }
    return SKJH_ReadSkeletonString(cachedName, output);
}

inline bool SKJH_ReadUnsafeListDescriptor(
    DWORD64 listAddress, DWORD64& data, int32_t& length, int32_t& capacity,
    int32_t minimumLength, int32_t maximumLength) {
    data = 0;
    length = 0;
    capacity = 0;
    if (!Mem::IsUserAddress(listAddress) ||
        !mem.Read(listAddress + SKJH_SkeletonFallbackLayout::kUnsafeListData,
                  &data, sizeof(data)) ||
        !mem.Read(listAddress + SKJH_SkeletonFallbackLayout::kUnsafeListLength,
                  &length, sizeof(length)) ||
        !mem.Read(listAddress + SKJH_SkeletonFallbackLayout::kUnsafeListCapacity,
                  &capacity, sizeof(capacity))) {
        return false;
    }
    if (!Mem::IsUserAddress(data) || length < minimumLength ||
        length > maximumLength || capacity < length ||
        capacity > maximumLength * 2) {
        return false;
    }
    return true;
}

inline bool SKJH_ReadArrayDescriptor(DWORD64 array, DWORD64& data,
                                     int32_t& length, int32_t minimumLength,
                                     int32_t maximumLength) {
    data = 0;
    length = 0;
    if (!Mem::IsUserAddress(array) ||
        !mem.Read(array + SKJH_SkeletonFallbackLayout::kIl2CppArrayLength,
                  &length, sizeof(length)) ||
        length < minimumLength || length > maximumLength) {
        return false;
    }
    data = array + SKJH_SkeletonFallbackLayout::kIl2CppArrayData;
    return Mem::IsUserAddress(data);
}

inline bool SKJH_ReadNativeTransformInstanceId(DWORD64 managedTransform,
                                               uint32_t nativeOffset,
                                               int32_t& instanceId) {
    instanceId = 0;
    if (!Mem::IsUserAddress(managedTransform)) return false;
    const DWORD64 nativeTransform = mem.Read<DWORD64>(
        managedTransform + g_RuntimeOffsets.unityObjectCachedPtr);
    if (!Mem::IsUserAddress(nativeTransform)) return false;
    return mem.Read(nativeTransform + nativeOffset, &instanceId,
                    sizeof(instanceId));
}

inline bool SKJH_TryResolvePlayerSkeletonBinding(
    DWORD64 playerGo, DWORD64 controller, uint32_t targetRootPointOffset,
    SKJH_PlayerSkeletonBinding& output) {
    output = {};
    g_SKJH_SkeletonBindingStage = "candidate_input";
    if (!Mem::IsUserAddress(playerGo) || !Mem::IsUserAddress(controller) ||
        !targetRootPointOffset) return false;

    g_SKJH_SkeletonBindingStage = "target_root";
    const DWORD64 targetRootPoint = mem.Read<DWORD64>(
        controller + targetRootPointOffset);
    if (!Mem::IsUserAddress(targetRootPoint)) return false;
    g_SKJH_SkeletonBindingStage = "target_class";
    // The component class name may live on a non-resident metadata page. The
    // serialized tree shape and complete humanoid mapping below are the
    // authoritative structural checks.
    g_SKJH_SkeletonBindingStage = "serialize_tree";
    const DWORD64 serializeTree = mem.Read<DWORD64>(
        targetRootPoint + g_RuntimeOffsets.objectPointSerializeBoneTree);
    if (!Mem::IsUserAddress(serializeTree)) return false;
    g_SKJH_SkeletonBindingStage = "tree_arrays";
    const DWORD64 nodesArray = mem.Read<DWORD64>(
        serializeTree + g_RuntimeOffsets.boneTreeNodes);
    const DWORD64 transformsArray = mem.Read<DWORD64>(
        serializeTree + g_RuntimeOffsets.boneTreeAllTransforms);
    if (!Mem::IsUserAddress(nodesArray) ||
        !Mem::IsUserAddress(transformsArray)) {
        return false;
    }

    constexpr int32_t kMaximumTreeEntries = 2048;
    g_SKJH_SkeletonBindingStage = "tree_counts";
    const int32_t nodeCount = mem.Read<int32_t>(nodesArray + 0x18);
    const int32_t transformCount = mem.Read<int32_t>(transformsArray + 0x18);
    if (nodeCount < BONE_COUNT || nodeCount > kMaximumTreeEntries ||
        transformCount < BONE_COUNT ||
        transformCount > kMaximumTreeEntries ||
        nodeCount > transformCount) {
        return false;
    }

    std::vector<DWORD64> nodes(static_cast<size_t>(nodeCount));
    std::vector<DWORD64> transforms(static_cast<size_t>(transformCount));
    g_SKJH_SkeletonBindingStage = "tree_bulk_read";
    if (!mem.Read(nodesArray + 0x20, nodes.data(),
                  static_cast<DWORD>(nodes.size() * sizeof(DWORD64))) ||
        !mem.Read(transformsArray + 0x20, transforms.data(),
                  static_cast<DWORD>(transforms.size() * sizeof(DWORD64)))) {
        return false;
    }

    std::array<bool, BONE_COUNT> found{};
    std::unordered_set<int32_t> mappedIndices;
    mappedIndices.reserve(static_cast<size_t>(nodeCount));
    g_SKJH_SkeletonBindingStage = "tree_node_layout";
    for (int32_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
        const DWORD64 node = nodes[static_cast<size_t>(nodeIndex)];
        if (!Mem::IsUserAddress(node)) return false;
        int32_t boneIndex = -1;
        int32_t parentIndex = -2;
        DWORD64 boneNameObject = 0;
        if (!mem.Read(node + g_RuntimeOffsets.boneTreeNodeBoneIndex,
                      &boneIndex, sizeof(boneIndex)) ||
            !mem.Read(node + g_RuntimeOffsets.boneTreeNodeParentIndex,
                      &parentIndex, sizeof(parentIndex)) ||
            !mem.Read(node + g_RuntimeOffsets.boneTreeNodeBoneName,
                      &boneNameObject, sizeof(boneNameObject)) ||
            boneIndex < 0 || boneIndex >= transformCount ||
            parentIndex < -1 || parentIndex >= nodeCount ||
            !mappedIndices.emplace(boneIndex).second ||
            !Mem::IsUserAddress(transforms[static_cast<size_t>(boneIndex)])) {
            return false;
        }

        std::string boneName;
        // Serialized trees also contain long locator paths unrelated to the
        // humanoid pose. A transient read of one of those entries must not
        // discard every required bone that was mapped successfully.
        if (!SKJH_ReadSkeletonString(boneNameObject, boneName)) continue;
        const int bone = SKJH_BoneIndexFromTreeName(boneName);
        if (bone < 0) continue;
        if (found[bone]) return false;
        output.managedTransforms[bone] =
            transforms[static_cast<size_t>(boneIndex)];
        output.sourceNodeArrayIndices[bone] = nodeIndex;
        output.sourceBoneIndices[bone] = boneIndex;
        output.sourceParentIndices[bone] = parentIndex;
        output.sourceNodePointers[bone] = node;
        found[bone] = true;
    }
    g_SKJH_SkeletonBindingStage = "required_bone_names";
    if (std::find(found.begin(), found.end(), false) != found.end())
        return false;

    // These four fields are independently exposed by ClientPlayerGo. When a
    // field is populated, it must resolve to the same native Transform as the
    // named tree entry.
    struct DirectAnchor {
        int bone;
        uint32_t offset;
    };
    const DirectAnchor anchors[] = {
        {BONE_HEAD, g_RuntimeOffsets.clientPlayerHead},
        {BONE_NECK, g_RuntimeOffsets.clientPlayerNeck},
        {BONE_LEFT_FOOT, g_RuntimeOffsets.clientPlayerLeftFoot},
        {BONE_RIGHT_FOOT, g_RuntimeOffsets.clientPlayerRightFoot},
    };
    g_SKJH_SkeletonBindingStage = "direct_anchors";
    for (const auto& anchor : anchors) {
        const DWORD64 direct = mem.Read<DWORD64>(playerGo + anchor.offset);
        if (Mem::IsUserAddress(direct) &&
            !SKJH_SameNativeTransform(output.managedTransforms[anchor.bone],
                                      direct)) {
            return false;
        }
    }

    DWORD64 commonTransformData = 0;
    std::unordered_set<int32_t> targetIndices;
    g_SKJH_SkeletonBindingStage = "transform_identity";
    for (int bone = 0; bone < BONE_COUNT; ++bone) {
        DWORD64 nativeTransform = 0;
        DWORD64 transformData = 0;
        int32_t transformIndex = -1;
        if (!SKJH_ReadNativeTransformIdentity(
                output.managedTransforms[bone], nativeTransform,
                transformData, transformIndex)) {
            return false;
        }
        if (!commonTransformData) commonTransformData = transformData;
        if (transformData != commonTransformData ||
            !targetIndices.emplace(transformIndex).second) {
            return false;
        }
        output.transformIndices[bone] = transformIndex;
    }

    g_SKJH_SkeletonBindingStage = "transform_arrays";
    const DWORD64 localTransforms = mem.Read<DWORD64>(
        commonTransformData + g_RuntimeOffsets.unityDataTransforms);
    const DWORD64 parentIndices = mem.Read<DWORD64>(
        commonTransformData + g_RuntimeOffsets.unityDataParents);
    if (!Mem::IsUserAddress(localTransforms) ||
        !Mem::IsUserAddress(parentIndices)) {
        return false;
    }

    std::unordered_set<int32_t> hierarchySet;
    g_SKJH_SkeletonBindingStage = "transform_hierarchy";
    for (const int32_t targetIndex : output.transformIndices) {
        int32_t current = targetIndex;
        std::unordered_set<int32_t> path;
        for (int depth = 0; current >= 0 && depth < 128; ++depth) {
            if (current > 1000000 || !path.emplace(current).second)
                return false;
            hierarchySet.emplace(current);
            int32_t parent = -2;
            if (!mem.Read(parentIndices +
                              static_cast<DWORD64>(current) * sizeof(int32_t),
                          &parent, sizeof(parent)) ||
                parent < -1 || parent > 1000000) {
                return false;
            }
            output.parentByIndex[current] = parent;
            current = parent;
        }
        if (current >= 0) return false;
    }
    if (hierarchySet.empty() || hierarchySet.size() > 512) return false;
    output.hierarchyIndices.assign(hierarchySet.begin(), hierarchySet.end());
    std::sort(output.hierarchyIndices.begin(), output.hierarchyIndices.end());

    output.playerGo = playerGo;
    output.controller = controller;
    output.targetRootPoint = targetRootPoint;
    output.serializeTree = serializeTree;
    output.nodesArray = nodesArray;
    output.transformsArray = transformsArray;
    output.transformData = commonTransformData;
    output.localTransforms = localTransforms;
    output.parentIndices = parentIndices;
    output.sourceKind = SKJH_SkeletonSourceKind::SerializeTree;
    output.targetRootPointOffset = targetRootPointOffset;
    output.valid = true;
    g_SKJH_SkeletonBindingStage = "complete";
    return true;
}

// Some remote player prefabs do not serialize ObjectPointComponent's managed
// tree. The runtime still builds UnsafeBoneSkeletonTree and pointCache for
// those prefabs. Resolve that representation only when the Transform objects
// expose all required semantic names and every identity check agrees. In
// particular, this routine never assigns an arm/leg by position or array
// order; an unknown name makes the candidate fail closed.
inline bool SKJH_TryResolvePlayerSkeletonBindingOriginal(
    DWORD64 playerGo, DWORD64 controller, uint32_t targetRootPointOffset,
    SKJH_PlayerSkeletonBinding& output) {
    output = {};
    g_SKJH_SkeletonCandidateProbe = {};
    g_SKJH_SkeletonBindingStage = "original_tree_input";
    if (!Mem::IsUserAddress(playerGo) || !Mem::IsUserAddress(controller) ||
        !targetRootPointOffset) {
        return false;
    }

    const DWORD64 targetRootPoint = mem.Read<DWORD64>(
        controller + targetRootPointOffset);
    g_SKJH_SkeletonCandidateProbe.targetRootPoint = targetRootPoint;
    if (!Mem::IsUserAddress(targetRootPoint)) {
        g_SKJH_SkeletonBindingStage = "original_target_root";
        return false;
    }

    g_SKJH_SkeletonBindingStage = "original_components";
    const DWORD64 pointCache = mem.Read<DWORD64>(
        targetRootPoint + SKJH_SkeletonFallbackLayout::kPointCache);
    const DWORD64 originalTree = mem.Read<DWORD64>(
        targetRootPoint + SKJH_SkeletonFallbackLayout::kOriginalBoneTree);
    g_SKJH_SkeletonCandidateProbe.pointCache = pointCache;
    g_SKJH_SkeletonCandidateProbe.originalTree = originalTree;
    if (!Mem::IsUserAddress(pointCache) ||
        !Mem::IsUserAddress(originalTree)) {
        return false;
    }

    // SerializableDictionary<ulong, Transform> stores its serialized arrays
    // at +0x18/+0x20 (object header included). The managed hash dictionary is
    // deliberately ignored because its bucket table can be resized while DMA
    // is reading; the serialized arrays are a single immutable generation.
    g_SKJH_SkeletonBindingStage = "original_point_cache_arrays";
    const DWORD64 keyArray = mem.Read<DWORD64>(
        pointCache + SKJH_SkeletonFallbackLayout::kSerializableDictionaryKeys);
    const DWORD64 valueArray = mem.Read<DWORD64>(
        pointCache + SKJH_SkeletonFallbackLayout::kSerializableDictionaryValues);
    g_SKJH_SkeletonCandidateProbe.keyArray = keyArray;
    g_SKJH_SkeletonCandidateProbe.valueArray = valueArray;
    DWORD64 keyData = 0;
    DWORD64 valueData = 0;
    int32_t keyCount = 0;
    int32_t valueCount = 0;
    if (!SKJH_ReadArrayDescriptor(keyArray, keyData, keyCount, BONE_COUNT,
                                  4096) ||
        !SKJH_ReadArrayDescriptor(valueArray, valueData, valueCount,
                                   BONE_COUNT, 4096) ||
        keyCount != valueCount) {
        return false;
    }
    g_SKJH_SkeletonCandidateProbe.keyCount = keyCount;
    g_SKJH_SkeletonCandidateProbe.valueCount = valueCount;
    std::vector<uint64_t> pointKeys(static_cast<size_t>(keyCount));
    std::vector<DWORD64> pointValues(static_cast<size_t>(valueCount));
    if (!mem.Read(keyData, pointKeys.data(), static_cast<DWORD>(
                      pointKeys.size() * sizeof(pointKeys[0]))) ||
        !mem.Read(valueData, pointValues.data(), static_cast<DWORD>(
                      pointValues.size() * sizeof(pointValues[0])))) {
        g_SKJH_SkeletonBindingStage = "original_point_cache_read";
        return false;
    }
    std::unordered_set<uint64_t> uniquePointKeys;
    uniquePointKeys.reserve(pointKeys.size());
    for (const uint64_t key : pointKeys) {
        if (!key || !uniquePointKeys.emplace(key).second) {
            g_SKJH_SkeletonBindingStage = "original_point_cache_keys";
            return false;
        }
    }

    // UnsafeList<T> is an inline descriptor (data, length, capacity). The
    // node list and instance-id list must describe one complete generation.
    g_SKJH_SkeletonBindingStage = "original_tree_lists";
    DWORD64 nodeData = 0;
    DWORD64 transformIdData = 0;
    int32_t nodeCount = 0;
    int32_t nodeCapacity = 0;
    int32_t transformIdCount = 0;
    int32_t transformIdCapacity = 0;
    if (!SKJH_ReadUnsafeListDescriptor(
            originalTree + SKJH_SkeletonFallbackLayout::kUnsafeBoneNodes,
            nodeData, nodeCount, nodeCapacity, BONE_COUNT, 4096) ||
        !SKJH_ReadUnsafeListDescriptor(
            originalTree + SKJH_SkeletonFallbackLayout::kUnsafeTransformIds,
            transformIdData, transformIdCount, transformIdCapacity,
            BONE_COUNT, 4096) ||
        nodeCount > transformIdCount) {
        return false;
    }
    g_SKJH_SkeletonCandidateProbe.nodeData = nodeData;
    g_SKJH_SkeletonCandidateProbe.transformIdData = transformIdData;
    g_SKJH_SkeletonCandidateProbe.nodeCount = nodeCount;
    g_SKJH_SkeletonCandidateProbe.transformIdCount = transformIdCount;

    struct OriginalNode {
        int32_t boneIndex = -1;
        int32_t parentIndex = -1;
    };
    std::vector<OriginalNode> nodes(static_cast<size_t>(nodeCount));
    std::vector<int32_t> transformIds(static_cast<size_t>(transformIdCount));
    if (!mem.Read(nodeData, nodes.data(), static_cast<DWORD>(
                      nodes.size() * sizeof(nodes[0]))) ||
        !mem.Read(transformIdData, transformIds.data(), static_cast<DWORD>(
                      transformIds.size() * sizeof(transformIds[0])))) {
        g_SKJH_SkeletonBindingStage = "original_tree_read";
        return false;
    }

    std::unordered_map<int32_t, int32_t> nodeByBoneIndex;
    nodeByBoneIndex.reserve(nodes.size());
    // Keep both known ParentIndex encodings until native Transform ancestry
    // gives us an unambiguous answer.  BuildBone receives NodeParentIndex,
    // while some generated revisions wrote the compact BoneIndex instead.
    std::unordered_map<int32_t, int32_t> parentByNodeIndex;
    parentByNodeIndex.reserve(nodes.size());
    std::unordered_map<int32_t, int32_t> parentByBoneIndex;
    parentByBoneIndex.reserve(nodes.size());
    std::unordered_map<int32_t, int32_t> treeIndexByInstanceId;
    treeIndexByInstanceId.reserve(transformIds.size());
    std::unordered_map<int32_t, int32_t> firstTransformIdIndex;
    firstTransformIdIndex.reserve(transformIds.size());
    for (int32_t index = 0; index < transformIdCount; ++index) {
        const int32_t instanceId = transformIds[static_cast<size_t>(index)];
        if (g_SKJH_SkeletonCandidateProbe.transformIdSampleCount <
                static_cast<int32_t>(g_SKJH_SkeletonCandidateProbe.transformIdSample.size())) {
            g_SKJH_SkeletonCandidateProbe.transformIdSample[
                static_cast<size_t>(g_SKJH_SkeletonCandidateProbe.transformIdSampleCount++)] = instanceId;
        }
        // Unity's native transform cache can retain zeroed slots inside the
        // active UnsafeList generation while a model is rebuilding. Zero is
        // not a valid Object instance id; ignore only that placeholder.
        if (instanceId == 0) continue;
        const auto firstId = firstTransformIdIndex.emplace(instanceId, index);
        if (!treeIndexByInstanceId.emplace(instanceId, index).second) {
            g_SKJH_SkeletonCandidateProbe.duplicateTransformId = instanceId;
            g_SKJH_SkeletonCandidateProbe.duplicateTransformIdIndex = index;
            g_SKJH_SkeletonCandidateProbe.duplicateTransformIdFirstIndex =
                firstId.first->second;
            g_SKJH_SkeletonBindingStage = "original_tree_transform_ids";
            return false;
        }
    }
    for (int32_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
        const OriginalNode node = nodes[static_cast<size_t>(nodeIndex)];
        if (node.boneIndex < 0 || node.boneIndex >= transformIdCount ||
            node.parentIndex < -1 || node.parentIndex >= nodeCount ||
            !nodeByBoneIndex.emplace(node.boneIndex, nodeIndex).second) {
            g_SKJH_SkeletonBindingStage = "original_tree_nodes";
            return false;
        }
    }
    for (const auto& entry : nodeByBoneIndex) {
        const int32_t boneIndex = entry.first;
        const OriginalNode node = nodes[static_cast<size_t>(entry.second)];
        int32_t parentNodeBoneIndex = -1;
        if (node.parentIndex >= 0) {
            const OriginalNode& parentNode =
                nodes[static_cast<size_t>(node.parentIndex)];
            // A node-index parent must resolve to a real compact bone entry;
            // otherwise this candidate is a torn or mixed tree generation.
            if (parentNode.boneIndex < 0 ||
                parentNode.boneIndex >= transformIdCount) {
                g_SKJH_SkeletonBindingStage = "original_tree_parent_map";
                return false;
            }
            parentNodeBoneIndex = parentNode.boneIndex;
        }
        if (parentNodeBoneIndex == boneIndex ||
            !parentByNodeIndex.emplace(boneIndex, parentNodeBoneIndex).second ||
            !parentByBoneIndex.emplace(boneIndex, node.parentIndex).second) {
            g_SKJH_SkeletonBindingStage = "original_tree_parent_map";
            return false;
        }
    }

    // Validate each candidate map independently before using any named points.
    // A malformed alternate encoding is allowed only when the other map is
    // fully valid and later wins the native-hierarchy comparison.
    const auto validateParentMap = [&](
            const std::unordered_map<int32_t, int32_t>& parents) {
        if (parents.size() != nodeByBoneIndex.size()) return false;
        for (const auto& entry : parents) {
            std::unordered_set<int32_t> path;
            int32_t current = entry.first;
            for (int depth = 0; current >= 0 && depth <= nodeCount; ++depth) {
                if (!path.emplace(current).second) return false;
                const auto parent = parents.find(current);
                if (parent == parents.end()) return false;
                current = parent->second;
            }
            if (current >= 0) return false;
        }
        return true;
    };
    const bool nodeParentMapValid = validateParentMap(parentByNodeIndex);
    const bool boneParentMapValid = validateParentMap(parentByBoneIndex);
    if (!nodeParentMapValid && !boneParentMapValid) {
        g_SKJH_SkeletonBindingStage = "original_tree_parent_map";
        return false;
    }

    // Resolve names from the actual Transform managed objects. cachedName is
    // the Component.name cache, not a guessed name derived from geometry.
    g_SKJH_SkeletonBindingStage = "original_cached_names";
    std::array<bool, BONE_COUNT> found{};
    std::unordered_set<DWORD64> uniqueManaged;
    uniqueManaged.reserve(BONE_COUNT);
    for (const DWORD64 managedTransform : pointValues) {
        if (!Mem::IsUserAddress(managedTransform)) continue;
        std::string boneName;
        if (!SKJH_ReadTransformCachedName(managedTransform, boneName)) continue;
        const int bone = SKJH_BoneIndexFromTreeName(boneName);
        if (bone < 0) continue;
        if (found[bone] || !uniqueManaged.emplace(managedTransform).second) {
            g_SKJH_SkeletonBindingStage = "original_cached_name_duplicates";
            return false;
        }
        output.managedTransforms[bone] = managedTransform;
        found[bone] = true;
    }
    if (std::find(found.begin(), found.end(), false) != found.end()) {
        g_SKJH_SkeletonBindingStage = "original_required_names";
        return false;
    }

    // Reject a pointCache/tree replacement that occurred while the two DMA
    // arrays were being copied. The next sampling pass will retry the new
    // generation instead of combining old names with new transforms.
    if (mem.Read<DWORD64>(targetRootPoint +
                              SKJH_SkeletonFallbackLayout::kPointCache) !=
            pointCache ||
        mem.Read<DWORD64>(targetRootPoint +
                              SKJH_SkeletonFallbackLayout::kOriginalBoneTree) !=
            originalTree) {
        g_SKJH_SkeletonBindingStage = "original_generation_changed";
        return false;
    }

    // Match Unity native instance IDs to UnsafeBoneSkeletonTree's cache. The
    // SDK's native Object layout has moved this field in a few Unity builds;
    // choose an offset only when all twenty named transforms agree. A partial
    // match is rejected rather than publishing an ambiguous mapping.
    g_SKJH_SkeletonBindingStage = "original_instance_ids";
    int32_t originalRootInstanceId = 0;
    if (!mem.Read(originalTree + 0x10, &originalRootInstanceId,
                  sizeof(originalRootInstanceId))) {
        return false;
    }
    g_SKJH_SkeletonCandidateProbe.originalRootInstanceId =
        originalRootInstanceId;
    const DWORD64 rootManagedTransform = mem.Read<DWORD64>(
        playerGo + g_RuntimeOffsets.basePlayerGoRootBone);
    uint32_t instanceIdOffset = 0;
    int bestMatches = -1;
    bool rootMatched = false;
    for (const uint32_t candidateOffset :
             SKJH_SkeletonFallbackLayout::kNativeInstanceIdCandidates) {
        int matches = 0;
        std::unordered_set<int32_t> seen;
        bool candidateValid = true;
        int32_t rootCandidate = 0;
        const bool hasRootCandidate = SKJH_ReadNativeTransformInstanceId(
            rootManagedTransform, candidateOffset, rootCandidate);
        const bool candidateRootMatches = hasRootCandidate &&
            originalRootInstanceId != 0 &&
            rootCandidate == originalRootInstanceId;
        for (int bone = 0; bone < BONE_COUNT; ++bone) {
            int32_t instanceId = 0;
            if (!SKJH_ReadNativeTransformInstanceId(
                    output.managedTransforms[bone], candidateOffset,
                    instanceId) || !seen.emplace(instanceId).second) {
                candidateValid = false;
                break;
            }
            if (treeIndexByInstanceId.find(instanceId) !=
                treeIndexByInstanceId.end()) {
                ++matches;
            }
        }
        if (candidateValid &&
            ((!rootMatched && candidateRootMatches) ||
             (rootMatched && candidateRootMatches && matches > bestMatches) ||
             (!rootMatched && !candidateRootMatches &&
              originalRootInstanceId == 0 && matches > bestMatches))) {
            bestMatches = matches;
            instanceIdOffset = candidateOffset;
            rootMatched = candidateRootMatches;
            g_SKJH_SkeletonCandidateProbe.selectedRootInstanceId = rootCandidate;
        }
    }
    if (!instanceIdOffset || bestMatches != BONE_COUNT) return false;
    g_SKJH_SkeletonCandidateProbe.selectedInstanceIdOffset = instanceIdOffset;
    g_SKJH_SkeletonCandidateProbe.selectedInstanceIdMatches = bestMatches;

    for (int bone = 0; bone < BONE_COUNT; ++bone) {
        int32_t instanceId = 0;
        if (!SKJH_ReadNativeTransformInstanceId(
                output.managedTransforms[bone], instanceIdOffset, instanceId)) {
            return false;
        }
        const auto treeIndex = treeIndexByInstanceId.find(instanceId);
        if (treeIndex == treeIndexByInstanceId.end() ||
            (!parentByNodeIndex.count(treeIndex->second) &&
             !parentByBoneIndex.count(treeIndex->second))) {
            g_SKJH_SkeletonBindingStage = "original_tree_mapping";
            return false;
        }
        output.originalBoneIndices[bone] = treeIndex->second;
        const auto nodeEntry = nodeByBoneIndex.find(treeIndex->second);
        if (nodeEntry == nodeByBoneIndex.end()) {
            g_SKJH_SkeletonBindingStage = "original_tree_mapping";
            return false;
        }
        const int32_t nodeArrayIndex = nodeEntry->second;
        const OriginalNode& sourceNode =
            nodes[static_cast<size_t>(nodeArrayIndex)];
        output.sourceNodeArrayIndices[bone] = nodeArrayIndex;
        output.sourceBoneIndices[bone] = sourceNode.boneIndex;
        output.sourceParentIndices[bone] = sourceNode.parentIndex;
        output.sourceNodePointers[bone] =
            nodeData + static_cast<DWORD64>(nodeArrayIndex) *
                sizeof(OriginalNode);
    }

    struct DirectAnchor {
        int bone;
        uint32_t offset;
    };
    const DirectAnchor anchors[] = {
        {BONE_HEAD, g_RuntimeOffsets.clientPlayerHead},
        {BONE_NECK, g_RuntimeOffsets.clientPlayerNeck},
        {BONE_LEFT_FOOT, g_RuntimeOffsets.clientPlayerLeftFoot},
        {BONE_RIGHT_FOOT, g_RuntimeOffsets.clientPlayerRightFoot},
    };
    g_SKJH_SkeletonBindingStage = "original_direct_anchors";
    for (const auto& anchor : anchors) {
        const DWORD64 direct = mem.Read<DWORD64>(playerGo + anchor.offset);
        if (Mem::IsUserAddress(direct) &&
            !SKJH_SameNativeTransform(output.managedTransforms[anchor.bone],
                                       direct)) {
            return false;
        }
    }

    DWORD64 commonTransformData = 0;
    std::unordered_set<int32_t> targetIndices;
    g_SKJH_SkeletonBindingStage = "original_transform_identity";
    for (int bone = 0; bone < BONE_COUNT; ++bone) {
        DWORD64 nativeTransform = 0;
        DWORD64 transformData = 0;
        int32_t transformIndex = -1;
        if (!SKJH_ReadNativeTransformIdentity(
                output.managedTransforms[bone], nativeTransform,
                transformData, transformIndex)) {
            return false;
        }
        if (!commonTransformData) commonTransformData = transformData;
        if (transformData != commonTransformData ||
            !targetIndices.emplace(transformIndex).second) {
            return false;
        }
        output.transformIndices[bone] = transformIndex;
    }

    g_SKJH_SkeletonBindingStage = "original_transform_arrays";
    const DWORD64 localTransforms = mem.Read<DWORD64>(
        commonTransformData + g_RuntimeOffsets.unityDataTransforms);
    const DWORD64 parentIndices = mem.Read<DWORD64>(
        commonTransformData + g_RuntimeOffsets.unityDataParents);
    if (!Mem::IsUserAddress(localTransforms) ||
        !Mem::IsUserAddress(parentIndices)) {
        return false;
    }

    std::unordered_set<int32_t> hierarchySet;
    g_SKJH_SkeletonBindingStage = "original_transform_hierarchy";
    for (const int32_t targetIndex : output.transformIndices) {
        int32_t current = targetIndex;
        std::unordered_set<int32_t> path;
        for (int depth = 0; current >= 0 && depth < 128; ++depth) {
            if (current > 1000000 || !path.emplace(current).second) return false;
            hierarchySet.emplace(current);
            int32_t parent = -2;
            if (!mem.Read(parentIndices + static_cast<DWORD64>(current) *
                              sizeof(int32_t), &parent, sizeof(parent)) ||
                parent < -1 || parent > 1000000) {
                return false;
            }
            output.parentByIndex[current] = parent;
            current = parent;
        }
        if (current >= 0) return false;
    }
    if (hierarchySet.empty() || hierarchySet.size() > 512) return false;
    output.hierarchyIndices.assign(hierarchySet.begin(), hierarchySet.end());
    std::sort(output.hierarchyIndices.begin(), output.hierarchyIndices.end());

    // Validate the managed tree independently from Unity's transform storage.
    // Unity may flatten animated TransformAccess entries under one internal
    // parent, so its parent array remains authoritative for world-pose math
    // but is not a semantic humanoid skeleton.
    const auto isAncestor = [](int32_t ancestor, int32_t node,
                               const std::unordered_map<int32_t, int32_t>& parents) {
        std::unordered_set<int32_t> path;
        for (int depth = 0; node >= 0 && depth < 128; ++depth) {
            if (!path.emplace(node).second) return false;
            if (node == ancestor) return true;
            const auto parent = parents.find(node);
            if (parent == parents.end()) return false;
            node = parent->second;
        }
        return false;
    };
    // ParentIndex has two layouts in generated trees: a node-array index or a
    // compact BoneIndex. Score both against the required humanoid topology and
    // accept only a unique exact match.
    struct ParentScore {
        int32_t mismatchCount = 0;
        int32_t first = -1;
        int32_t second = -1;
    };
    // BoneConnections is a draw-edge list with mixed endpoint order, so keep
    // an explicit oriented table for binding validation instead of guessing
    // direction from that rendering list.
    constexpr std::array<int32_t, BONE_COUNT> kExpectedHumanoidParents{{
        BONE_NECK,            // head
        BONE_SPINE1,          // upper torso / Spine2
        BONE_PELVIS,          // Spine
        BONE_LEFT_CALF,       // left foot
        BONE_RIGHT_CALF,      // right foot
        BONE_BODY,            // neck
        -1,                   // pelvis root
        BONE_SPINE,           // Spine1
        BONE_BODY,            // left clavicle
        BONE_LEFT_CLAVICLE,   // left upper arm
        BONE_LEFT_UPPER_ARM,  // left forearm
        BONE_LEFT_FOREARM,    // left hand
        BONE_BODY,            // right clavicle
        BONE_RIGHT_CLAVICLE,  // right upper arm
        BONE_RIGHT_UPPER_ARM, // right forearm
        BONE_RIGHT_FOREARM,   // right hand
        BONE_PELVIS,          // left thigh
        BONE_LEFT_THIGH,      // left calf
        BONE_PELVIS,          // right thigh
        BONE_RIGHT_THIGH,     // right calf
    }};
    std::unordered_map<int32_t, int32_t> expectedParentByBone;
    expectedParentByBone.reserve(BONE_COUNT);
    for (int bone = 0; bone < BONE_COUNT; ++bone)
        expectedParentByBone.emplace(bone, kExpectedHumanoidParents[bone]);

    const auto scoreParentMap = [&](
            const std::unordered_map<int32_t, int32_t>& parents,
            bool mapValid) {
        ParentScore score;
        if (!mapValid) {
            score.mismatchCount =
                (std::numeric_limits<int32_t>::max)();
            return score;
        }
        for (int first = 0; first < BONE_COUNT; ++first) {
            for (int second = 0; second < BONE_COUNT; ++second) {
                if (first == second) continue;
                const bool originalRelation = isAncestor(
                    output.originalBoneIndices[first],
                    output.originalBoneIndices[second], parents);
                const bool expectedRelation = isAncestor(
                    first, second, expectedParentByBone);
                if (originalRelation == expectedRelation) continue;
                ++score.mismatchCount;
                if (score.first < 0) {
                    score.first = first;
                    score.second = second;
                }
            }
        }
        return score;
    };
    g_SKJH_SkeletonCandidateProbe.parentRelationsCompared =
        BONE_COUNT * (BONE_COUNT - 1);
    for (int bone = 0; bone < BONE_COUNT; ++bone) {
        const int32_t originalIndex = output.originalBoneIndices[bone];
        const int32_t nativeIndex = output.transformIndices[bone];
        g_SKJH_SkeletonCandidateProbe.namedOriginalIndices[bone] =
            originalIndex;
        const auto nodeParent = parentByNodeIndex.find(originalIndex);
        g_SKJH_SkeletonCandidateProbe.namedNodeIndexParents[bone] =
            nodeParent == parentByNodeIndex.end() ? -2 : nodeParent->second;
        const auto boneParent = parentByBoneIndex.find(originalIndex);
        g_SKJH_SkeletonCandidateProbe.namedBoneIndexParents[bone] =
            boneParent == parentByBoneIndex.end() ? -2 : boneParent->second;
        g_SKJH_SkeletonCandidateProbe.namedNativeIndices[bone] = nativeIndex;
        const auto nativeParent = output.parentByIndex.find(nativeIndex);
        g_SKJH_SkeletonCandidateProbe.namedNativeParents[bone] =
            nativeParent == output.parentByIndex.end() ? -2 :
                nativeParent->second;
    }
    const ParentScore nodeScore =
        scoreParentMap(parentByNodeIndex, nodeParentMapValid);
    const ParentScore boneScore =
        scoreParentMap(parentByBoneIndex, boneParentMapValid);
    g_SKJH_SkeletonCandidateProbe.parentNodeIndexMismatchCount =
        nodeParentMapValid ? nodeScore.mismatchCount : -1;
    g_SKJH_SkeletonCandidateProbe.parentBoneIndexMismatchCount =
        boneParentMapValid ? boneScore.mismatchCount : -1;

    int selectedInterpretation = -1;
    if (nodeParentMapValid &&
        (!boneParentMapValid || nodeScore.mismatchCount <
         boneScore.mismatchCount)) {
        selectedInterpretation = 0;
    } else if (boneParentMapValid &&
               (!nodeParentMapValid || boneScore.mismatchCount <
                nodeScore.mismatchCount)) {
        selectedInterpretation = 1;
    } else if (nodeParentMapValid && boneParentMapValid &&
               nodeScore.mismatchCount == boneScore.mismatchCount) {
        // Equal scores are safe only when both maps produce the same named
        // relations. Prefer the SDK-documented node form in that case.
        bool relationsEquivalent = true;
        for (int first = 0; first < BONE_COUNT && relationsEquivalent;
             ++first) {
            for (int second = 0; second < BONE_COUNT; ++second) {
                if (first == second) continue;
                if (isAncestor(output.originalBoneIndices[first],
                               output.originalBoneIndices[second],
                               parentByNodeIndex) !=
                    isAncestor(output.originalBoneIndices[first],
                               output.originalBoneIndices[second],
                               parentByBoneIndex)) {
                    relationsEquivalent = false;
                    break;
                }
            }
        }
        if (relationsEquivalent) selectedInterpretation = 0;
    }

    const ParentScore& selectedScore = selectedInterpretation == 0
        ? nodeScore : boneScore;
    if (selectedInterpretation < 0) {
        g_SKJH_SkeletonBindingStage = "original_parent_ambiguous";
        return false;
    }
    // Record the parent mismatch for diagnostics but do not abort the binding.
    // The geometry compactness gate below is the real safety net: it rejects
    // any mapping that produces an incoherent humanoid pose. Aborting here on
    // a parent-relation mismatch (which can happen when the game reorders the
    // flattened transform storage) leaves only the six legacy direct-anchor
    // bones and makes the skeleton permanently invisible.
    if (selectedScore.mismatchCount != 0) {
        const ParentScore& diagnosticScore = selectedInterpretation == 1
            ? boneScore : nodeScore;
        g_SKJH_SkeletonCandidateProbe.parentMismatchFirst =
            diagnosticScore.first;
        g_SKJH_SkeletonCandidateProbe.parentMismatchSecond =
            diagnosticScore.second;
        if (diagnosticScore.first >= 0 && diagnosticScore.second >= 0) {
            g_SKJH_SkeletonCandidateProbe.parentMismatchOriginalFirst =
                output.originalBoneIndices[diagnosticScore.first];
            g_SKJH_SkeletonCandidateProbe.parentMismatchOriginalSecond =
                output.originalBoneIndices[diagnosticScore.second];
            g_SKJH_SkeletonCandidateProbe.parentMismatchNativeFirst =
                output.transformIndices[diagnosticScore.first];
            g_SKJH_SkeletonCandidateProbe.parentMismatchNativeSecond =
                output.transformIndices[diagnosticScore.second];
        }
        g_SKJH_SkeletonBindingStage = "original_parent_mismatch_relaxed";
    }
    g_SKJH_SkeletonCandidateProbe.parentInterpretation =
        selectedInterpretation;
    const auto& selectedParentMap = selectedInterpretation == 0
        ? parentByNodeIndex : parentByBoneIndex;

    // Record native ancestry drift without treating the flattened storage
    // graph as the semantic skeleton. The native graph has already passed
    // its own address, cycle and depth validation and remains the pose source.
    ParentScore nativeScore;
    for (int first = 0; first < BONE_COUNT; ++first) {
        for (int second = 0; second < BONE_COUNT; ++second) {
            if (first == second) continue;
            const bool originalRelation = isAncestor(
                output.originalBoneIndices[first],
                output.originalBoneIndices[second], selectedParentMap);
            const bool nativeRelation = isAncestor(
                output.transformIndices[first],
                output.transformIndices[second], output.parentByIndex);
            if (originalRelation == nativeRelation) continue;
            ++nativeScore.mismatchCount;
            if (nativeScore.first < 0) {
                nativeScore.first = first;
                nativeScore.second = second;
            }
        }
    }
    g_SKJH_SkeletonCandidateProbe.parentNativeMismatchCount =
        nativeScore.mismatchCount;
    if (nativeScore.first >= 0) {
        g_SKJH_SkeletonCandidateProbe.parentMismatchFirst = nativeScore.first;
        g_SKJH_SkeletonCandidateProbe.parentMismatchSecond =
            nativeScore.second;
        g_SKJH_SkeletonCandidateProbe.parentMismatchOriginalFirst =
            output.originalBoneIndices[nativeScore.first];
        g_SKJH_SkeletonCandidateProbe.parentMismatchOriginalSecond =
            output.originalBoneIndices[nativeScore.second];
        g_SKJH_SkeletonCandidateProbe.parentMismatchNativeFirst =
            output.transformIndices[nativeScore.first];
        g_SKJH_SkeletonCandidateProbe.parentMismatchNativeSecond =
            output.transformIndices[nativeScore.second];
    }

    // Recheck complete named paths before publishing, guarding future map
    // changes from turning this validation into a partial membership test.
    // When the parent map had mismatches (relaxed path), a cycle or missing
    // parent is expected on the mismatched entries — record it but do not
    // abort, because the geometry compactness gate is the authoritative check.
    bool treeCycleDetected = false;
    for (const int32_t boneIndex : output.originalBoneIndices) {
        std::unordered_set<int32_t> path;
        int32_t current = boneIndex;
        for (int depth = 0; current >= 0 && depth <= nodeCount; ++depth) {
            if (!path.emplace(current).second) {
                treeCycleDetected = true;
                break;
            }
            const auto parent = selectedParentMap.find(current);
            if (parent == selectedParentMap.end()) {
                treeCycleDetected = true;
                break;
            }
            current = parent->second;
        }
        if (current >= 0) treeCycleDetected = true;
        if (treeCycleDetected) break;
    }
    if (treeCycleDetected &&
        g_SKJH_SkeletonBindingStage != "original_parent_mismatch_relaxed") {
        g_SKJH_SkeletonBindingStage = "original_tree_cycle";
        return false;
    }

    // Sample all points once before publishing the binding. This is a cheap
    // finite/compactness gate; ReadBoundSkeletonPose still takes the coherent
    // scatter snapshot used by the renderer on every subsequent frame.
    g_SKJH_SkeletonBindingStage = "original_geometry";
    std::array<FVector, BONE_COUNT> positions{};
    for (int bone = 0; bone < BONE_COUNT; ++bone) {
        const SKJH_UnityTransformResult sample =
            SKJH_ReadUnityTransform(output.managedTransforms[bone]);
        if (!sample.valid || !SKJH_IsFiniteVector(sample.position)) return false;
        positions[bone] = sample.position;
    }
    float maximumSeparationSq = 0.0f;
    for (int first = 0; first < BONE_COUNT; ++first) {
        for (int second = first + 1; second < BONE_COUNT; ++second) {
            const float dx = positions[first].X - positions[second].X;
            const float dy = positions[first].Y - positions[second].Y;
            const float dz = positions[first].Z - positions[second].Z;
            maximumSeparationSq = (std::max)(maximumSeparationSq,
                                              dx*dx + dy*dy + dz*dz);
        }
    }
    if (!std::isfinite(maximumSeparationSq) || maximumSeparationSq > 25.0f)
        return false;

    output.playerGo = playerGo;
    output.controller = controller;
    output.targetRootPoint = targetRootPoint;
    output.targetRootPointOffset = targetRootPointOffset;
    output.sourceKind = SKJH_SkeletonSourceKind::OriginalPointCache;
    output.originalTree = originalTree;
    output.pointCache = pointCache;
    output.originalNodeData = nodeData;
    output.originalTransformIdData = transformIdData;
    output.originalNodeCount = nodeCount;
    output.originalTransformIdCount = transformIdCount;
    output.nativeInstanceIdOffset = instanceIdOffset;
    output.transformData = commonTransformData;
    output.localTransforms = localTransforms;
    output.parentIndices = parentIndices;
    output.valid = true;
    g_SKJH_SkeletonBindingStage = "original_complete";
    return true;
}

inline bool SKJH_ResolvePlayerSkeletonBinding(
    DWORD64 playerGo, SKJH_PlayerSkeletonBinding& output) {
    output = {};
    g_SKJH_SkeletonBindingStage = "player_go";
    if (!Mem::IsUserAddress(playerGo)) return false;
    g_SKJH_SkeletonBindingStage = "controller";
    const DWORD64 controller = mem.Read<DWORD64>(
        playerGo + g_RuntimeOffsets.clientPlayerPartModelController);

    g_SKJH_SkeletonBindingStage = "controller_class";
    const std::string controllerClass = Mem::IsUserAddress(controller)
        ? SKJH_ReadKlassName(mem.Read<DWORD64>(controller)) : std::string{};
    const bool controllerClassValid = true;

    std::array<uint32_t, 2> candidates{{
        g_RuntimeOffsets.newPartModelControllerTargetRootPoint,
        g_RuntimeOffsets.partModelControllerTargetRootPoint,
    }};
    if (controllerClass == "PartModelController")
        std::swap(candidates[0], candidates[1]);

    g_SKJH_SkeletonCandidateStages = {{
        "not_attempted", "not_attempted", "not_attempted"}};
    const char* primaryFailure = "candidate_missing";
    for (size_t index = 0;
         controllerClassValid && Mem::IsUserAddress(controller) &&
         index < candidates.size(); ++index) {
        if (!candidates[index] ||
            (index && candidates[index] == candidates[index - 1])) {
            continue;
        }
        SKJH_PlayerSkeletonBinding candidate;
        if (!SKJH_TryResolvePlayerSkeletonBinding(
                playerGo, controller, candidates[index], candidate)) {
            const char* serializeFailure = g_SKJH_SkeletonBindingStage;
            SKJH_PlayerSkeletonBinding originalCandidate;
            if (SKJH_TryResolvePlayerSkeletonBindingOriginal(
                    playerGo, controller, candidates[index], originalCandidate)) {
                g_SKJH_SkeletonCandidateStages[index] = "original_complete";
                output = std::move(originalCandidate);
                return true;
            }
            // Preserve the more specific original-tree stage when it was
            // reached; otherwise retain the primary serialized-tree failure.
            g_SKJH_SkeletonCandidateStages[index] =
                g_SKJH_SkeletonBindingStage;
            if (index == 0) {
                primaryFailure = g_SKJH_SkeletonBindingStage;
                if (!primaryFailure ||
                    std::string(primaryFailure) == "original_tree_input") {
                    primaryFailure = serializeFailure;
                }
            }
            continue;
        }
        g_SKJH_SkeletonCandidateStages[index] = "complete";
        output = std::move(candidate);
        return true;
    }

    // BaseEntityGo exposes the same ObjectPointComponent directly. This is a
    // stable fallback when the controller implementation changes or its class
    // metadata is not resident.
    SKJH_PlayerSkeletonBinding directCandidate;
    if (SKJH_TryResolvePlayerSkeletonBinding(
            playerGo, playerGo,
            g_RuntimeOffsets.baseEntityGoObjectComponent,
            directCandidate)) {
        directCandidate.controller = controller;
        directCandidate.targetRootPointFromPlayerGo = true;
        g_SKJH_SkeletonCandidateStages[2] = "complete";
        output = std::move(directCandidate);
        return true;
    }
    const char* directSerializeFailure = g_SKJH_SkeletonBindingStage;
    SKJH_PlayerSkeletonBinding directOriginalCandidate;
    if (SKJH_TryResolvePlayerSkeletonBindingOriginal(
            playerGo, playerGo, g_RuntimeOffsets.baseEntityGoObjectComponent,
            directOriginalCandidate)) {
        directOriginalCandidate.controller = controller;
        directOriginalCandidate.targetRootPointFromPlayerGo = true;
        g_SKJH_SkeletonCandidateStages[2] = "original_complete";
        output = std::move(directOriginalCandidate);
        return true;
    }
    g_SKJH_SkeletonCandidateStages[2] = g_SKJH_SkeletonBindingStage;
    if (!g_SKJH_SkeletonBindingStage ||
        std::string(g_SKJH_SkeletonBindingStage) == "original_tree_input") {
        g_SKJH_SkeletonBindingStage = directSerializeFailure;
    }
    g_SKJH_SkeletonBindingStage = primaryFailure;
    return false;
}

enum class SKJH_SkeletonBindingState : uint8_t {
    Current,
    Changed,
    Unreadable,
};

// Verify the semantic source contents periodically.  Unity/IL2CPP can keep
// the same managed array addresses while replacing every node during a small
// model update; address/count checks then incorrectly retain an old mapping.
// This check is deliberately bounded to the twenty published bones plus the
// already-known native hierarchy and is rate-limited so it does not add a DMA
// read burst to every render/sample iteration.
inline bool SKJH_VerifySkeletonBindingGeneration(
    const SKJH_PlayerSkeletonBinding& binding) {
    if (!binding.valid || !binding.nodesArray &&
            binding.sourceKind == SKJH_SkeletonSourceKind::SerializeTree) {
        return false;
    }
    for (int bone = 0; bone < BONE_COUNT; ++bone) {
        const int32_t nodeIndex = binding.sourceNodeArrayIndices[bone];
        const int32_t sourceBone = binding.sourceBoneIndices[bone];
        if (nodeIndex < 0 || sourceBone < 0 ||
            !Mem::IsUserAddress(binding.sourceNodePointers[bone])) {
            return false;
        }

        DWORD64 node = 0;
        if (binding.sourceKind == SKJH_SkeletonSourceKind::SerializeTree) {
            if (!mem.Read(binding.nodesArray + 0x20 +
                              static_cast<DWORD64>(nodeIndex) * sizeof(DWORD64),
                          &node, sizeof(node)) ||
                node != binding.sourceNodePointers[bone] ||
                !Mem::IsUserAddress(node)) {
                return false;
            }
        } else {
            node = binding.originalNodeData +
                static_cast<DWORD64>(nodeIndex) * sizeof(int32_t) * 2;
            if (node != binding.sourceNodePointers[bone]) return false;
        }

        int32_t currentBone = -1;
        int32_t currentParent = -2;
        const uint32_t boneIndexOffset =
            binding.sourceKind == SKJH_SkeletonSourceKind::SerializeTree
                ? g_RuntimeOffsets.boneTreeNodeBoneIndex : 0u;
        const uint32_t parentIndexOffset =
            binding.sourceKind == SKJH_SkeletonSourceKind::SerializeTree
                ? g_RuntimeOffsets.boneTreeNodeParentIndex : 4u;
        if (!mem.Read(node + boneIndexOffset,
                      &currentBone, sizeof(currentBone)) ||
            !mem.Read(node + parentIndexOffset,
                      &currentParent, sizeof(currentParent)) ||
            currentBone != sourceBone ||
            currentParent != binding.sourceParentIndices[bone]) {
            return false;
        }

        if (binding.sourceKind == SKJH_SkeletonSourceKind::SerializeTree) {
            DWORD64 managed = 0;
            if (!mem.Read(binding.transformsArray + 0x20 +
                              static_cast<DWORD64>(sourceBone) * sizeof(DWORD64),
                          &managed, sizeof(managed)) ||
                managed != binding.managedTransforms[bone]) {
                return false;
            }
        } else {
            int32_t cachedInstanceId = 0;
            int32_t currentInstanceId = 0;
            if (!mem.Read(binding.originalTransformIdData +
                              static_cast<DWORD64>(sourceBone) * sizeof(int32_t),
                          &cachedInstanceId, sizeof(cachedInstanceId)) ||
                !SKJH_ReadNativeTransformInstanceId(
                    binding.managedTransforms[bone],
                    binding.nativeInstanceIdOffset, currentInstanceId) ||
                cachedInstanceId != currentInstanceId) {
                return false;
            }
        }

        DWORD64 nativeTransform = 0;
        DWORD64 transformData = 0;
        int32_t transformIndex = -1;
        if (!SKJH_ReadNativeTransformIdentity(
                binding.managedTransforms[bone], nativeTransform,
                transformData, transformIndex) ||
            transformData != binding.transformData ||
            transformIndex != binding.transformIndices[bone]) {
            return false;
        }
    }

    // Parent-index changes are already checked in the coherent scatter read
    // performed by SKJH_ReadBoundSkeletonPose; duplicating that whole list
    // here would add dozens of single DMA calls per actor.
    return true;
}

inline SKJH_SkeletonBindingState SKJH_PlayerSkeletonBindingState(
    const SKJH_PlayerSkeletonBinding& binding) {
    if (!binding.valid || !Mem::IsUserAddress(binding.playerGo))
        return SKJH_SkeletonBindingState::Changed;

    DWORD64 controller = binding.controller;
    if (!binding.targetRootPointFromPlayerGo &&
        !mem.Read(binding.playerGo +
                      g_RuntimeOffsets.clientPlayerPartModelController,
                  &controller, sizeof(controller))) {
        return SKJH_SkeletonBindingState::Unreadable;
    }
    if (!binding.targetRootPointFromPlayerGo &&
        controller != binding.controller) {
        return SKJH_SkeletonBindingState::Changed;
    }

    const DWORD64 owner = binding.targetRootPointFromPlayerGo
        ? binding.playerGo : controller;
    DWORD64 targetRootPoint = 0;
    if (!mem.Read(owner + binding.targetRootPointOffset,
                  &targetRootPoint, sizeof(targetRootPoint))) {
        return SKJH_SkeletonBindingState::Unreadable;
    }
    if (targetRootPoint != binding.targetRootPoint)
        return SKJH_SkeletonBindingState::Changed;

    if (binding.sourceKind == SKJH_SkeletonSourceKind::OriginalPointCache) {
        DWORD64 originalTree = 0;
        DWORD64 pointCache = 0;
        if (!mem.Read(targetRootPoint +
                          SKJH_SkeletonFallbackLayout::kOriginalBoneTree,
                      &originalTree, sizeof(originalTree)) ||
            !mem.Read(targetRootPoint +
                          SKJH_SkeletonFallbackLayout::kPointCache,
                      &pointCache, sizeof(pointCache))) {
            return SKJH_SkeletonBindingState::Unreadable;
        }
        if (originalTree != binding.originalTree ||
            pointCache != binding.pointCache) {
            return SKJH_SkeletonBindingState::Changed;
        }
        DWORD64 nodeData = 0;
        DWORD64 transformIdData = 0;
        int32_t nodeCount = 0;
        int32_t nodeCapacity = 0;
        int32_t transformIdCount = 0;
        int32_t transformIdCapacity = 0;
        if (!SKJH_ReadUnsafeListDescriptor(
                originalTree + SKJH_SkeletonFallbackLayout::kUnsafeBoneNodes,
                nodeData, nodeCount, nodeCapacity, BONE_COUNT, 4096) ||
            !SKJH_ReadUnsafeListDescriptor(
                originalTree +
                    SKJH_SkeletonFallbackLayout::kUnsafeTransformIds,
                transformIdData, transformIdCount, transformIdCapacity,
                BONE_COUNT, 4096)) {
            return SKJH_SkeletonBindingState::Unreadable;
        }
        const bool descriptorCurrent = nodeData == binding.originalNodeData &&
                       transformIdData == binding.originalTransformIdData &&
                       nodeCount == binding.originalNodeCount &&
                       transformIdCount == binding.originalTransformIdCount;
        if (!descriptorCurrent) return SKJH_SkeletonBindingState::Changed;
    } else {
        DWORD64 serializeTree = 0;
        DWORD64 nodesArray = 0;
        DWORD64 transformsArray = 0;
        if (!mem.Read(targetRootPoint +
                          g_RuntimeOffsets.objectPointSerializeBoneTree,
                      &serializeTree, sizeof(serializeTree)) ||
            !Mem::IsUserAddress(serializeTree) ||
            !mem.Read(serializeTree + g_RuntimeOffsets.boneTreeNodes,
                      &nodesArray, sizeof(nodesArray)) ||
            !mem.Read(serializeTree + g_RuntimeOffsets.boneTreeAllTransforms,
                      &transformsArray, sizeof(transformsArray))) {
            return SKJH_SkeletonBindingState::Unreadable;
        }
        if (serializeTree != binding.serializeTree ||
            nodesArray != binding.nodesArray ||
            transformsArray != binding.transformsArray) {
            return SKJH_SkeletonBindingState::Changed;
        }
    }

    // Normal pose sampling performs the coherent native identity and parent
    // checks in SKJH_ReadBoundSkeletonPose. Keep this fast path fail-open for
    // transient DMA short reads; the strict source-generation verifier remains
    // available to diagnostics without making every frame lose its binding.
    return SKJH_SkeletonBindingState::Current;
}

inline std::unordered_map<DWORD64, SKJH_PlayerSkeletonBinding>&
SKJH_PlayerSkeletonBindings() {
    static thread_local std::unordered_map<
        DWORD64, SKJH_PlayerSkeletonBinding> bindings;
    return bindings;
}

inline SKJH_PlayerSkeletonBinding* SKJH_GetPlayerSkeletonBinding(
    DWORD64 playerGo) {
    auto& bindings = SKJH_PlayerSkeletonBindings();
    auto existing = bindings.find(playerGo);
    if (existing != bindings.end()) {
        const auto state = SKJH_PlayerSkeletonBindingState(existing->second);
        if (state != SKJH_SkeletonBindingState::Changed)
            return &existing->second;
        bindings.erase(existing);
    }
    if (bindings.size() >= 2048) bindings.clear();
    SKJH_PlayerSkeletonBinding resolved;
    if (!SKJH_ResolvePlayerSkeletonBinding(playerGo, resolved)) return nullptr;
    return &bindings.emplace(playerGo, std::move(resolved)).first->second;
}

inline int SKJH_ReadBoundSkeletonPose(
    const SKJH_PlayerSkeletonBinding& binding,
    SKJH_BoneData bones[BONE_COUNT], bool* hierarchyChanged = nullptr) {
    if (hierarchyChanged) *hierarchyChanged = false;
    if (!binding.valid || binding.hierarchyIndices.empty()) return 0;

    // A model rebuild can replace TransformData while retaining the managed
    // tree object. Use one anchor to reject that stale binding before sampling.
    DWORD64 nativeTransform = 0;
    DWORD64 transformData = 0;
    int32_t transformIndex = -1;
    if (!mem.Read(binding.managedTransforms[BONE_HEAD] +
                      g_RuntimeOffsets.unityObjectCachedPtr,
                  &nativeTransform, sizeof(nativeTransform)) ||
        !Mem::IsUserAddress(nativeTransform) ||
        !mem.Read(nativeTransform + g_RuntimeOffsets.unityNativeData,
                  &transformData, sizeof(transformData)) ||
        !mem.Read(nativeTransform + g_RuntimeOffsets.unityNativeIndex,
                  &transformIndex, sizeof(transformIndex))) {
        return 0;
    }
    if (!Mem::IsUserAddress(transformData) ||
        transformData != binding.transformData ||
        transformIndex != binding.transformIndices[BONE_HEAD]) {
        if (hierarchyChanged) *hierarchyChanged = true;
        return 0;
    }
    DWORD64 localTransforms = 0;
    DWORD64 parentIndices = 0;
    if (!mem.Read(transformData + g_RuntimeOffsets.unityDataTransforms,
                  &localTransforms, sizeof(localTransforms)) ||
        !mem.Read(transformData + g_RuntimeOffsets.unityDataParents,
                  &parentIndices, sizeof(parentIndices))) {
        return 0;
    }
    if (localTransforms != binding.localTransforms ||
        parentIndices != binding.parentIndices) {
        if (hierarchyChanged) *hierarchyChanged = true;
        return 0;
    }

    const size_t hierarchyCount = binding.hierarchyIndices.size();
    std::vector<SKJH_UnityTransformNode> localNodes(hierarchyCount);
    std::vector<int32_t> currentParents(
        hierarchyCount, (std::numeric_limits<int32_t>::min)());
    VMMDLL_SCATTER_HANDLE scatter = mem.CreateScatter();
    if (!scatter) return 0;
    for (size_t slot = 0; slot < hierarchyCount; ++slot) {
        const DWORD64 index = static_cast<DWORD64>(
            binding.hierarchyIndices[slot]);
        mem.AddScatter(
            scatter,
            binding.localTransforms +
                index * g_RuntimeOffsets.unityTransformStride,
            &localNodes[slot], sizeof(localNodes[slot]));
        mem.AddScatter(scatter,
                       binding.parentIndices + index * sizeof(int32_t),
                       &currentParents[slot], sizeof(currentParents[slot]));
    }
    mem.ExecuteScatter(scatter);
    mem.CloseScatter(scatter);

    std::unordered_map<int32_t, size_t> slotByIndex;
    slotByIndex.reserve(hierarchyCount);
    std::vector<bool> nodeValid(hierarchyCount, false);
    bool anyParentChanged = false;
    for (size_t slot = 0; slot < hierarchyCount; ++slot) {
        const int32_t index = binding.hierarchyIndices[slot];
        slotByIndex.emplace(index, slot);
        const auto expectedParent = binding.parentByIndex.find(index);
        const SKJH_UnityTransformNode& node = localNodes[slot];
        const bool parentMatches = expectedParent !=
                binding.parentByIndex.end() &&
            currentParents[slot] == expectedParent->second;
        anyParentChanged = anyParentChanged ||
            (currentParents[slot] !=
                 (std::numeric_limits<int32_t>::min)() &&
             !parentMatches);
        SKJH_Quaternion rotation{node.qx, node.qy, node.qz, node.qw};
        const bool scaleValid = std::isfinite(node.sx) &&
            std::isfinite(node.sy) && std::isfinite(node.sz) &&
            std::fabs(node.sx) < 1000.0f &&
            std::fabs(node.sy) < 1000.0f &&
            std::fabs(node.sz) < 1000.0f;
        nodeValid[slot] = parentMatches && scaleValid &&
            SKJH_IsFiniteVector({node.px, node.py, node.pz}) &&
            SKJH_NormalizeQuaternion(rotation);
    }
    if (hierarchyChanged) *hierarchyChanged = anyParentChanged;

    int validCount = 0;
    for (int bone = 0; bone < BONE_COUNT; ++bone) {
        const auto localSlot = slotByIndex.find(
            binding.transformIndices[bone]);
        if (localSlot == slotByIndex.end() ||
            !nodeValid[localSlot->second]) {
            continue;
        }
        const SKJH_UnityTransformNode& local =
            localNodes[localSlot->second];
        FVector position(local.px, local.py, local.pz);
        SKJH_Quaternion rotation{local.qx, local.qy, local.qz, local.qw};
        if (!SKJH_NormalizeQuaternion(rotation)) continue;

        int32_t parent = currentParents[localSlot->second];
        bool valid = true;
        for (int depth = 0; parent >= 0 && depth < 128; ++depth) {
            const auto parentSlot = slotByIndex.find(parent);
            if (parentSlot == slotByIndex.end() ||
                !nodeValid[parentSlot->second]) {
                valid = false;
                break;
            }
            const SKJH_UnityTransformNode& parentNode =
                localNodes[parentSlot->second];
            const FVector scaled(position.X * parentNode.sx,
                                 position.Y * parentNode.sy,
                                 position.Z * parentNode.sz);
            SKJH_Quaternion parentRotation{
                parentNode.qx, parentNode.qy,
                parentNode.qz, parentNode.qw};
            if (!SKJH_NormalizeQuaternion(parentRotation)) {
                valid = false;
                break;
            }
            const FVector rotated = SKJH_RotateVector(
                parentRotation, scaled);
            position = {parentNode.px + rotated.X,
                        parentNode.py + rotated.Y,
                        parentNode.pz + rotated.Z};
            rotation = SKJH_MultiplyQuaternion(parentRotation, rotation);
            if (!SKJH_IsFiniteVector(position) ||
                !SKJH_NormalizeQuaternion(rotation)) {
                valid = false;
                break;
            }
            parent = currentParents[parentSlot->second];
        }
        if (!valid || parent >= 0) continue;
        bones[bone].worldPos = position;
        bones[bone].valid = true;
        ++validCount;
    }
    return validCount;
}

inline bool SKJH_ReadPlayerRoot(DWORD64 playerGo, DWORD64 entity,
                                FVector& rootPosition) {
    rootPosition = {};
    if (!SKJH_HasMatchingPlayerEntity(playerGo, entity)) return false;
    const DWORD64 managedTransform = mem.Read<DWORD64>(
        playerGo + g_RuntimeOffsets.basePlayerGoRootBone);
    SKJH_UnityTransformResult transform = SKJH_ReadUnityTransform(managedTransform);
    if (!transform.valid) transform = SKJH_ReadUnityTransform(managedTransform);
    if (!transform.valid || !SKJH_IsFiniteVector(transform.position)) return false;
    rootPosition = transform.position;
    return true;
}

struct SKJH_PlayerRootBatchRequest {
    DWORD64 playerGo = 0;
    DWORD64 entity = 0;
};

struct SKJH_PlayerRootBatchResult {
    FVector position{};
    bool valid = false;
    int hierarchyDepth = 0;
};

// Resolve all player roots with a small number of scatter round trips. The
// scalar SKJH_ReadUnityTransform path is reliable but costs several DMA calls
// per actor; at dozens of players that turns one root refresh into hundreds of
// milliseconds. This pipeline batches each dependency level for every actor.
inline size_t SKJH_ReadPlayerRootsBatch(
        const std::vector<SKJH_PlayerRootBatchRequest>& requests,
        std::vector<SKJH_PlayerRootBatchResult>& results) {
    results.assign(requests.size(), {});
    if (requests.empty() || !mem.hVMM || !mem.pid) return 0;

    struct Work {
        DWORD64 backReference = 0;
        DWORD64 managedTransform = 0;
        DWORD64 nativeTransform = 0;
        DWORD64 transformData = 0;
        DWORD64 transforms = 0;
        DWORD64 parents = 0;
        int32_t index = -1;
        int32_t parent = -2;
        int32_t nextParent = -2;
        SKJH_UnityTransformNode node{};
        SKJH_UnityTransformNode parentNode{};
        FVector position{};
        SKJH_Quaternion rotation{};
        int depth = 0;
        bool active = false;
    };
    std::vector<Work> work(requests.size());
    VMMDLL_SCATTER_HANDLE scatter = mem.CreateScatter();
    if (!scatter) return 0;

    for (size_t index = 0; index < requests.size(); ++index) {
        const auto& request = requests[index];
        if (!Mem::IsUserAddress(request.playerGo) ||
            !Mem::IsUserAddress(request.entity)) {
            continue;
        }
        mem.AddScatter(scatter,
            request.playerGo + g_RuntimeOffsets.basePlayerGoEntity,
            &work[index].backReference);
        mem.AddScatter(scatter,
            request.playerGo + g_RuntimeOffsets.basePlayerGoRootBone,
            &work[index].managedTransform);
    }
    mem.ExecuteScatter(scatter);

    for (size_t index = 0; index < requests.size(); ++index) {
        Work& value = work[index];
        const auto& request = requests[index];
        bool matching = value.backReference == request.entity;
        if (!matching && Mem::IsUserAddress(value.backReference)) {
            matching = SKJH_HasMatchingPlayerEntity(
                request.playerGo, request.entity);
        }
        if (!matching || !Mem::IsUserAddress(value.managedTransform)) continue;
        mem.AddScatter(scatter,
            value.managedTransform + g_RuntimeOffsets.unityObjectCachedPtr,
            &value.nativeTransform);
    }
    mem.ExecuteScatter(scatter);

    for (Work& value : work) {
        if (!Mem::IsUserAddress(value.nativeTransform)) continue;
        mem.AddScatter(scatter,
            value.nativeTransform + g_RuntimeOffsets.unityNativeData,
            &value.transformData);
        mem.AddScatter(scatter,
            value.nativeTransform + g_RuntimeOffsets.unityNativeIndex,
            &value.index);
    }
    mem.ExecuteScatter(scatter);

    for (Work& value : work) {
        if (!Mem::IsUserAddress(value.transformData) || value.index < 0 ||
            value.index > 1000000) {
            continue;
        }
        mem.AddScatter(scatter,
            value.transformData + g_RuntimeOffsets.unityDataTransforms,
            &value.transforms);
        mem.AddScatter(scatter,
            value.transformData + g_RuntimeOffsets.unityDataParents,
            &value.parents);
    }
    mem.ExecuteScatter(scatter);

    for (Work& value : work) {
        if (!Mem::IsUserAddress(value.transforms) ||
            !Mem::IsUserAddress(value.parents) || value.index < 0) {
            continue;
        }
        const DWORD64 nodeAddress = value.transforms +
            static_cast<DWORD64>(value.index) *
                g_RuntimeOffsets.unityTransformStride;
        const DWORD64 parentAddress = value.parents +
            static_cast<DWORD64>(value.index) * sizeof(int32_t);
        mem.AddScatter(scatter, nodeAddress, &value.node, sizeof(value.node));
        mem.AddScatter(scatter, parentAddress, &value.parent);
    }
    mem.ExecuteScatter(scatter);

    for (Work& value : work) {
        value.position = {value.node.px, value.node.py, value.node.pz};
        value.rotation = {
            value.node.qx, value.node.qy, value.node.qz, value.node.qw};
        value.active = Mem::IsUserAddress(value.transforms) &&
            Mem::IsUserAddress(value.parents) && value.index >= 0 &&
            SKJH_IsFiniteVector(value.position) &&
            SKJH_NormalizeQuaternion(value.rotation) &&
            value.parent >= -1 && value.parent <= 1000000;
    }

    for (int iteration = 0; iteration < 128; ++iteration) {
        bool anyParent = false;
        for (Work& value : work) {
            value.parentNode = {};
            value.nextParent = -2;
            if (!value.active || value.parent < 0) continue;
            anyParent = true;
            mem.AddScatter(scatter,
                value.transforms + static_cast<DWORD64>(value.parent) *
                    g_RuntimeOffsets.unityTransformStride,
                &value.parentNode, sizeof(value.parentNode));
            mem.AddScatter(scatter,
                value.parents + static_cast<DWORD64>(value.parent) *
                    sizeof(int32_t),
                &value.nextParent);
        }
        if (!anyParent) break;
        mem.ExecuteScatter(scatter);

        for (Work& value : work) {
            if (!value.active || value.parent < 0) continue;
            const FVector scale{
                value.parentNode.sx,
                value.parentNode.sy,
                value.parentNode.sz};
            SKJH_Quaternion parentRotation{
                value.parentNode.qx,
                value.parentNode.qy,
                value.parentNode.qz,
                value.parentNode.qw};
            if (!SKJH_IsFiniteVector(scale) ||
                !SKJH_NormalizeQuaternion(parentRotation) ||
                value.nextParent < -1 || value.nextParent > 1000000 ||
                value.nextParent == value.parent) {
                value.active = false;
                continue;
            }
            const FVector scaled{
                value.position.X * scale.X,
                value.position.Y * scale.Y,
                value.position.Z * scale.Z};
            const FVector rotated =
                SKJH_RotateVector(parentRotation, scaled);
            value.position = {
                value.parentNode.px + rotated.X,
                value.parentNode.py + rotated.Y,
                value.parentNode.pz + rotated.Z};
            value.rotation =
                SKJH_MultiplyQuaternion(parentRotation, value.rotation);
            ++value.depth;
            if (!SKJH_IsFiniteVector(value.position) ||
                !SKJH_NormalizeQuaternion(value.rotation)) {
                value.active = false;
                continue;
            }
            value.parent = value.nextParent;
        }
    }
    mem.CloseScatter(scatter);

    size_t validCount = 0;
    for (size_t index = 0; index < work.size(); ++index) {
        Work& value = work[index];
        if (!value.active || value.parent >= 0 || value.depth >= 128 ||
            !SKJH_IsFiniteVector(value.position)) {
            continue;
        }
        results[index].position = value.position;
        results[index].valid = true;
        results[index].hierarchyDepth = value.depth;
        ++validCount;
    }
    return validCount;
}

inline int SKJH_ReadPlayerBones(DWORD64 playerGo, DWORD64 entity,
                                const FVector& entityPosition,
                                SKJH_BoneData bones[BONE_COUNT],
                                FVector* sampledRoot = nullptr,
                                bool* sampledRootValid = nullptr) {
    for (int i = 0; i < BONE_COUNT; ++i) bones[i] = {};
    if (sampledRoot) *sampledRoot = {};
    if (sampledRootValid) *sampledRootValid = false;
    if (!SKJH_IsMatchingPlayerGo(playerGo, entity)) return 0;
    if (sampledRoot) {
        const bool rootValid = SKJH_ReadPlayerRoot(
            playerGo, entity, *sampledRoot);
        if (sampledRootValid) *sampledRootValid = rootValid;
    }

    int validCount = 0;
    bool hierarchyChanged = false;
    if (SKJH_PlayerSkeletonBinding* binding =
            SKJH_GetPlayerSkeletonBinding(playerGo)) {
        validCount = SKJH_ReadBoundSkeletonPose(
            *binding, bones, &hierarchyChanged);
    }
    if (hierarchyChanged)
        SKJH_PlayerSkeletonBindings().erase(playerGo);

    // The six legacy ClientPlayerGo fields remain a real-Transform fallback.
    // They fill only points missed by the coherent tree snapshot.
    constexpr int directBones[] = {
        BONE_HEAD, BONE_BODY, BONE_SPINE,
        BONE_LEFT_FOOT, BONE_RIGHT_FOOT, BONE_NECK
    };
    for (const int bone : directBones) {
        if (bones[bone].valid) continue;
        const DWORD64 offset = SKJH_PlayerBoneOffset(bone);
        if (!offset) continue;
        const DWORD64 managedTransform = mem.Read<DWORD64>(playerGo + offset);
        SKJH_UnityTransformResult transform =
            SKJH_ReadUnityTransform(managedTransform);
        if (!transform.valid)
            transform = SKJH_ReadUnityTransform(managedTransform);
        if (!transform.valid || !SKJH_IsFiniteVector(transform.position))
            continue;
        bones[bone].worldPos = transform.position;
        bones[bone].valid = true;
        ++validCount;
    }

    // Network properties can lag behind the interpolated GameObject by tens
    // of metres. This bound rejects wild pointers without treating that lag as
    // a missing pose.
    for (int bone = 0; bone < BONE_COUNT; ++bone) {
        if (!bones[bone].valid) continue;
        const float dx = bones[bone].worldPos.X - entityPosition.X;
        const float dy = bones[bone].worldPos.Y - entityPosition.Y;
        const float dz = bones[bone].worldPos.Z - entityPosition.Z;
        const float distanceSq = dx*dx + dy*dy + dz*dz;
        if (!std::isfinite(distanceSq) || distanceSq > 40000.0f) {
            bones[bone] = {};
            --validCount;
        }
    }

    // If the bound-skeleton pose was entirely rejected by the distance check
    // (e.g. the parent hierarchy changed and the local-to-world transform
    // produced wrong positions), retry with ONLY the six direct-anchor bones.
    // These read the managed Transform directly via SKJH_ReadUnityTransform,
    // which is independent of the cached parent hierarchy and always correct.
    if (validCount == 0) {
        for (const int bone : directBones) {
            if (bones[bone].valid) continue;
            const DWORD64 offset = SKJH_PlayerBoneOffset(bone);
            if (!offset) continue;
            const DWORD64 managedTransform = mem.Read<DWORD64>(playerGo + offset);
            SKJH_UnityTransformResult transform =
                SKJH_ReadUnityTransform(managedTransform);
            if (!transform.valid)
                transform = SKJH_ReadUnityTransform(managedTransform);
            if (!transform.valid || !SKJH_IsFiniteVector(transform.position))
                continue;
            bones[bone].worldPos = transform.position;
            bones[bone].valid = true;
            ++validCount;
        }
    }

    // Validate all accepted points as one compact humanoid. On a torn native
    // snapshot, leave this generation empty so the caller's per-bone last-good
    // cache keeps the previous coherent pose.
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
    if (!std::isfinite(maximumSeparationSq) || maximumSeparationSq > 25.0f) {
        for (int bone = 0; bone < BONE_COUNT; ++bone) bones[bone] = {};
        return 0;
    }
    return validCount;
}
