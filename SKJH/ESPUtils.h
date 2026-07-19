#pragma once

#include "Mem.h"
#include "Offset.h"
#include "GameMatrix.h"
#include "SKJH_Property.h"
#include "SKJH_Entity.h"
#include "SKJH_Skeleton.h"
#include "UnityTransform.h"
#include "ImGui/imgui.h"
#include <cmath>

inline void StrokeText(ImDrawList* drawList, const char* text, const ImVec2& position,
                       ImU32 outline, ImU32 fill) {
    if (!drawList || !text || !text[0]) return;
    drawList->AddText(ImVec2(position.x - 1.0f, position.y), outline, text);
    drawList->AddText(ImVec2(position.x + 1.0f, position.y), outline, text);
    drawList->AddText(ImVec2(position.x, position.y - 1.0f), outline, text);
    drawList->AddText(ImVec2(position.x, position.y + 1.0f), outline, text);
    drawList->AddText(position, fill, text);
}

inline float CalcTextWidth(const char* text) {
    return ImGui::CalcTextSize(text ? text : "").x;
}

struct SKJH_CameraData {
    FVector camLoc = {0, 0, 0};
    FVector camRot = {0, 0, 0};
    FVector localPos = {0, 0, 0};
    FVector right = {1, 0, 0};
    FVector up = {0, 1, 0};
    FVector forward = {0, 0, 1};
    float camFov = 90.0f;
    bool valid = false;
};

inline FVector SKJH_Cross(const FVector& a, const FVector& b) {
    return {a.Y*b.Z - a.Z*b.Y,
            a.Z*b.X - a.X*b.Z,
            a.X*b.Y - a.Y*b.X};
}

inline bool SKJH_NormalizeVector(FVector& value) {
    const float lengthSq = value.Dot(value);
    if (!std::isfinite(lengthSq) || lengthSq < 0.0001f) return false;
    const float inverse = 1.0f / std::sqrt(lengthSq);
    value.X *= inverse; value.Y *= inverse; value.Z *= inverse;
    return true;
}

inline void SKJH_BuildUnityBasis(float pitchDegrees, float yawDegrees,
                                 FVector& right, FVector& up, FVector& forward) {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    const float pitch = pitchDegrees * kDegToRad;
    const float yaw = yawDegrees * kDegToRad;
    const float cosPitch = std::cos(pitch);
    forward = {std::sin(yaw) * cosPitch, -std::sin(pitch), std::cos(yaw) * cosPitch};
    right = {std::cos(yaw), 0.0f, -std::sin(yaw)};
    up = SKJH_Cross(forward, right);
    SKJH_NormalizeVector(forward);
    SKJH_NormalizeVector(right);
    SKJH_NormalizeVector(up);
}

inline bool SKJH_ReadCameraLocalPosition(DWORD64 entity, FVector& position) {
    position = {0, 0, 0};
    if (!Mem::IsUserAddress(entity)) return false;
    DWORD64 klass = 0;
    if (!mem.Read(entity, &klass, sizeof(klass)) || !Mem::IsUserAddress(klass))
        return false;

    const int32_t posX = SKJH_PropertyIdOr(klass, "PosX", PropId::PosX);
    const int32_t posY = SKJH_PropertyIdOr(klass, "PosY", PropId::PosY);
    const int32_t posZ = SKJH_PropertyIdOr(klass, "PosZ", PropId::PosZ);
    if (!SKJH_GetPropertyValue(entity, posX, position.X) ||
        !SKJH_GetPropertyValue(entity, posY, position.Y) ||
        !SKJH_GetPropertyValue(entity, posZ, position.Z)) {
        position = {0, 0, 0};
        return false;
    }
    return SKJH_IsFiniteVector(position);
}

inline SKJH_CameraData SKJH_ReadCamera() {
    SKJH_CameraData camera;
    const DWORD64 fields = SKJH_GetMcStaticFields();
    if (!fields) return camera;
    const DWORD64 manager = mem.Read<DWORD64>(fields + g_RuntimeOffsets.mcCamera);
    if (!Mem::IsUserAddress(manager)) return camera;
    const DWORD64 controller = mem.Read<DWORD64>(
        manager + g_RuntimeOffsets.mgrCameraController);
    if (!Mem::IsUserAddress(controller)) return camera;

    const bool rotationValid =
        mem.Read(controller + g_RuntimeOffsets.cameraYaw,
                 &camera.camRot.Y, sizeof(camera.camRot.Y)) &&
        mem.Read(controller + g_RuntimeOffsets.cameraPitch,
                 &camera.camRot.X, sizeof(camera.camRot.X)) &&
        mem.Read(controller + g_RuntimeOffsets.cameraRoll,
                 &camera.camRot.Z, sizeof(camera.camRot.Z)) &&
        SKJH_IsFiniteVector(camera.camRot);
    float controllerFov = 0.0f;
    if (!mem.Read(controller + g_RuntimeOffsets.cameraCurrentFov,
                  &controllerFov, sizeof(controllerFov)) ||
        !std::isfinite(controllerFov) || controllerFov < 25.0f ||
        controllerFov > 140.0f) {
        return camera;
    }
    camera.camFov = controllerFov;

    const DWORD64 localPlayer = SKJH_GetLocalPlayer();
    if (!SKJH_ReadCameraLocalPosition(localPlayer, camera.localPos)) return camera;

    const DWORD64 state = mem.Read<DWORD64>(controller + g_RuntimeOffsets.cameraNowState);
    const DWORD64 sceneTransform = Mem::IsUserAddress(state)
        ? mem.Read<DWORD64>(state + g_RuntimeOffsets.baseCameraTransform) : 0;
    SKJH_UnityTransformResult transform = SKJH_ReadUnityTransform(sceneTransform);
    if (!transform.valid) {
        const DWORD64 myPlayerManager = mem.Read<DWORD64>(
            fields + g_RuntimeOffsets.mcMyPlayer);
        const DWORD64 boneManager = Mem::IsUserAddress(myPlayerManager)
            ? mem.Read<DWORD64>(myPlayerManager + g_RuntimeOffsets.mgrMyPlayerBoneMgr) : 0;
        const DWORD64 cameraLocator = Mem::IsUserAddress(boneManager)
            ? mem.Read<DWORD64>(boneManager + g_RuntimeOffsets.playerBoneCamera) : 0;
        transform = SKJH_ReadUnityTransform(cameraLocator);
    }
    if (transform.valid) {
        camera.camLoc = transform.position;
        camera.right = SKJH_RotateVector(transform.rotation, {1, 0, 0});
        camera.up = SKJH_RotateVector(transform.rotation, {0, 1, 0});
        camera.forward = SKJH_RotateVector(transform.rotation, {0, 0, 1});
    } else if (rotationValid && SKJH_IsFiniteVector(camera.localPos) &&
               (camera.localPos.X != 0 || camera.localPos.Y != 0 || camera.localPos.Z != 0)) {
        camera.camLoc = camera.localPos;
        camera.camLoc.Y += 1.7f;
        SKJH_BuildUnityBasis(camera.camRot.X, camera.camRot.Y,
                             camera.right, camera.up, camera.forward);
    } else {
        return camera;
    }

    camera.valid = SKJH_IsFiniteVector(camera.camLoc) &&
        SKJH_IsFiniteVector(camera.localPos) &&
        SKJH_NormalizeVector(camera.right) && SKJH_NormalizeVector(camera.up) &&
        SKJH_NormalizeVector(camera.forward);
    return camera;
}

// Unity uses X=right, Y=up, Z=forward. Camera.fieldOfView is vertical FOV.
inline bool SKJH_ProjectWorldToScreen(const FVector& world,
                                      const SKJH_CameraData& camera,
                                      int screenWidth, int screenHeight,
                                      FVector2D& output, bool requireOnScreen) {
    output = {0, 0};
    if (!camera.valid || screenWidth <= 0 || screenHeight <= 0 ||
        !SKJH_IsFiniteVector(world)) return false;
    const FVector delta = world - camera.camLoc;
    const float depth = delta.Dot(camera.forward);
    if (!std::isfinite(depth) || depth < 0.05f || depth > 100000.0f) return false;
    const float horizontal = delta.Dot(camera.right);
    const float vertical = delta.Dot(camera.up);
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    const float tangent = std::tan(camera.camFov * kDegToRad * 0.5f);
    if (!std::isfinite(tangent) || tangent <= 0.001f) return false;
    const float centerX = screenWidth * 0.5f;
    const float centerY = screenHeight * 0.5f;
    const float focal = centerY / tangent;
    output.X = centerX + horizontal * focal / depth;
    output.Y = centerY - vertical * focal / depth;
    if (!std::isfinite(output.X) || !std::isfinite(output.Y)) return false;
    return !requireOnScreen ||
           (output.X >= 0.0f && output.Y >= 0.0f &&
            output.X <= static_cast<float>(screenWidth) &&
            output.Y <= static_cast<float>(screenHeight));
}

inline bool SKJH_W2S(const FVector& world, const SKJH_CameraData& camera,
                     int screenWidth, int screenHeight, FVector2D& output) {
    return SKJH_ProjectWorldToScreen(world, camera, screenWidth, screenHeight,
                                     output, true);
}

inline bool SKJH_W2SUnclipped(const FVector& world,
                              const SKJH_CameraData& camera,
                              int screenWidth, int screenHeight,
                              FVector2D& output) {
    return SKJH_ProjectWorldToScreen(world, camera, screenWidth, screenHeight,
                                      output, false);
}

inline bool SKJH_IsScreenPointNear(const FVector2D& point,
                                   int screenWidth, int screenHeight,
                                   float margin = 0.0f) {
    return screenWidth > 0 && screenHeight > 0 && margin >= 0.0f &&
        std::isfinite(point.X) && std::isfinite(point.Y) &&
        point.X >= -margin && point.Y >= -margin &&
        point.X <= static_cast<float>(screenWidth) + margin &&
        point.Y <= static_cast<float>(screenHeight) + margin;
}

inline bool SKJH_ClipScreenLine(FVector2D& start, FVector2D& end,
                                int screenWidth, int screenHeight) {
    const float dx = end.X - start.X;
    const float dy = end.Y - start.Y;
    float first = 0.0f;
    float last = 1.0f;
    const auto clip = [&first, &last](float p, float q) {
        if (std::fabs(p) < 1.0e-6f) return q >= 0.0f;
        const float ratio = q / p;
        if (p < 0.0f) {
            if (ratio > last) return false;
            if (ratio > first) first = ratio;
        } else {
            if (ratio < first) return false;
            if (ratio < last) last = ratio;
        }
        return true;
    };
    if (!clip(-dx, start.X) ||
        !clip(dx, static_cast<float>(screenWidth) - start.X) ||
        !clip(-dy, start.Y) ||
        !clip(dy, static_cast<float>(screenHeight) - start.Y)) {
        return false;
    }
    const FVector2D original = start;
    if (last < 1.0f) {
        end.X = original.X + last * dx;
        end.Y = original.Y + last * dy;
    }
    if (first > 0.0f) {
        start.X = original.X + first * dx;
        start.Y = original.Y + first * dy;
    }
    return true;
}
