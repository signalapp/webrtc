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
  explicit JsepSessionDescription(SdpType type);
  [[deprecated(
      "Use the CreateSessionDescription() method(s) to create an instance.")]]
  explicit JsepSessionDescription(const std::string& type);
  JsepSessionDescription(SdpType type,
                         std::unique_ptr<SessionDescription> description,
                         absl::string_view session_id,
                         absl::string_view session_version);
  ~JsepSessionDescription() override;

  JsepSessionDescription(const JsepSessionDescription&) = delete;
  JsepSessionDescription& operator=(const JsepSessionDescription&) = delete;

  // Takes ownership of `description`.
  // TODO(bugs.webrtc.org/442220720): Deprecate.
  // [[deprecated(
  //    "Use CreateSessionDescription() to construct
  //    SessionDescriptionInterface objects.")]]
  bool Initialize(std::unique_ptr<SessionDescription> description,
                  const std::string& session_id,
                  const std::string& session_version);

  std::unique_ptr<SessionDescriptionInterface> Clone() const override;

  SessionDescription* description() override { return description_.get(); }
  const SessionDescription* description() const override {
    return description_.get();
  }
  std::string session_id() const override { return session_id_; }
  std::string session_version() const override { return session_version_; }
  SdpType GetType() const override { return type_; }
  std::string type() const override { return SdpTypeToString(type_); }
  // Allows changing the type. Used for testing.
  bool AddCandidate(const IceCandidate* candidate) override;
  bool RemoveCandidate(const IceCandidate* candidate) override;

  size_t number_of_mediasections() const override;
  const IceCandidateCollection* candidates(
      size_t mediasection_index) const override;
  bool ToString(std::string* out) const override;

 private:
  std::unique_ptr<SessionDescription> description_;
  std::string session_id_;
  std::string session_version_;
  const SdpType type_;
  std::vector<JsepCandidateCollection> candidate_collection_;

  bool IsValidMLineIndex(int index) const;
  bool GetMediasectionIndex(const IceCandidate* candidate, size_t* index) const;
  int GetMediasectionIndex(absl::string_view mid) const;
};

}  // namespace webrtc

#endif  // API_JSEP_SESSION_DESCRIPTION_H_
