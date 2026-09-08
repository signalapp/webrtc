/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_API_INJECTABLE_NETWORK_H__
#define RFFI_API_INJECTABLE_NETWORK_H__

#include <cstddef>

#include "rffi/api/network.h"
#include "rffi/api/rffi_defs.h"
#include "rffi/api/webrtc_common.h"

namespace webrtc {

namespace rffi {

typedef struct {
  ptr::Owned<void> object_owned;
  int (*SendUdp)(ptr::Borrowed<void> object_borrowed,
                 IpPort source,
                 IpPort dest,
                 ptr::Borrowed<const uint8_t> data_borrowed,
                 size_t);
  int (*Delete)(ptr::Owned<void> object_owned);
} InjectableNetworkSender;

class InjectableNetwork;

RUSTEXPORT void Rust_InjectableNetwork_SetSender(
    ptr::Borrowed<InjectableNetwork> network_borrowed,
    ptr::Borrowed<const InjectableNetworkSender> sender_borrowed);

RUSTEXPORT void Rust_InjectableNetwork_AddInterface(
    ptr::Borrowed<InjectableNetwork> network_borrowed,
    ptr::Borrowed<const char> name_borrowed,
    AdapterType type,
    Ip ip,
    uint16_t preference);

RUSTEXPORT void Rust_InjectableNetwork_RemoveInterface(
    ptr::Borrowed<InjectableNetwork> network_borrowed,
    ptr::Borrowed<const char> name_borrowed);

RUSTEXPORT void Rust_InjectableNetwork_ReceiveUdp(
    ptr::Borrowed<InjectableNetwork> network_borrowed,
    IpPort source,
    IpPort dest,
    ptr::Borrowed<const uint8_t> data_borrowed,
    size_t size);

}  // namespace rffi

}  // namespace webrtc

#endif /* RFFI_API_INJECTABLE_NETWORK_H__ */
