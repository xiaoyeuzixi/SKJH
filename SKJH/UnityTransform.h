#pragma once

#include "Mem.h"
#include "Offset.h"
#include "GameMatrix.h"
#include <cmath>
#include <cstdint>

struct SKJH_Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct SKJH_UnityTransformResult {
    FVector position = {0, 0, 0};
    SKJH_Quaternion rotation;
    bool valid = false;
    int hierarchyDepth = 0;
};

struct SKJH_UnityTransformNode {
    float px, py, pz, positionPad;
    float qx, qy, qz, qw;
    float sx, sy, sz, scalePad;
};
static_assert(sizeof(SKJH_UnityTransformNode) == 0x30,
              "Unity transform node layout must be 0x30 bytes");

inline bool SKJH_IsFiniteVector(const FVector& value) {
    return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z) &&
           std::fabs(value.X) < 1.0e7f && std::fabs(value.Y) < 1.0e7f &&
           std::fabs(value.Z) < 1.0e7f;
}

inline SKJH_Quaternion SKJH_MultiplyQuaternion(const SKJH_Quaternion& a,
                                                const SKJH_Quaternion& b) {
    return {
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
    };
}

inline FVector SKJH_RotateVector(const SKJH_Quaternion& q, const FVector& v) {
    const FVector u(q.x, q.y, q.z);
    const float dotUV = u.Dot(v);
    const float dotUU = u.Dot(u);
    const FVector cross(
        u.Y * v.Z - u.Z * v.Y,
        u.Z * v.X - u.X * v.Z,
        u.X * v.Y - u.Y * v.X);
    return FVector(
        2.0f * dotUV * u.X + (q.w*q.w - dotUU) * v.X + 2.0f*q.w*cross.X,
        2.0f * dotUV * u.Y + (q.w*q.w - dotUU) * v.Y + 2.0f*q.w*cross.Y,
        2.0f * dotUV * u.Z + (q.w*q.w - dotUU) * v.Z + 2.0f*q.w*cross.Z);
}

inline bool SKJH_NormalizeQuaternion(SKJH_Quaternion& q) {
    const float lengthSq = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (!std::isfinite(lengthSq) || lengthSq < 0.0001f || lengthSq > 100.0f) return false;
    const float inverse = 1.0f / std::sqrt(lengthSq);
    q.x *= inverse; q.y *= inverse; q.z *= inverse; q.w *= inverse;
    return true;
}

inline bool SKJH_ReadTransformNode(DWORD64 array, int32_t index,
                                   SKJH_UnityTransformNode& node) {
    if (!Mem::IsUserAddress(array) || index < 0 || index > 1000000) return false;
    return mem.Read(array + static_cast<DWORD64>(index) *
                    g_RuntimeOffsets.unityTransformStride,
                    &node, sizeof(node));
}

inline SKJH_UnityTransformResult SKJH_ReadUnityTransform(DWORD64 managedTransform) {
    SKJH_UnityTransformResult result;
    if (!Mem::IsUserAddress(managedTransform)) return result;

    const DWORD64 nativeTransform = mem.Read<DWORD64>(
        managedTransform + g_RuntimeOffsets.unityObjectCachedPtr);
    if (!Mem::IsUserAddress(nativeTransform)) return result;
    const DWORD64 transformData = mem.Read<DWORD64>(
        nativeTransform + g_RuntimeOffsets.unityNativeData);
    int32_t index = -1;
    if (!mem.Read(nativeTransform + g_RuntimeOffsets.unityNativeIndex,
                  &index, sizeof(index))) return result;
    if (!Mem::IsUserAddress(transformData) || index < 0 || index > 1000000) return result;

    const DWORD64 transforms = mem.Read<DWORD64>(
        transformData + g_RuntimeOffsets.unityDataTransforms);
    const DWORD64 parents = mem.Read<DWORD64>(
        transformData + g_RuntimeOffsets.unityDataParents);
    if (!Mem::IsUserAddress(transforms) || !Mem::IsUserAddress(parents)) return result;

    SKJH_UnityTransformNode node{};
    if (!SKJH_ReadTransformNode(transforms, index, node)) return result;
    result.position = {node.px, node.py, node.pz};
    result.rotation = {node.qx, node.qy, node.qz, node.qw};
    if (!SKJH_IsFiniteVector(result.position) || !SKJH_NormalizeQuaternion(result.rotation))
        return {};

    int32_t parent = -2;
    if (!mem.Read(parents + static_cast<DWORD64>(index) * sizeof(int32_t),
                  &parent, sizeof(parent))) return {};
    int depth = 0;
    while (parent >= 0 && depth++ < 128) {
        SKJH_UnityTransformNode parentNode{};
        if (!SKJH_ReadTransformNode(transforms, parent, parentNode)) return {};
        const FVector scale(parentNode.sx, parentNode.sy, parentNode.sz);
        const FVector scaled(result.position.X * scale.X,
                             result.position.Y * scale.Y,
                             result.position.Z * scale.Z);
        SKJH_Quaternion parentRotation{
            parentNode.qx, parentNode.qy, parentNode.qz, parentNode.qw};
        if (!SKJH_IsFiniteVector(scale) || !SKJH_NormalizeQuaternion(parentRotation)) return {};
        const FVector rotated = SKJH_RotateVector(parentRotation, scaled);
        result.position = {
            parentNode.px + rotated.X,
            parentNode.py + rotated.Y,
            parentNode.pz + rotated.Z
        };
        result.rotation = SKJH_MultiplyQuaternion(parentRotation, result.rotation);
        if (!SKJH_IsFiniteVector(result.position) || !SKJH_NormalizeQuaternion(result.rotation))
            return {};
        int32_t next = -2;
        if (!mem.Read(parents + static_cast<DWORD64>(parent) * sizeof(int32_t),
                      &next, sizeof(next))) return {};
        if (next == parent) return {};
        parent = next;
    }
    if (parent >= 0 || depth >= 128) return {};
    result.valid = true;
    result.hierarchyDepth = depth;
    return result;
}
