#pragma once

// Live third-person Transform points. The first six numeric values preserve
// the existing aim/config ABI; BONE_BODY is the upper torso (Bip01 Spine2).
enum SKJH_Bones {
    BONE_HEAD       = 0,
    BONE_BODY       = 1,
    BONE_SPINE2     = BONE_BODY,
    BONE_SPINE      = 2,
    BONE_LEFT_FOOT  = 3,
    BONE_RIGHT_FOOT = 4,
    BONE_NECK       = 5,
    BONE_PELVIS     = 6,
    BONE_SPINE1     = 7,
    BONE_LEFT_CLAVICLE = 8,
    BONE_LEFT_UPPER_ARM = 9,
    BONE_LEFT_FOREARM   = 10,
    BONE_LEFT_HAND      = 11,
    BONE_RIGHT_CLAVICLE = 12,
    BONE_RIGHT_UPPER_ARM = 13,
    BONE_RIGHT_FOREARM   = 14,
    BONE_RIGHT_HAND      = 15,
    BONE_LEFT_THIGH      = 16,
    BONE_LEFT_CALF       = 17,
    BONE_RIGHT_THIGH     = 18,
    BONE_RIGHT_CALF      = 19,
    BONE_COUNT           = 20,
};

// Connections follow the hierarchy stored in SerializeBoneSkeletonTree.
inline constexpr int BoneConnections[][2] = {
    {BONE_HEAD, BONE_NECK},
    {BONE_NECK, BONE_BODY},
    {BONE_BODY, BONE_SPINE1},
    {BONE_SPINE1, BONE_SPINE},
    {BONE_SPINE, BONE_PELVIS},

    {BONE_BODY, BONE_LEFT_CLAVICLE},
    {BONE_LEFT_CLAVICLE, BONE_LEFT_UPPER_ARM},
    {BONE_LEFT_UPPER_ARM, BONE_LEFT_FOREARM},
    {BONE_LEFT_FOREARM, BONE_LEFT_HAND},

    {BONE_BODY, BONE_RIGHT_CLAVICLE},
    {BONE_RIGHT_CLAVICLE, BONE_RIGHT_UPPER_ARM},
    {BONE_RIGHT_UPPER_ARM, BONE_RIGHT_FOREARM},
    {BONE_RIGHT_FOREARM, BONE_RIGHT_HAND},

    {BONE_PELVIS, BONE_LEFT_THIGH},
    {BONE_LEFT_THIGH, BONE_LEFT_CALF},
    {BONE_LEFT_CALF, BONE_LEFT_FOOT},

    {BONE_PELVIS, BONE_RIGHT_THIGH},
    {BONE_RIGHT_THIGH, BONE_RIGHT_CALF},
    {BONE_RIGHT_CALF, BONE_RIGHT_FOOT},
};
inline constexpr int BoneConnectionCount =
    static_cast<int>(sizeof(BoneConnections) / sizeof(BoneConnections[0]));
static_assert(BONE_COUNT == 20, "The live humanoid pose ABI must expose 20 points");
static_assert(BoneConnectionCount == BONE_COUNT - 1,
              "The humanoid hierarchy must remain a complete tree");

inline constexpr const char* BoneNames[BONE_COUNT] = {
    "Bip01 Head",
    "Bip01 Spine2",
    "Bip01 Spine",
    "Bip01 L Foot",
    "Bip01 R Foot",
    "Bip01 Neck",
    "Bip01 Pelvis",
    "Bip01 Spine1",
    "Bip01 L Clavicle",
    "Bip01 L UpperArm",
    "Bip01 L Forearm",
    "Bip01 L Hand",
    "Bip01 R Clavicle",
    "Bip01 R UpperArm",
    "Bip01 R Forearm",
    "Bip01 R Hand",
    "Bip01 L Thigh",
    "Bip01 L Calf",
    "Bip01 R Thigh",
    "Bip01 R Calf",
};
