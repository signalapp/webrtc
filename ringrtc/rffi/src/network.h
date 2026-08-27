/*
 * Copyright 2026 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_NETWORK_H__
#define RFFI_NETWORK_H__

#include "rffi/api/network.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/socket_address.h"

namespace webrtc {
namespace rffi {

IPAddress IpToRtcIp(Ip ip);
SocketAddress IpPortToRtcSocketAddress(IpPort ip_port);
Ip RtcIpToIp(IPAddress address);
IpPort RtcSocketAddressToIpPort(const SocketAddress& address);

}  // namespace rffi
}  // namespace webrtc

#endif  // RFFI_NETWORK_H__
