/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_LOGGING_H__
#define RFFI_LOGGING_H__

#include "rffi/api/rffi_defs.h"
#include "rffi/api/webrtc_common.h"

typedef struct {
  void (*onLogMessage)(webrtc::LoggingSeverity severity,
                       const char* message_borrowed);
} LoggerCallbacks;

// Should only be called once.
RUSTEXPORT void Rust_setLogger(LoggerCallbacks* cbs_borrowed,
                               webrtc::LoggingSeverity min_sev);

#endif /* RFFI_LOGGING_H__ */