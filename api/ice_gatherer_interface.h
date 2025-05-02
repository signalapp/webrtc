/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef API_ICE_GATHERER_INTERFACE_H_
#define API_ICE_GATHERER_INTERFACE_H_

#include <memory>
#include "api/ref_count.h"

namespace webrtc {
class PortAllocatorSession;

// An IceGatherer is basically a shareable PortAllocatorSession.
// This is useful for doing ICE forking, where the local ports are shared
// between many IceTransports. As long as the IceGatherer is not destroyed, the
// PortAllocatorSession is valid.
class IceGathererInterface : public webrtc::RefCountInterface {
 public:
  virtual webrtc::PortAllocatorSession* port_allocator_session() = 0;
  virtual ~IceGathererInterface() = default;
};

}  // namespace webrtc
#endif  // API_ICE_GATHERER_INTERFACE_H_
