/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "rffi/api/field_trial.h"
#include "system_wrappers/include/field_trial.h"

namespace webrtc {
namespace rffi {

// Initialize field trials from a string.
// This method can be called at most once before any other call into WebRTC.
// E.g. before the peer connection factory is constructed.
// Note: field_trials_string must never be destroyed.
RUSTEXPORT void
Rust_setFieldTrials(const char* field_trials_string) {
  webrtc::field_trial::InitFieldTrialsFromString(field_trials_string);
}

} // namespace rffi
} // namespace webrtc
