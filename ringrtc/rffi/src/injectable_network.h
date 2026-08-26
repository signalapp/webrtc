/*
 * Copyright 2026 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_INJECTABLE_NETWORK_H__
#define RFFI_INJECTABLE_NETWORK_H__

namespace webrtc {
namespace rffi {

// This is a class that acts like a PortAllocator + PacketSocketFactory +
// NetworkManager to the network stack and allows simulated or injected networks
// to control the flow of packets and which network interfaces come up and down.
class InjectableNetwork {
 public:
  virtual ~InjectableNetwork() = default;

  // This is what the network stack sees.
  // The PacketSocketFactory and NetworkManager are referenced by the
  // PortAllocator.
  virtual std::unique_ptr<PortAllocator> CreatePortAllocator() = 0;

  // This is what the "driver" of the network sees: control of packets,
  // network interfaces, etc.
  virtual void SetSender(const InjectableNetworkSender* sender) = 0;
  virtual void AddInterface(const char* name,
                            AdapterType type,
                            Ip ip,
                            uint16_t preference) = 0;
  virtual void RemoveInterface(const char* name) = 0;
  virtual void ReceiveUdp(IpPort source,
                          IpPort dest,
                          const uint8_t* data,
                          size_t size) = 0;

  // These are more for internal use, not external, which is why the types
  // aren't the external types.
  virtual int SendUdp(const SocketAddress& local_address,
                      const SocketAddress& remote_address,
                      const uint8_t* data,
                      size_t size) = 0;
  virtual void ForgetUdp(const SocketAddress& local_address) = 0;
};

std::unique_ptr<InjectableNetwork> CreateInjectableNetwork(
    const Environment& env,
    Thread* network_thread);

}  // namespace rffi

}  // namespace webrtc

#endif  // RFFI_INJECTABLE_NETWORK_H__
