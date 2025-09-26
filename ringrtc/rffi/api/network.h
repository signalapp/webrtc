/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_API_NETWORK_H__
#define RFFI_API_NETWORK_H__

#include "rtc_base/ip_address.h"
#include "rtc_base/socket_address.h"

namespace webrtc {

namespace rffi {

// A simplified version of IpAddress
typedef struct {
  // If v6 == false, only use the first 4 bytes.
  bool v6;
  uint8_t address[16];
} Ip;

// A simplified version of SocketAddress
typedef struct {
  Ip ip;
  uint16_t port;
} IpPort;

IPAddress IpToRtcIp(Ip ip);
SocketAddress IpPortToRtcSocketAddress(IpPort ip_port);
Ip RtcIpToIp(IPAddress address);
IpPort RtcSocketAddressToIpPort(const SocketAddress& address);

}  // namespace rffi

}  // namespace webrtc

#endif /* RFFI_API_NETWORK_H__ */
