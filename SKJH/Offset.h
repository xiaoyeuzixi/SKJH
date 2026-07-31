#pragma once
#include "SdkResolver.h"
/*
 * Offset.h — SKJH (失控进化) IL2CPP 内存偏移定义
 * 基于 DogeDebuggerMCP 内存验证 + dump.cs IL2CPP 结构确认
 * 日期: 2026-06-23
 */

// ===================== 模块 =====================
// 进程名: SKJH.exe
// 核心模块: GameAssembly.dll (GA.dll)
// 无国服/国际服区分, 统一偏移

// ===================== 全局访问链 =====================
// 实体字典: [GA.dll + GA_EntityDict] → +0xB8 → +0x00 → +0x18 → Dictionary
// Current values are loaded from script.json. These aliases keep the older
// call sites readable while removing the per-version source edit.
#define GA_EntityDict (g_RuntimeOffsets.entityManagerTypeInfo)

// Mc 管理器: [GA.dll + Mc_GA_Ptr] → +0xB8 → staticFields
//   staticFields + 0x10 = MgrEntity
//   staticFields + 0x50 = MgrMyPlayer
//   staticFields + 0x80 = MgrCamera
#define Mc_GA_Ptr (g_RuntimeOffsets.mcTypeInfo)

// ===================== Dictionary<long, EntityBase> =====================
constexpr int Dict_Entries   = 0x18;  // → Entry[]
constexpr int Dict_Count     = 0x20;  // int32
constexpr int Dict_FreeCount = 0x28;  // int32

// ===================== IL2CPP 数组头 =====================
constexpr int Array_Length   = 0x18;  // 元素个数
constexpr int Array_Data     = 0x20;  // 数据区起始

// ===================== Entry (stride=0x18) =====================
constexpr int Entry_Stride   = 0x18;
constexpr int Entry_HashCode = 0x00;  // int32, <0 = 空槽
constexpr int Entry_Key      = 0x08;  // int64 = EntityId
constexpr int Entry_Value    = 0x10;  // 指针 = entity*

// ===================== EntityBase =====================
constexpr int Entity_Klass    = 0x00;  // IL2CPP类指针
constexpr int Entity_DataSet  = 0x20;  // ArrayDataSet*
constexpr int Entity_EntityId = 0x30;  // int64

// ===================== ArrayDataSet =====================
constexpr int DataSet_ValProps       = 0x18;  // PropArray<ValTypeValue>*
constexpr int DataSet_PropertyId2Index = 0x30; // int[] (映射表)

// ===================== PropArray =====================
constexpr int PropArray_Array = 0x10;  // ValTypeValue[]*

// ===================== ValTypeValue (stride=0x10) =====================
constexpr int ValTypeValue_Size  = 0x10;  // 16字节
constexpr int ValTypeValue_Float = 0x00;  // float值 (前4字节)
constexpr int ValTypeValue_Type  = 0x08;  // byte (3=float, 2=int64, 6=int32)

// ===================== 属性ID =====================
constexpr int Prop_PosX  = 0x02;
constexpr int Prop_PosY  = 0x03;
constexpr int Prop_PosZ  = 0x04;
constexpr int Prop_RotX  = 0x05;
constexpr int Prop_RotY  = 0x06;
constexpr int Prop_RotZ  = 0x07;
constexpr int Prop_Hp    = 0xB7;   // 183
constexpr int Prop_MaxHp = 0xB8;   // 184

// ===================== PlayerEntity 直接字段 =====================
constexpr int Player_PosX_Smooth = 0x60;  // float
constexpr int Player_PosY_Smooth = 0x64;
constexpr int Player_PosZ_Smooth = 0x68;
constexpr int Player_Health      = 0x110; // float (BaseCombatEntity)

// ===================== MgrMyPlayer =====================
constexpr int MgrMyPlayer_MyEntityLocal = 0x20;  // PlayerEntity*
constexpr int MgrMyPlayer_TpPlayerGo    = 0x50;  // ClientPlayerGo*
constexpr int MgrMyPlayer_BoneManager   = 0xA0;  // PlayerBoneManager*

// ===================== ClientPlayerGo 骨骼偏移 =====================
constexpr int PlayerGo_HeadBone      = 0xD8;  // Transform*
constexpr int PlayerGo_BodyBone      = 0xE0;
constexpr int PlayerGo_SpineBone     = 0xE8;
constexpr int PlayerGo_LeftFootBone  = 0xF0;
constexpr int PlayerGo_RightFootBone = 0xF8;
constexpr int PlayerGo_Bip01Neck     = 0x100;

// ===================== PlayerBoneManager =====================
constexpr int BoneMgr_CameraLocator    = 0x18;  // Transform*
constexpr int BoneMgr_DirectionLocator = 0x20;
constexpr int BoneMgr_ViewPoint        = 0x28;
constexpr int BoneMgr_BaseLocator      = 0x30;

// ===================== 相机 =====================
constexpr int MgrCamera_StateController = 0x18;  // CameraStateController*
constexpr int CameraCtrl_NowState       = 0x10;  // BaseCameraState*
constexpr int CameraCtrl_CameraYaw      = 0xA4;  // float
constexpr int CameraCtrl_CameraPitch    = 0xA8;
constexpr int CameraCtrl_CameraRoll     = 0xAC;

// BaseCameraState
constexpr int CamState_SceneCamera       = 0x18;  // Camera*
constexpr int CamState_SceneCameraTrans  = 0x20;  // Transform*

// URP CameraData
constexpr int URP_ViewMatrix       = 0x08;  // 64字节 (Matrix4x4)
constexpr int URP_ProjectionMatrix = 0x48;  // 64字节
constexpr int URP_Camera           = 0x108; // Camera*

// ===================== Unity Transform =====================
constexpr int Transform_LocalPos = 0x90;  // Vector3 (可能因版本不同)

// ===================== 版本基址 (兼容main.cpp的extern) =====================
// SKJH 无国服/国际服区分, 保留空值兼容旧代码
constexpr DWORD64 BaseWorld_CN = 0;
constexpr DWORD64 BaseName_CN  = 0;
constexpr DWORD64 NameKey_CN   = 0;
constexpr DWORD64 BaseWorld_GL = 0;
constexpr DWORD64 BaseName_GL  = 0;
constexpr DWORD64 NameKey_GL   = 0;
