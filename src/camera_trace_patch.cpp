#include "generated/ACRE_init.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

#include <rex/logging/macros.h>
#include <rex/system/kernel_state.h>

PPC_EXTERN_IMPORT(__imp__UpdateCameraModeA);
PPC_EXTERN_IMPORT(__imp__UpdateCameraModeAOwner);
PPC_EXTERN_IMPORT(__imp__UpdateCameraModeB);
PPC_EXTERN_IMPORT(__imp__UpdateCameraModeBOwner);
PPC_EXTERN_IMPORT(__imp__UpdateCameraModeBWrapper);
PPC_EXTERN_IMPORT(__imp__UpdateCameraFollower);
PPC_EXTERN_IMPORT(__imp__sub_824167E8);

namespace {

constexpr uint32_t kOwnerBasisCacheOffset = 752;
constexpr uint32_t kOwnerBasisCacheMatrixOffset = kOwnerBasisCacheOffset + 16;
constexpr uint32_t kOwnerSubCameraOffset = 1184;
constexpr float kBaseMoveSpeed = 24.0f;
constexpr float kFastMoveSpeed = 216.0f;
constexpr float kLookSpeed = 1.8f;
constexpr float kPitchLimit = 1.48f;
constexpr float kDefaultFollowerTargetDistance = 20.0f;

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct CameraMatrix {
  std::array<float, 4> row0{};
  std::array<float, 4> row1{};
  std::array<float, 4> row2{};
  std::array<float, 4> row3{};
};

struct FreeCameraState {
  bool enabled = false;
  bool initialized = false;
  bool hook_logged = false;
  bool toggle_down = false;
  bool reset_down = false;
  bool controls_logged = false;
  bool input_logged = false;
  uint32_t hook_count = 0;
  uint32_t verbose_hook_logs = 0;
  float yaw = 0.0f;
  float pitch = 0.0f;
  float follower_target_distance = kDefaultFollowerTargetDistance;
  Vec3 position{};
};

struct HostInputState {
  bool toggle = false;
  bool reset = false;
  bool fast = false;
  float move_x = 0.0f;
  float move_y = 0.0f;
  float move_z = 0.0f;
  float look_yaw = 0.0f;
  float look_pitch = 0.0f;
};

struct ProcessedInputDebugState {
  bool valid = false;
  uint32_t input_ptr = 0;
  uint32_t flags = 0;
  float axis16 = 0.0f;
  float axis20 = 0.0f;
  float axis24 = 0.0f;
  float axis28 = 0.0f;
};

struct CandidateHookDebugState {
  bool hook_logged = false;
  bool input_logged = false;
  bool write_failure_logged = false;
  uint32_t tick_count = 0;
  uint32_t verbose_logs = 0;
};

FreeCameraState g_free_camera;
uint32_t g_active_mode_a_owner = 0;
uint32_t g_mode_a_owner_update_depth = 0;
uint32_t g_active_mode_b_wrapper = 0;
uint32_t g_mode_b_wrapper_update_depth = 0;
uint32_t g_active_camera_owner = 0;
uint32_t g_owner_update_depth = 0;
uint32_t g_active_camera_follower_wrapper = 0;
uint32_t g_camera_follower_wrapper_depth = 0;
CandidateHookDebugState g_camera_follower_candidate;
ProcessedInputDebugState g_last_processed_input[2];
uint32_t g_raw_input_logs = 0;

bool TryReadGuestFloat(uint32_t guest_ptr, uint32_t offset, float& out_value) {
  if (guest_ptr == 0) {
    return false;
  }

  const auto* value_ptr =
      REX_KERNEL_MEMORY()->TranslateVirtual<const uint32_t*>(guest_ptr + offset);
  if (!value_ptr) {
    return false;
  }

  const uint32_t bits = std::byteswap(*value_ptr);
  const float value = std::bit_cast<float>(bits);
  if (!std::isfinite(value)) {
    return false;
  }

  out_value = value;
  return true;
}

bool TryReadGuestU32(uint32_t guest_ptr, uint32_t offset, uint32_t& out_value) {
  if (guest_ptr == 0) {
    return false;
  }

  const auto* value_ptr =
      REX_KERNEL_MEMORY()->TranslateVirtual<const uint32_t*>(guest_ptr + offset);
  if (!value_ptr) {
    return false;
  }

  out_value = std::byteswap(*value_ptr);
  return true;
}

bool TryWriteGuestFloat(uint32_t guest_ptr, uint32_t offset, float value) {
  if (guest_ptr == 0 || !std::isfinite(value)) {
    return false;
  }

  auto* value_ptr = REX_KERNEL_MEMORY()->TranslateVirtual<uint32_t*>(guest_ptr + offset);
  if (!value_ptr) {
    return false;
  }

  *value_ptr = std::byteswap(std::bit_cast<uint32_t>(value));
  return true;
}

bool TryReadGuestVec4(uint32_t guest_ptr, uint32_t offset, std::array<float, 4>& out_value) {
  for (size_t i = 0; i < out_value.size(); ++i) {
    if (!TryReadGuestFloat(guest_ptr, offset + static_cast<uint32_t>(i * sizeof(float)),
                           out_value[i])) {
      return false;
    }
  }
  return true;
}

bool TryReadGuestVec3(uint32_t guest_ptr, uint32_t offset, Vec3& out_value) {
  return TryReadGuestFloat(guest_ptr, offset + 0, out_value.x) &&
         TryReadGuestFloat(guest_ptr, offset + 4, out_value.y) &&
         TryReadGuestFloat(guest_ptr, offset + 8, out_value.z);
}

bool TryWriteGuestVec4(uint32_t guest_ptr, uint32_t offset, const std::array<float, 4>& value) {
  for (size_t i = 0; i < value.size(); ++i) {
    if (!TryWriteGuestFloat(guest_ptr, offset + static_cast<uint32_t>(i * sizeof(float)),
                            value[i])) {
      return false;
    }
  }
  return true;
}

bool TryReadCameraMatrix(uint32_t guest_ptr, CameraMatrix& out_matrix) {
  return TryReadGuestVec4(guest_ptr, 0, out_matrix.row0) &&
         TryReadGuestVec4(guest_ptr, 16, out_matrix.row1) &&
         TryReadGuestVec4(guest_ptr, 32, out_matrix.row2) &&
         TryReadGuestVec4(guest_ptr, 48, out_matrix.row3);
}

bool TryWriteCameraMatrix(uint32_t guest_ptr, const CameraMatrix& matrix) {
  return TryWriteGuestVec4(guest_ptr, 0, matrix.row0) &&
         TryWriteGuestVec4(guest_ptr, 16, matrix.row1) &&
         TryWriteGuestVec4(guest_ptr, 32, matrix.row2) &&
         TryWriteGuestVec4(guest_ptr, 48, matrix.row3);
}

float ClampAxis(float value) {
  return std::clamp(value, -1.0f, 1.0f);
}

bool IsKeyDown(int virtual_key) {
  return (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
}

float Dot(const Vec3& lhs, const Vec3& rhs) {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

float Length(const Vec3& value) {
  return std::sqrt(Dot(value, value));
}

Vec3 Normalize(const Vec3& value) {
  const float length = Length(value);
  if (length <= 0.00001f) {
    return {};
  }

  const float inv_length = 1.0f / length;
  return Vec3{
      .x = value.x * inv_length,
      .y = value.y * inv_length,
      .z = value.z * inv_length,
  };
}

Vec3 Add(const Vec3& lhs, const Vec3& rhs) {
  return Vec3{
      .x = lhs.x + rhs.x,
      .y = lhs.y + rhs.y,
      .z = lhs.z + rhs.z,
  };
}

Vec3 Subtract(const Vec3& lhs, const Vec3& rhs) {
  return Vec3{
      .x = lhs.x - rhs.x,
      .y = lhs.y - rhs.y,
      .z = lhs.z - rhs.z,
  };
}

Vec3 Scale(const Vec3& value, float scalar) {
  return Vec3{
      .x = value.x * scalar,
      .y = value.y * scalar,
      .z = value.z * scalar,
  };
}

CameraMatrix BuildSubCameraMatrix(const FreeCameraState& state) {
  const float sin_yaw = std::sin(state.yaw);
  const float cos_yaw = std::cos(state.yaw);
  const float sin_pitch = std::sin(state.pitch);
  const float cos_pitch = std::cos(state.pitch);

  return CameraMatrix{
      .row0 = {cos_yaw, 0.0f, -sin_yaw, 0.0f},
      .row1 = {-sin_yaw * sin_pitch, cos_pitch, -cos_yaw * sin_pitch, 0.0f},
      .row2 = {sin_yaw * cos_pitch, sin_pitch, cos_yaw * cos_pitch, 0.0f},
      .row3 = {state.position.x, state.position.y, state.position.z, 1.0f},
  };
}

bool InitializeFromPose(const Vec3& position, const Vec3& forward) {
  const Vec3 normalized_forward = Normalize(forward);
  if (Length(normalized_forward) <= 0.00001f) {
    return false;
  }

  g_free_camera.position = position;
  g_free_camera.pitch = std::asin(std::clamp(normalized_forward.y, -1.0f, 1.0f));
  g_free_camera.yaw = std::atan2(normalized_forward.x, normalized_forward.z);
  g_free_camera.initialized = true;
  return true;
}

bool InitializeFromCurrentCamera(uint32_t owner_ptr) {
  CameraMatrix matrix;
  if (!TryReadCameraMatrix(owner_ptr + kOwnerSubCameraOffset, matrix)) {
    return false;
  }

  const Vec3 forward = Normalize(Vec3{
      .x = matrix.row2[0],
      .y = matrix.row2[1],
      .z = matrix.row2[2],
  });
  if (Length(forward) <= 0.00001f) {
    return false;
  }

  return InitializeFromPose(
      Vec3{
          .x = matrix.row3[0],
          .y = matrix.row3[1],
          .z = matrix.row3[2],
      },
      forward);
}

bool InitializeFromCameraMatrix(uint32_t camera_ptr) {
  CameraMatrix matrix;
  if (!TryReadCameraMatrix(camera_ptr, matrix)) {
    return false;
  }

  const Vec3 forward = Normalize(Vec3{
      .x = matrix.row2[0],
      .y = matrix.row2[1],
      .z = matrix.row2[2],
  });
  if (Length(forward) <= 0.00001f) {
    return false;
  }

  return InitializeFromPose(
      Vec3{
          .x = matrix.row3[0],
          .y = matrix.row3[1],
          .z = matrix.row3[2],
      },
      forward);
}

bool InitializeFromCameraMatrixFlipped(uint32_t camera_ptr) {
  CameraMatrix matrix;
  if (!TryReadCameraMatrix(camera_ptr, matrix)) {
    return false;
  }

  const Vec3 forward = Normalize(Vec3{
      .x = -matrix.row2[0],
      .y = -matrix.row2[1],
      .z = -matrix.row2[2],
  });
  if (Length(forward) <= 0.00001f) {
    return false;
  }

  return InitializeFromPose(
      Vec3{
          .x = matrix.row3[0],
          .y = matrix.row3[1],
          .z = matrix.row3[2],
      },
      forward);
}

bool TryGetCameraFollowerMatrixPtrAtOffset(uint32_t object_ptr, uint32_t cache_offset,
                                           uint32_t& out_matrix_ptr) {
  uint32_t cache_object_ptr = 0;
  if (!TryReadGuestU32(object_ptr, cache_offset, cache_object_ptr) || cache_object_ptr == 0) {
    return false;
  }

  out_matrix_ptr = cache_object_ptr + 16;
  return true;
}

bool TryGetCameraFollowerInternalMatrixPtr(uint32_t object_ptr, uint32_t& out_matrix_ptr) {
  return TryGetCameraFollowerMatrixPtrAtOffset(object_ptr, 16, out_matrix_ptr);
}

bool TryGetCameraFollowerPublishedMatrixPtr(uint32_t object_ptr, uint32_t& out_matrix_ptr) {
  return TryGetCameraFollowerMatrixPtrAtOffset(object_ptr, 4, out_matrix_ptr);
}

bool InitializeFromCameraFollower(uint32_t object_ptr) {
  uint32_t matrix_ptr = 0;
  const bool have_matrix_ptr = TryGetCameraFollowerPublishedMatrixPtr(object_ptr, matrix_ptr) ||
                               TryGetCameraFollowerInternalMatrixPtr(object_ptr, matrix_ptr);

  Vec3 eye{};
  Vec3 target{};
  const bool have_eye = TryReadGuestVec3(object_ptr, 24, eye);
  const bool have_target = TryReadGuestVec3(object_ptr, 40, target);
  if (have_eye && have_target) {
    const Vec3 forward = Normalize(Subtract(eye, target));
    const float target_distance = Length(Subtract(target, eye));
    if (Length(forward) > 0.00001f) {
      InitializeFromPose(eye, forward);
      g_free_camera.follower_target_distance =
          std::max(target_distance, kDefaultFollowerTargetDistance);
      return true;
    }
  }

  if (have_matrix_ptr && InitializeFromCameraMatrixFlipped(matrix_ptr)) {
    g_free_camera.follower_target_distance = kDefaultFollowerTargetDistance;
    return true;
  }

  return false;
}

void UpdateFreeCameraState(float delta_seconds, const HostInputState& input) {
  const float move_speed = input.fast ? kFastMoveSpeed : kBaseMoveSpeed;
  g_free_camera.yaw += input.look_yaw * kLookSpeed * delta_seconds;
  g_free_camera.pitch = std::clamp(g_free_camera.pitch + input.look_pitch * kLookSpeed * delta_seconds,
                                   -kPitchLimit, kPitchLimit);

  const CameraMatrix matrix = BuildSubCameraMatrix(g_free_camera);
  Vec3 movement = Add(Scale(Vec3{matrix.row0[0], matrix.row0[1], matrix.row0[2]}, input.move_x),
                      Scale(Vec3{0.0f, 1.0f, 0.0f}, input.move_y));
  movement = Add(movement, Scale(Vec3{matrix.row2[0], matrix.row2[1], matrix.row2[2]}, input.move_z));
  movement = Normalize(movement);

  g_free_camera.position =
      Add(g_free_camera.position, Scale(movement, move_speed * delta_seconds));
}

bool WriteFreeCameraMatrices(uint32_t camera_ptr, uint32_t basis_cache_matrix_ptr) {
  const CameraMatrix sub_camera_matrix = BuildSubCameraMatrix(g_free_camera);
  if (!TryWriteCameraMatrix(camera_ptr, sub_camera_matrix)) {
    return false;
  }

  if (basis_cache_matrix_ptr != 0) {
    if (!TryWriteCameraMatrix(basis_cache_matrix_ptr, sub_camera_matrix)) {
      return false;
    }
  }

  return true;
}

bool WriteCameraFollowerState(uint32_t object_ptr, CandidateHookDebugState& state) {
  const CameraMatrix matrix = BuildSubCameraMatrix(g_free_camera);
  uint32_t internal_matrix_ptr = 0;
  uint32_t published_matrix_ptr = 0;
  const Vec3 forward = Vec3{matrix.row2[0], matrix.row2[1], matrix.row2[2]};
  const Vec3 target =
      Add(g_free_camera.position, Scale(forward, g_free_camera.follower_target_distance));
  const std::array<float, 4> eye_vec4 = {
      g_free_camera.position.x,
      g_free_camera.position.y,
      g_free_camera.position.z,
      1.0f,
  };
  const std::array<float, 4> target_vec4 = {
      target.x,
      target.y,
      target.z,
      1.0f,
  };

  const bool have_internal_matrix_ptr =
      TryGetCameraFollowerInternalMatrixPtr(object_ptr, internal_matrix_ptr);
  const bool have_published_matrix_ptr =
      TryGetCameraFollowerPublishedMatrixPtr(object_ptr, published_matrix_ptr);
  const bool internal_matrix_ok =
      have_internal_matrix_ptr && TryWriteCameraMatrix(internal_matrix_ptr, matrix);
  const bool published_matrix_ok =
      have_published_matrix_ptr && TryWriteCameraMatrix(published_matrix_ptr, matrix);
  const bool matrix_write_ok = internal_matrix_ok || published_matrix_ok;

  const bool near_eye_ok = TryWriteGuestVec4(object_ptr, 24, eye_vec4);
  const bool near_target_ok = TryWriteGuestVec4(object_ptr, 40, target_vec4);
  const bool far_eye_ok = TryWriteGuestVec4(object_ptr, 56, eye_vec4);
  const bool far_target_ok = TryWriteGuestVec4(object_ptr, 72, target_vec4);

  const bool vector_write_ok = near_eye_ok && near_target_ok && far_eye_ok && far_target_ok;
  if (!vector_write_ok ||
      (have_internal_matrix_ptr && !internal_matrix_ok) ||
      (have_published_matrix_ptr && !published_matrix_ok)) {
    if (!state.write_failure_logged) {
      state.write_failure_logged = true;
      REXLOG_INFO(
          "[freecam] follower write detail object=0x{:08X} have_internal_matrix_ptr={} "
          "internal_matrix_ptr=0x{:08X} internal_matrix_ok={} "
          "have_published_matrix_ptr={} published_matrix_ptr=0x{:08X} "
          "published_matrix_ok={} near_eye_ok={} near_target_ok={} "
          "far_eye_ok={} far_target_ok={}",
          object_ptr, have_internal_matrix_ptr, internal_matrix_ptr, internal_matrix_ok,
          have_published_matrix_ptr, published_matrix_ptr, published_matrix_ok, near_eye_ok,
          near_target_ok, far_eye_ok, far_target_ok);
    }
  }

  return vector_write_ok || matrix_write_ok;
}

void LogControlsOnce() {
  if (g_free_camera.controls_logged) {
    return;
  }

  g_free_camera.controls_logged = true;
  REXLOG_INFO(
      "[freecam] controls: Keyboard Toggle=F6, re-anchor=F7, move=WASD, up/down=E/Q, "
      "look=arrow keys, fast=Left Shift. Game raw-input logging remains enabled for debugging.");
}

bool HasMeaningfulInput(const HostInputState& input) {
  return input.toggle || input.reset || input.fast || std::fabs(input.move_x) > 0.05f ||
         std::fabs(input.move_y) > 0.05f || std::fabs(input.move_z) > 0.05f ||
         std::fabs(input.look_yaw) > 0.05f || std::fabs(input.look_pitch) > 0.05f;
}

bool ShouldLogProcessedInputSample(int index, const ProcessedInputDebugState& sample) {
  if (index < 0 || index >= 2) {
    return false;
  }

  const ProcessedInputDebugState& previous = g_last_processed_input[index];
  const bool has_signal = sample.flags != 0 || std::fabs(sample.axis16) > 0.02f ||
                          std::fabs(sample.axis20) > 0.02f || std::fabs(sample.axis24) > 0.02f ||
                          std::fabs(sample.axis28) > 0.02f;
  const bool changed = !previous.valid || previous.input_ptr != sample.input_ptr ||
                       previous.flags != sample.flags ||
                       std::fabs(previous.axis16 - sample.axis16) > 0.02f ||
                       std::fabs(previous.axis20 - sample.axis20) > 0.02f ||
                       std::fabs(previous.axis24 - sample.axis24) > 0.02f ||
                       std::fabs(previous.axis28 - sample.axis28) > 0.02f;

  return g_raw_input_logs < 48 && (has_signal || !previous.valid) && changed;
}

void LogProcessedInputSample(int index, const ProcessedInputDebugState& sample) {
  if (!ShouldLogProcessedInputSample(index, sample)) {
    g_last_processed_input[index] = sample;
    return;
  }

  ++g_raw_input_logs;
  g_last_processed_input[index] = sample;
  REXLOG_INFO(
      "[freecam] raw_input idx={} ptr=0x{:08X} flags=0x{:08X} axis16={:.3f} axis20={:.3f} "
      "axis24={:.3f} axis28={:.3f}",
      index, sample.input_ptr, sample.flags, sample.axis16, sample.axis20, sample.axis24,
      sample.axis28);
}

HostInputState GatherKeyboardState() {
  HostInputState input;
  input.toggle = IsKeyDown(VK_F6);
  input.reset = IsKeyDown(VK_F7);
  input.fast = IsKeyDown(VK_LSHIFT) || IsKeyDown(VK_RSHIFT);
  input.move_x = (IsKeyDown('D') ? 1.0f : 0.0f) - (IsKeyDown('A') ? 1.0f : 0.0f);
  input.move_y = (IsKeyDown('E') ? 1.0f : 0.0f) - (IsKeyDown('Q') ? 1.0f : 0.0f);
  input.move_z = (IsKeyDown('W') ? 1.0f : 0.0f) - (IsKeyDown('S') ? 1.0f : 0.0f);
  input.look_yaw = (IsKeyDown(VK_RIGHT) ? 1.0f : 0.0f) - (IsKeyDown(VK_LEFT) ? 1.0f : 0.0f);
  input.look_pitch = (IsKeyDown(VK_UP) ? 1.0f : 0.0f) - (IsKeyDown(VK_DOWN) ? 1.0f : 0.0f);
  return input;
}

HostInputState InvertFollowerFreeCameraInput(const HostInputState& input) {
  HostInputState adjusted = input;
  adjusted.move_x = -adjusted.move_x;
  adjusted.move_z = -adjusted.move_z;
  adjusted.look_pitch = -adjusted.look_pitch;
  return adjusted;
}

template <typename PPCContextT, typename BaseT>
bool TryGetProcessedInputObject(PPCContextT& source_ctx, BaseT base, int index,
                                uint32_t& out_input_ptr) {
  auto input_ctx = source_ctx;
  input_ctx.r3.s32 = index;
  sub_823F5CC8(input_ctx, base);
  out_input_ptr = input_ctx.r3.u32;
  return out_input_ptr != 0;
}

template <typename PPCContextT, typename BaseT>
HostInputState GatherGameInput(PPCContextT& source_ctx, BaseT base) {
  HostInputState input;

  for (int index = 0; index < 2; ++index) {
    uint32_t input_ptr = 0;
    if (!TryGetProcessedInputObject(source_ctx, base, index, input_ptr)) {
      continue;
    }

    ProcessedInputDebugState sample;
    sample.valid = true;
    sample.input_ptr = input_ptr;

    if (!TryReadGuestU32(input_ptr, 4, sample.flags)) {
      continue;
    }

    TryReadGuestFloat(input_ptr, 16, sample.axis16);
    TryReadGuestFloat(input_ptr, 20, sample.axis20);
    TryReadGuestFloat(input_ptr, 24, sample.axis24);
    TryReadGuestFloat(input_ptr, 28, sample.axis28);
    LogProcessedInputSample(index, sample);
  }

  return input;
}

template <typename PPCContextT, typename BaseT>
HostInputState GatherCombinedInput(PPCContextT& source_ctx, BaseT base) {
  static_cast<void>(GatherGameInput(source_ctx, base));
  return GatherKeyboardState();
}

void TickFreeCamera(uint32_t camera_ptr, uint32_t owner_ptr, uint32_t basis_cache_matrix_ptr,
                    uint32_t caller, const char* label, float delta_seconds,
                    const HostInputState& input) {
  if (camera_ptr == 0) {
    return;
  }

  ++g_free_camera.hook_count;

  if (!g_free_camera.hook_logged) {
    g_free_camera.hook_logged = true;
    LogControlsOnce();
    REXLOG_INFO("[freecam] {} hook active caller=0x{:08X} camera=0x{:08X}", label, caller,
                camera_ptr);
  }

  if (g_free_camera.verbose_hook_logs < 16) {
    ++g_free_camera.verbose_hook_logs;
    REXLOG_INFO("[freecam] tick label={} caller=0x{:08X} camera=0x{:08X} owner=0x{:08X} dt={:.6f}",
                label, caller, camera_ptr, owner_ptr, delta_seconds);
  }

  if ((g_free_camera.hook_count % 600u) == 0u) {
    REXLOG_INFO(
        "[freecam] heartbeat label={} camera=0x{:08X} owner=0x{:08X} enabled={} initialized={} "
        "hooks={}",
        label, camera_ptr, owner_ptr, g_free_camera.enabled, g_free_camera.initialized,
        g_free_camera.hook_count);
  }

  if (!g_free_camera.input_logged && HasMeaningfulInput(input)) {
    g_free_camera.input_logged = true;
    REXLOG_INFO(
        "[freecam] input seen label={} toggle={} reset={} fast={} move=[{:.2f},{:.2f},{:.2f}] "
        "look=[{:.2f},{:.2f}]",
        label, input.toggle, input.reset, input.fast, input.move_x, input.move_y, input.move_z,
        input.look_yaw, input.look_pitch);
  }

  const bool toggle_pressed = input.toggle && !g_free_camera.toggle_down;
  const bool reset_pressed = input.reset && !g_free_camera.reset_down;
  g_free_camera.toggle_down = input.toggle;
  g_free_camera.reset_down = input.reset;

  if (toggle_pressed) {
    REXLOG_INFO("[freecam] toggle pressed label={} camera=0x{:08X} owner=0x{:08X}", label,
                camera_ptr, owner_ptr);
    if (!g_free_camera.enabled) {
      if (!InitializeFromCameraMatrix(camera_ptr)) {
        REXLOG_INFO(
            "[freecam] enable failed label={} current camera matrix unreadable camera=0x{:08X} "
            "owner=0x{:08X}",
            label, camera_ptr, owner_ptr);
      } else {
        g_free_camera.enabled = true;
        LogControlsOnce();
        REXLOG_INFO("[freecam] enabled label={} pos=[{:.3f},{:.3f},{:.3f}] yaw={:.3f} pitch={:.3f}",
                    label, g_free_camera.position.x, g_free_camera.position.y,
                    g_free_camera.position.z, g_free_camera.yaw, g_free_camera.pitch);
      }
    } else {
      g_free_camera.enabled = false;
      REXLOG_INFO("[freecam] disabled label={}", label);
    }
  }

  if (!g_free_camera.enabled) {
    return;
  }

  if (reset_pressed || !g_free_camera.initialized) {
    if (InitializeFromCameraMatrix(camera_ptr)) {
      REXLOG_INFO(
          "[freecam] re-anchored label={} pos=[{:.3f},{:.3f},{:.3f}] yaw={:.3f} pitch={:.3f}",
          label, g_free_camera.position.x, g_free_camera.position.y, g_free_camera.position.z,
          g_free_camera.yaw, g_free_camera.pitch);
    }
  }

  if (!g_free_camera.initialized) {
    return;
  }

  UpdateFreeCameraState(delta_seconds, input);
  if (!WriteFreeCameraMatrices(camera_ptr, basis_cache_matrix_ptr)) {
    REXLOG_INFO("[freecam] write failed label={} camera=0x{:08X} owner=0x{:08X}", label,
                camera_ptr, owner_ptr);
  }
}

void TraceCandidateCameraHook(CandidateHookDebugState& state, uint32_t object_ptr, uint32_t caller,
                              const char* label, float delta_seconds,
                              const HostInputState& input) {
  if (object_ptr == 0) {
    return;
  }

  ++state.tick_count;

  if (!state.hook_logged) {
    state.hook_logged = true;
    LogControlsOnce();
    REXLOG_INFO("[freecam] {} hook active caller=0x{:08X} object=0x{:08X}", label, caller,
                object_ptr);
  }

  if (state.verbose_logs < 16) {
    ++state.verbose_logs;
    Vec3 near_pos{};
    Vec3 far_pos{};
    const bool have_near = TryReadGuestVec3(object_ptr, 24, near_pos);
    const bool have_far = TryReadGuestVec3(object_ptr, 40, far_pos);
    if (have_near && have_far) {
      REXLOG_INFO(
          "[freecam] candidate tick label={} caller=0x{:08X} object=0x{:08X} dt={:.6f} "
          "near=[{:.3f},{:.3f},{:.3f}] far=[{:.3f},{:.3f},{:.3f}]",
          label, caller, object_ptr, delta_seconds, near_pos.x, near_pos.y, near_pos.z, far_pos.x,
          far_pos.y, far_pos.z);
    } else {
      REXLOG_INFO(
          "[freecam] candidate tick label={} caller=0x{:08X} object=0x{:08X} dt={:.6f} "
          "vec_read_failed={}",
          label, caller, object_ptr, delta_seconds, !(have_near && have_far));
    }
  }

  if ((state.tick_count % 600u) == 0u) {
    REXLOG_INFO("[freecam] candidate heartbeat label={} object=0x{:08X} hooks={}", label,
                object_ptr, state.tick_count);
  }

  if (!state.input_logged && HasMeaningfulInput(input)) {
    state.input_logged = true;
    REXLOG_INFO(
        "[freecam] candidate input label={} toggle={} reset={} fast={} move=[{:.2f},{:.2f},{:.2f}] "
        "look=[{:.2f},{:.2f}]",
        label, input.toggle, input.reset, input.fast, input.move_x, input.move_y, input.move_z,
        input.look_yaw, input.look_pitch);
  }
}

void TickCameraFollowerFreeCamera(CandidateHookDebugState& state, uint32_t object_ptr,
                                  uint32_t caller, const char* label, float delta_seconds,
                                  const HostInputState& input) {
  const HostInputState adjusted_input = InvertFollowerFreeCameraInput(input);
  TraceCandidateCameraHook(state, object_ptr, caller, label, delta_seconds, adjusted_input);

  const bool toggle_pressed = adjusted_input.toggle && !g_free_camera.toggle_down;
  const bool reset_pressed = adjusted_input.reset && !g_free_camera.reset_down;
  g_free_camera.toggle_down = adjusted_input.toggle;
  g_free_camera.reset_down = adjusted_input.reset;

  if (toggle_pressed) {
    REXLOG_INFO("[freecam] toggle pressed label={} object=0x{:08X}", label, object_ptr);
    if (!g_free_camera.enabled) {
      if (!InitializeFromCameraFollower(object_ptr)) {
        REXLOG_INFO("[freecam] enable failed label={} object=0x{:08X}", label, object_ptr);
      } else {
        g_free_camera.enabled = true;
        REXLOG_INFO(
            "[freecam] enabled label={} pos=[{:.3f},{:.3f},{:.3f}] yaw={:.3f} pitch={:.3f} "
            "target_distance={:.3f}",
            label, g_free_camera.position.x, g_free_camera.position.y, g_free_camera.position.z,
            g_free_camera.yaw, g_free_camera.pitch, g_free_camera.follower_target_distance);
      }
    } else {
      g_free_camera.enabled = false;
      REXLOG_INFO("[freecam] disabled label={}", label);
    }
  }

  if (!g_free_camera.enabled) {
    return;
  }

  if (reset_pressed || !g_free_camera.initialized) {
    if (InitializeFromCameraFollower(object_ptr)) {
      REXLOG_INFO(
          "[freecam] re-anchored label={} pos=[{:.3f},{:.3f},{:.3f}] yaw={:.3f} pitch={:.3f} "
          "target_distance={:.3f}",
          label, g_free_camera.position.x, g_free_camera.position.y, g_free_camera.position.z,
          g_free_camera.yaw, g_free_camera.pitch, g_free_camera.follower_target_distance);
    }
  }

  if (!g_free_camera.initialized) {
    return;
  }

  UpdateFreeCameraState(delta_seconds, adjusted_input);
  if (!WriteCameraFollowerState(object_ptr, state)) {
    REXLOG_INFO("[freecam] write failed label={} object=0x{:08X}", label, object_ptr);
  }
}

}  // namespace

PPC_FUNC_IMPL(UpdateCameraModeA) {
  const uint32_t camera_ptr = ctx.r3.u32;
  const float delta_seconds = std::clamp(static_cast<float>(ctx.f1.f64), 0.0f, 0.1f);
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);

  __imp__UpdateCameraModeA(ctx, base);
  const bool owner_hook_will_handle =
      g_mode_a_owner_update_depth != 0 && g_active_mode_a_owner != 0 &&
      g_active_mode_a_owner == camera_ptr;
  if (owner_hook_will_handle) {
    return;
  }

  const HostInputState input = GatherCombinedInput(ctx, base);
  TickFreeCamera(camera_ptr, 0, 0, caller, "mode_a", delta_seconds, input);
}

PPC_FUNC_IMPL(UpdateCameraModeB) {
  const uint32_t camera_ptr = ctx.r3.u32;
  const float delta_seconds = std::clamp(static_cast<float>(ctx.f1.f64), 0.0f, 0.1f);
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);

  __imp__UpdateCameraModeB(ctx, base);
  const bool owner_hook_will_handle =
      g_owner_update_depth != 0 && g_active_camera_owner != 0 &&
      g_active_camera_owner + kOwnerSubCameraOffset == camera_ptr;
  const bool wrapper_hook_will_handle =
      g_mode_b_wrapper_update_depth != 0 && g_active_mode_b_wrapper != 0 &&
      g_active_mode_b_wrapper + 128 == camera_ptr;
  if (owner_hook_will_handle || wrapper_hook_will_handle) {
    return;
  }

  const HostInputState input = GatherCombinedInput(ctx, base);
  TickFreeCamera(camera_ptr, 0, 0, caller, "mode_b", delta_seconds, input);
}

PPC_FUNC_IMPL(UpdateCameraModeBOwner) {
  const uint32_t owner_ptr = ctx.r3.u32;
  const float delta_seconds = std::clamp(static_cast<float>(ctx.f1.f64), 0.0f, 0.1f);
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  const uint32_t previous_active_owner = g_active_camera_owner;

  if (owner_ptr != 0) {
    g_active_camera_owner = owner_ptr;
    ++g_owner_update_depth;
  }

  __imp__UpdateCameraModeBOwner(ctx, base);

  if (owner_ptr != 0) {
    const HostInputState input = GatherCombinedInput(ctx, base);
    TickFreeCamera(owner_ptr + kOwnerSubCameraOffset, owner_ptr,
                   owner_ptr + kOwnerBasisCacheMatrixOffset, caller, "mode_b_owner",
                   delta_seconds, input);
    --g_owner_update_depth;
    g_active_camera_owner = previous_active_owner;
  }
}

PPC_FUNC_IMPL(UpdateCameraModeAOwner) {
  const uint32_t owner_ptr = ctx.r3.u32;
  const float delta_seconds = std::clamp(static_cast<float>(ctx.f1.f64), 0.0f, 0.1f);
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  const uint32_t previous_active_owner = g_active_mode_a_owner;

  if (owner_ptr != 0) {
    g_active_mode_a_owner = owner_ptr;
    ++g_mode_a_owner_update_depth;
  }

  __imp__UpdateCameraModeAOwner(ctx, base);

  if (owner_ptr != 0) {
    const HostInputState input = GatherCombinedInput(ctx, base);
    TickFreeCamera(owner_ptr, 0, 0, caller, "mode_a_owner", delta_seconds, input);
    --g_mode_a_owner_update_depth;
    g_active_mode_a_owner = previous_active_owner;
  }
}

PPC_FUNC_IMPL(UpdateCameraModeBWrapper) {
  const uint32_t wrapper_ptr = ctx.r3.u32;
  const float delta_seconds = std::clamp(static_cast<float>(ctx.f1.f64), 0.0f, 0.1f);
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  const uint32_t previous_active_wrapper = g_active_mode_b_wrapper;

  if (wrapper_ptr != 0) {
    g_active_mode_b_wrapper = wrapper_ptr;
    ++g_mode_b_wrapper_update_depth;
  }

  __imp__UpdateCameraModeBWrapper(ctx, base);

  if (wrapper_ptr != 0) {
    const HostInputState input = GatherCombinedInput(ctx, base);
    TickFreeCamera(wrapper_ptr + 128, wrapper_ptr, wrapper_ptr + 32, caller, "mode_b_wrapper",
                   delta_seconds, input);
    --g_mode_b_wrapper_update_depth;
    g_active_mode_b_wrapper = previous_active_wrapper;
  }
}

PPC_FUNC_IMPL(UpdateCameraFollower) {
  const uint32_t object_ptr = ctx.r3.u32;
  const float delta_seconds = std::clamp(static_cast<float>(ctx.f1.f64), 0.0f, 0.1f);
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);

  __imp__UpdateCameraFollower(ctx, base);
  const bool wrapper_hook_will_handle =
      g_camera_follower_wrapper_depth != 0 && g_active_camera_follower_wrapper != 0 &&
      g_active_camera_follower_wrapper == object_ptr;
  if (wrapper_hook_will_handle) {
    return;
  }

  const HostInputState input = GatherCombinedInput(ctx, base);
  TickCameraFollowerFreeCamera(g_camera_follower_candidate, object_ptr, caller, "camera_follower",
                               delta_seconds, input);
}

PPC_FUNC_IMPL(sub_824167E8) {
  const uint32_t object_ptr = ctx.r3.u32;
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  const uint32_t previous_active_wrapper = g_active_camera_follower_wrapper;

  if (object_ptr != 0) {
    g_active_camera_follower_wrapper = object_ptr;
    ++g_camera_follower_wrapper_depth;
  }

  __imp__sub_824167E8(ctx, base);

  if (object_ptr != 0) {
    float delta_seconds = 0.0f;
    TryReadGuestFloat(object_ptr, 628, delta_seconds);
    delta_seconds = std::clamp(delta_seconds, 0.0f, 0.1f);
    const HostInputState input = GatherCombinedInput(ctx, base);
    TickCameraFollowerFreeCamera(g_camera_follower_candidate, object_ptr, caller,
                                 "camera_follower", delta_seconds, input);
    --g_camera_follower_wrapper_depth;
    g_active_camera_follower_wrapper = previous_active_wrapper;
  }
}
