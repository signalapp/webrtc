/*
 *  Copyright 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// TODO(deadbeef): Move this out of api/; it's an implementation detail and
// shouldn't be used externally.

#ifndef API_JSEP_SESSION_DESCRIPTION_H_
#define API_JSEP_SESSION_DESCRIPTION_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/jsep.h"

namespace webrtc {

class SessionDescription;

// Implementation of SessionDescriptionInterface.
class JsepSessionDescription final : public SessionDescriptionInterface {
 public:
  // TODO: bugs.webrtc.org/442220720 - Remove this constructor and make sure
  // that JsepSessionDescription can only be constructed with a valid
  // SessionDescription object (with the exception of kRollback).
  [[deprecated(
      "JsepSessionDescription needs to be initialized with a valid description "
      "object")]]
  explicit JsepSessionDescription(SdpType type);
  JsepSessionDescription(SdpType type,
                         std::unique_ptr<SessionDescription> description,
                         absl::string_view session_id,
                         absl::string_view session_version,
                         std::vector<IceCandidateCollection> candidates = {});
  ~JsepSessionDescription() override;

  JsepSessionDescription(const JsepSessionDescription&) = delete;
  JsepSessionDescription& operator=(const JsepSessionDescription&) = delete;

  std::unique_ptr<SessionDescriptionInterface> Clone() const override;
  bool AddCandidate(const IceCandidate* candidate) override;
  bool RemoveCandidate(const IceCandidate* candidate) override;
  const IceCandidateCollection* candidates(
      size_t mediasection_index) const override;
  bool ToString(std::string* out) const override;

 private:
  std::vector<IceCandidateCollection> candidate_collection_
      RTC_GUARDED_BY(sequence_checker());

  bool IsValidMLineIndex(int index) const;
  bool GetMediasectionIndex(const IceCandidate* candidate, size_t* index) const;
  int GetMediasectionIndex(absl::string_view mid) const;
};

}  // namespace webrtc

#endif  // API_JSEP_SESSION_DESCRIPTION_H_
