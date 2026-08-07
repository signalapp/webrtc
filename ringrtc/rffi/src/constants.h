/*
 * Copyright 2026 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_CONSTANTS_H_
#define RFFI_CONSTANTS_H_

#include <cstdint>

namespace webrtc {
namespace rffi {

constexpr int TRANSPORT_CC1_EXT_ID = 1;
constexpr int VIDEO_ORIENTATION_EXT_ID = 4;
constexpr int AUDIO_LEVEL_EXT_ID = 5;
constexpr int DEPENDENCY_DESCRIPTOR_EXT_ID = 6;
constexpr int ABS_SEND_TIME_EXT_ID = 12;
constexpr int VIDEO_LAYERS_ALLOCATION_EXT_ID = 14;

// Payload types must be over 96 and less than 128.
// 101 used by connection.rs
constexpr int OPUS_PT = 102;
constexpr int VP8_PT = 108;
constexpr int VP8_RTX_PT = 118;
constexpr int VP9_PT = 109;
constexpr int VP9_RTX_PT = 119;
constexpr int RED_PT = 120;
constexpr int RED_RTX_PT = 121;
constexpr int ULPFEC_PT = 122;

constexpr uint32_t DISABLED_DEMUX_ID = 0;

}  // namespace rffi
}  // namespace webrtc

#endif  // RFFI_CONSTANTS_H_
