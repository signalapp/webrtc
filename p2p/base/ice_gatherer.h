/*
 *  Copyright 2016 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef P2P_BASE_ICE_GATHERER_H_
#define P2P_BASE_ICE_GATHERER_H_

#include <memory>
#include "api/ice_gatherer_interface.h"
#include "p2p/base/port_allocator.h"

namespace cricket {

// RingRTC change to add ICE forking
// A simple implementation of an IceGatherer that owns the
// PortAllocator and PortAllocatorSession.
class BasicIceGatherer : public webrtc::IceGathererInterface {
 public:
  BasicIceGatherer(
      rtc::Thread* network_thread,
      std::unique_ptr<PortAllocator> port_allocator,
      std::unique_ptr<PortAllocatorSession> port_allocator_session);
  ~BasicIceGatherer() override;

  PortAllocatorSession* port_allocator_session() override;

  // For tests
  PortAllocator* port_allocator() { return port_allocator_.get(); }

 private:
  rtc::Thread* network_thread_;
  std::unique_ptr<PortAllocator> port_allocator_;
  std::unique_ptr<PortAllocatorSession> port_allocator_session_;
};

}  // namespace cricket

#endif  // P2P_BASE_ICE_GATHERER_H_
