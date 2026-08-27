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
  void* object_owned;
  int (*SendUdp)(void* object_borrowed,
                 IpPort source,
                 IpPort dest,
                 const uint8_t* data_borrowed,
                 size_t);
  int (*Delete)(void* object_owned);
} InjectableNetworkSender;

class InjectableNetwork;

RUSTEXPORT void Rust_InjectableNetwork_SetSender(
    InjectableNetwork* network_borrowed,
    const InjectableNetworkSender* sender_borrowed);

RUSTEXPORT void Rust_InjectableNetwork_AddInterface(
    InjectableNetwork* network_borrowed,
    const char* name_borrowed,
    AdapterType type,
    Ip ip,
    uint16_t preference);

RUSTEXPORT void Rust_InjectableNetwork_RemoveInterface(
    InjectableNetwork* network_borrowed,
    const char* name_borrowed);

RUSTEXPORT void Rust_InjectableNetwork_ReceiveUdp(
    InjectableNetwork* network_borrowed,
    IpPort source,
    IpPort dest,
    const uint8_t* data_borrowed,
    size_t size);

}  // namespace rffi

}  // namespace webrtc

#endif /* RFFI_API_INJECTABLE_NETWORK_H__ */
