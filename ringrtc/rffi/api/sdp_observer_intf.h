/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_API_SDP_OBSERVER_INTF_H__
#define RFFI_API_SDP_OBSERVER_INTF_H__

#include <cstdint>

#include "rffi/api/rffi_defs.h"

/**
 * Rust friendly wrapper for creating objects that implement the
 * webrtc::CreateSessionDescriptionObserver and
 * webrtc::SetSessionDescriptionObserver interfaces.
 *
 */

namespace webrtc {
class SessionDescriptionInterface;
namespace rffi {
class CreateSessionDescriptionObserverRffi;
class SetSessionDescriptionObserverRffi;
/* Create Session Description Observer callback function pointers */
typedef struct {
  void (*onSuccess)(ptr::Borrowed<void> csd_observer_borrowed,
                    // Note: This differs from the name -- rust defines it as
                    // Owned, not OwnedRc
                    ptr::Owned<webrtc::SessionDescriptionInterface>
                        session_description_owned_rc);
  void (*onFailure)(ptr::Borrowed<void> csd_observer_borrowed,
                    ptr::Borrowed<const char> err_message_borrowed,
                    int32_t err_type);
} CreateSessionDescriptionObserverCallbacks;

RUSTEXPORT ptr::OwnedRc<webrtc::rffi::CreateSessionDescriptionObserverRffi>
Rust_createCreateSessionDescriptionObserver(
    ptr::Borrowed<void> csd_observer_borrowed,
    ptr::Borrowed<const CreateSessionDescriptionObserverCallbacks>
        csd_observer_cbs_borrowed);

/* Set Session Description Observer callback function pointers */
typedef struct {
  void (*onSuccess)(ptr::Borrowed<void> ssd_observer_borrowed);
  void (*onFailure)(ptr::Borrowed<void> ssd_observer_borrowed,
                    ptr::Borrowed<const char> err_message_borrowed,
                    int32_t err_type);
} SetSessionDescriptionObserverCallbacks;

RUSTEXPORT ptr::OwnedRc<webrtc::rffi::SetSessionDescriptionObserverRffi>
Rust_createSetSessionDescriptionObserver(
    ptr::Borrowed<void> ssd_observer_borrowed,
    ptr::Borrowed<const SetSessionDescriptionObserverCallbacks>
        ssd_observer_cbs_borrowed);
}  // namespace rffi
}  // namespace webrtc

#endif /* RFFI_API_SDP_OBSERVER_INTF_H__ */
