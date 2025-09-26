/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "rffi/src/sdp_observer.h"

#include "rffi/api/sdp_observer_intf.h"
#include "rffi/src/ptr.h"

namespace webrtc {
namespace rffi {

CreateSessionDescriptionObserverRffi::CreateSessionDescriptionObserverRffi(
    void* csd_observer,
    const CreateSessionDescriptionObserverCallbacks* csd_observer_cbs)
    : csd_observer_(csd_observer), csd_observer_cbs_(*csd_observer_cbs) {
  RTC_LOG(LS_INFO) << "CreateSessionDescriptionObserverRffi:ctor(): "
                   << this->csd_observer_;
}

CreateSessionDescriptionObserverRffi::~CreateSessionDescriptionObserverRffi() {
  RTC_LOG(LS_INFO) << "CreateSessionDescriptionObserverRffi:dtor(): "
                   << this->csd_observer_;
}

void CreateSessionDescriptionObserverRffi::OnSuccess(
    SessionDescriptionInterface* session_description) {
  // OnSuccess transfers ownership of the description
  RTC_LOG(LS_INFO) << "CreateSessionDescriptionObserverRffi:OnSuccess(): ";
  this->csd_observer_cbs_.onSuccess(this->csd_observer_, session_description);
}

void CreateSessionDescriptionObserverRffi::OnFailure(RTCError error) {
  RTC_LOG(LS_INFO) << "CreateSessionDescriptionObserverRffi:OnFailure(): ";
  this->csd_observer_cbs_.onFailure(this->csd_observer_, error.message(),
                                    static_cast<int32_t>(error.type()));
}

RUSTEXPORT CreateSessionDescriptionObserverRffi*
Rust_createCreateSessionDescriptionObserver(
    void* csd_observer_borrowed,
    const CreateSessionDescriptionObserverCallbacks*
        csd_observer_cbs_borrowed) {
  return take_rc(make_ref_counted<CreateSessionDescriptionObserverRffi>(
      csd_observer_borrowed, csd_observer_cbs_borrowed));
}

SetSessionDescriptionObserverRffi::SetSessionDescriptionObserverRffi(
    void* ssd_observer_borrowed,
    const SetSessionDescriptionObserverCallbacks* ssd_observer_cbs_borrowed)
    : ssd_observer_(ssd_observer_borrowed),
      ssd_observer_cbs_(*ssd_observer_cbs_borrowed) {
  RTC_LOG(LS_INFO) << "SetSessionDescriptionObserverRffi:ctor(): "
                   << this->ssd_observer_;
}

SetSessionDescriptionObserverRffi::~SetSessionDescriptionObserverRffi() {
  RTC_LOG(LS_INFO) << "SetSessionDescriptionObserverRffi:dtor(): "
                   << this->ssd_observer_;
}

void SetSessionDescriptionObserverRffi::OnSuccess() {
  RTC_LOG(LS_INFO) << "SetSessionDescriptionObserverRffi:OnSuccess(): ";
  this->ssd_observer_cbs_.onSuccess(this->ssd_observer_);
}

void SetSessionDescriptionObserverRffi::OnFailure(RTCError error) {
  RTC_LOG(LS_INFO) << "SetSessionDescriptionObserverRffi:OnFailure(): ";
  this->ssd_observer_cbs_.onFailure(this->ssd_observer_, error.message(),
                                    static_cast<int32_t>(error.type()));
}

// Returns an owned RC.
// The passed-in values must outlive the returned value.
RUSTEXPORT SetSessionDescriptionObserverRffi*
Rust_createSetSessionDescriptionObserver(
    void* ssd_observer_borrowed,
    const SetSessionDescriptionObserverCallbacks* ssd_observer_cbs_borrowed) {
  return take_rc(make_ref_counted<SetSessionDescriptionObserverRffi>(
      ssd_observer_borrowed, ssd_observer_cbs_borrowed));
}

}  // namespace rffi
}  // namespace webrtc
