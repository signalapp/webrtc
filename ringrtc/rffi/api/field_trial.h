/*
 * Copyright 2022 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_API_FIELD_TRIAL_H__
#define RFFI_API_FIELD_TRIAL_H__

#include "rffi/api/rffi_defs.h"

RUSTEXPORT void
Rust_setFieldTrials(const char* field_trials_string);

#endif /* RFFI_API_FIELD_TRIAL_H__ */
