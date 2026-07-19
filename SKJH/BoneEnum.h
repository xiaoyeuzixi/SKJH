#pragma once
/*
 * BoneEnum.h — SKJH 骨骼索引定义
 * 用于 ESP 骨骼连线绘制
 * IL2CPP ClientPlayerGo 骨骼偏移
 */

// SKJH 骨骼索引 (与 SKJH_Skeleton.h 的 SKJH_Bone 对应)
enum SKJH_Bones {
    BONE_HEAD       = 0,   // 头部  ClientPlayerGo+0xD8
    BONE_BODY       = 1,   // 躯干  ClientPlayerGo+0xE0
    BONE_SPINE      = 2,   // 脊柱  ClientPlayerGo+0xE8
    BONE_LEFT_FOOT  = 3,   // 左脚  ClientPlayerGo+0xF0
    BONE_RIGHT_FOOT = 4,   // 右脚  ClientPlayerGo+0xF8
    BONE_NECK       = 5,   // 颈部  ClientPlayerGo+0x100
    BONE_COUNT      = 6,
};

// 骨骼连接关系 (绘制骨架用)
// {骨骼A, 骨骼B}
inline const int BoneConnections[][2] = {
    {BONE_NECK,       BONE_HEAD},        // 颈→头
    {BONE_BODY,       BONE_NECK},        // 躯干→颈
    {BONE_SPINE,      BONE_BODY},        // 脊柱→躯干
    {BONE_BODY,       BONE_LEFT_FOOT},   // 躯干→左脚
    {BONE_BODY,       BONE_RIGHT_FOOT},  // 躯干→右脚
};
constexpr int BoneConnectionCount = 5;

// 骨骼名称
inline const char* const BoneNames[BONE_COUNT] = {
    "Head", "Body", "Spine", "LeftFoot", "RightFoot", "Neck"
};
