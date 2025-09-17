/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "rffi/api/injectable_network.h"

#include "api/environment/environment.h"
#include "api/packet_socket_factory.h"
#include "api/transport/network_types.h"
#include "p2p/client/basic_port_allocator.h"
#include "rffi/api/network.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/time_utils.h"

namespace webrtc {

namespace rffi {

class InjectableUdpSocket : public AsyncPacketSocket {
 public:
  InjectableUdpSocket(InjectableNetwork* network,
                      const SocketAddress& local_address)
      : network_(network), local_address_(local_address) {}
  ~InjectableUdpSocket() override { network_->ForgetUdp(local_address_); }

  // As AsyncPacketSocket
  SocketAddress GetLocalAddress() const override { return local_address_; }

  // As AsyncPacketSocket
  SocketAddress GetRemoteAddress() const override {
    // Only used for TCP.
    return SocketAddress();
  }

  // As AsyncPacketSocket
  int Send(const void* data,
           size_t data_size,
           const AsyncSocketPacketOptions& options) override {
    // Only used for TCP
    return -1;
  }

  // As AsyncPacketSocket
  int SendTo(const void* data,
             size_t data_size,
             const SocketAddress& remote_address,
             const AsyncSocketPacketOptions& options) override {
    // RTC_LOG(LS_VERBOSE) << "InjectableUdpSocket::SendTo()"
    //                     << " from " << local_address_.ToString()
    //                     << " to " << remote_address.ToString();
    int result =
        network_->SendUdp(local_address_, remote_address,
                          static_cast<const uint8_t*>(data), data_size);
    if (result < 0) {
      last_error_ = result;
      return result;
    }

    // Ends up going to Call::OnSentPacket for congestion control purposes.
    SentPacketInfo sent_packet(options.packet_id, TimeMillis());
    SignalSentPacket(this, sent_packet);
    return result;
  }

  void ReceiveFrom(const uint8_t* data,
                   size_t data_size,
                   const SocketAddress& remote_address) {
    RTC_LOG(LS_VERBOSE) << "InjectableUdpSocket::ReceiveFrom()"
                        << " from " << remote_address.ToString() << " to "
                        << local_address_.ToString();
    NotifyPacketReceived(ReceivedIpPacket::CreateFromLegacy(
        reinterpret_cast<const char*>(data), data_size, TimeMicros(),
        remote_address));
  }

  // As AsyncPacketSocket
  int Close() override {
    // This appears to never be called.
    // And the real "close" is the destructor.
    return -1;
  }

  // As AsyncPacketSocket
  State GetState() const override {
    // UDPPort waits until it's bound to generate a candidate and send binding
    // requests. If it's not currently bound, it will listen for
    // SignalAddressReady.
    // TODO: Simulate slow binds?
    return AsyncPacketSocket::STATE_BOUND;
  }

  // As AsyncPacketSocket
  int GetOption(Socket::Option option, int* value) override {
    // This appears to never be called.
    return -1;
  }

  // As AsyncPacketSocket
  int SetOption(Socket::Option option, int value) override {
    // This is used to:
    //  Set OPT_NODELAY on TCP connections (we can ignore that)
    //  Set OPT_DSCP when DSCP is enabled (we can ignore that)
    //  Set OPT_SNDBUF to 65536 (when video is used)
    //  Set OPT_RCVBUF to 262144 (when video is used)
    // TODO: Simulate changes to OPT_SNDBUF and OPT_RCVBUF

    // Pretend it worked.
    return 1;
  }

  // As AsyncPacketSocket
  int GetError() const override {
    // UDPPort and TurnPort will call this if SendTo fails (returns < 0).
    // And that gets bubbled all the way up to RtpTransport::SendPacket
    // which will check to see if it's ENOTCONN, at which point it will
    // stop sending RTP/RTCP until SignalReadyToSend fires (weird, right?).
    // TODO: Simulate "ready" or "not ready to send" by returning ENOTCONN
    // and firing SignalReadyToSend at the appropriate times.
    return last_error_;
  }

  // As AsyncPacketSocket
  void SetError(int error) override {
    // This appears to never be called.
  }

 private:
  InjectableNetwork* network_;
  SocketAddress local_address_;
  int last_error_ = 0;
};

class InjectableNetworkImpl : public InjectableNetwork,
                              public NetworkManager,
                              public PacketSocketFactory {
 public:
  InjectableNetworkImpl(const Environment& env, Thread* network_thread)
      : env_(env), network_thread_(network_thread) {}

  ~InjectableNetworkImpl() override {
    if (sender_.object_owned) {
      sender_.Delete(sender_.object_owned);
    }
  }

  // As InjectableNetwork
  std::unique_ptr<PortAllocator> CreatePortAllocator() override {
    RTC_LOG(LS_INFO) << "InjectableNetworkImpl::CreatePortAllocator()";
    return network_thread_->BlockingCall([this] {
      return std::make_unique<BasicPortAllocator>(env_, this, this);
    });
  }

  void SetSender(const InjectableNetworkSender* sender) override {
    RTC_LOG(LS_INFO) << "InjectableNetworkImpl::SetSender()";
    sender_ = *sender;
  }

  // name used for debugging a lot, but also as an ID for the network for TURN
  // pruning. type Affects Candidate network cost and other ICE behavior
  // preference affects ICE candidate priorities higher is more preferred
  void AddInterface(const char* name,
                    AdapterType type,
                    Ip ip,
                    uint16_t preference) override {
    RTC_LOG(LS_INFO) << "InjectableNetworkImpl::AddInterface() name: " << name;
    // We need to access interface_by_name_ and SignalNetworksChanged on the
    // network_thread_. Make sure to copy the name first!
    network_thread_->PostTask([this, name{std::string(name)}, type, ip,
                               preference] {
      // TODO: Support different IP prefixes.
      auto interface = std::make_unique<Network>(name, name /* description */,
                                                 IpToRtcIp(ip) /* prefix */,
                                                 0 /* prefix_length */, type);
      // TODO: Add more than one IP per network interface
      interface->AddIP(IpToRtcIp(ip));
      interface->set_preference(preference);
      interface_by_name_.insert({std::move(name), std::move(interface)});
      SignalNetworksChanged();
    });
  }

  void RemoveInterface(const char* name) override {
    RTC_LOG(LS_INFO) << "InjectableNetworkImpl::RemoveInterface() name: "
                     << name;
    // We need to access interface_by_name_ on the network_thread_.
    // Make sure to copy the name first!
    network_thread_->PostTask(
        [this, name{std::string(name)}] { interface_by_name_.erase(name); });
  }

  void ReceiveUdp(IpPort source,
                  IpPort dest,
                  const uint8_t* data,
                  size_t size) override {
    // The network stack expects everything to happen on the network thread.
    // Make sure to copy the data!
    network_thread_->PostTask([this, source, dest,
                               data{std::vector<uint8_t>(data, data + size)},
                               size] {
      auto local_address = IpPortToRtcSocketAddress(dest);
      auto remote_address = IpPortToRtcSocketAddress(source);
      RTC_LOG(LS_VERBOSE) << "InjectableNetworkImpl::ReceiveUdp()"
                          << " from " << remote_address.ToString() << " to "
                          << local_address.ToString() << " size: " << size;
      auto udp_socket = udp_socket_by_local_address_.find(local_address);
      if (udp_socket == udp_socket_by_local_address_.end()) {
        RTC_LOG(LS_WARNING) << "Received packet for unknown local address.";
        return;
      }
      udp_socket->second->ReceiveFrom(data.data(), data.size(), remote_address);
    });
  }

  int SendUdp(const SocketAddress& local_address,
              const SocketAddress& remote_address,
              const uint8_t* data,
              size_t size) override {
    if (!sender_.object_owned) {
      RTC_LOG(LS_WARNING) << "Dropping packet because no sender set.";
      return -1;
    }
    IpPort local = RtcSocketAddressToIpPort(local_address);
    IpPort remote = RtcSocketAddressToIpPort(remote_address);
    // RTC_LOG(LS_VERBOSE) << "InjectableNetworkImpl::SendUdp()"
    //                     << " from " << local_address.ToString()
    //                     << " to " << remote_address.ToString()
    //                     << " size: " << size;
    sender_.SendUdp(sender_.object_owned, local, remote, data, size);
    return size;
  }

  void ForgetUdp(const SocketAddress& local_address) override {
    // We need to access udp_socket_by_local_address_ on the network_thread_.
    network_thread_->PostTask([this, local_address] {
      udp_socket_by_local_address_.erase(local_address);
    });
  }

  // As NetworkManager
  void StartUpdating() override {
    RTC_DCHECK(network_thread_->IsCurrent());
    RTC_LOG(LS_INFO) << "InjectableNetworkImpl::StartUpdating()";
    // TODO: Add support for changing networks dynamically.
    //       BasicPortAllocatorSession listens to it do detect when networks
    //       have failed (gone away)
    // Documentation says this must be called by StartUpdating() once the
    // network list is available.
    SignalNetworksChanged();
  }

  // As NetworkManager
  void StopUpdating() override {}

  // As NetworkManager
  std::vector<const Network*> GetNetworks() const override {
    RTC_LOG(LS_INFO) << "InjectableNetworkImpl::GetNetworks()";
    RTC_DCHECK(network_thread_->IsCurrent());

    std::vector<const Network*> networks;
    for (const auto& kv : interface_by_name_) {
      networks.push_back(kv.second.get());
    }

    return networks;
  }

  // As NetworkManager
  MdnsResponderInterface* GetMdnsResponder() const override {
    // We'll probably never use mDNS
    return nullptr;
  }

  // As NetworkManager
  std::vector<const Network*> GetAnyAddressNetworks() override {
    // TODO: Add support for using a default route instead of choosing a
    // particular network. (such as when we can't enumerate networks or IPs)
    std::vector<const Network*> networks;

    return networks;
  }

  // As NetworkManager
  EnumerationPermission enumeration_permission() const override {
    // This is only really needed for web security things we don't need to worry
    // about. So, always allow.
    return ENUMERATION_ALLOWED;
  }

  // As NetworkManager
  bool GetDefaultLocalAddress(int family, IPAddress* ipaddr) const override {
    // TODO: Add support for using a default route instead of choosing a
    // particular network. (such as when we can't enumerate networks or IPs)
    return false;
  }

  // As PacketSocketFactory
  AsyncPacketSocket* CreateUdpSocket(
      const SocketAddress& local_address_without_port,
      uint16_t min_port,
      uint16_t max_port) override {
    RTC_DCHECK(network_thread_->IsCurrent());
    RTC_LOG(LS_INFO) << "InjectableNetworkImpl::CreateUdpSocket() ip: "
                     << local_address_without_port.ip();
    const IPAddress& local_ip = local_address_without_port.ipaddr();
    // The min_port and max_port are ultimately controlled by the PortAllocator,
    // which we create, so we can ignore those.
    // And the local_address is supposed to have a port of 0.
    uint16_t local_port = next_udp_port_++;
    SocketAddress local_address(local_ip, local_port);
    auto udp_socket = new InjectableUdpSocket(this, local_address);
    udp_socket_by_local_address_.insert({local_address, udp_socket});
    // This really should return a std::unique_ptr because callers all take
    // ownership.
    return udp_socket;
  }

  // As PacketSocketFactory
  AsyncListenSocket* CreateServerTcpSocket(const SocketAddress& local_address,
                                           uint16_t min_port,
                                           uint16_t max_port,
                                           int opts) override {
    // We never plan to support TCP ICE (other than through TURN),
    // So we'll never implement this.
    return nullptr;
  }

  // As PacketSocketFactory
  AsyncPacketSocket* CreateClientTcpSocket(
      const SocketAddress& local_address,
      const SocketAddress& remote_address,
      const PacketSocketTcpOptions& tcp_options) override {
    // TODO: Support TCP for TURN
    return nullptr;
  }

  // As PacketSocketFactory
  std::unique_ptr<AsyncDnsResolverInterface> CreateAsyncDnsResolver() override {
    // TODO: Add support for DNS-based STUN/TURN servers.
    // For now, just use IP addresses
    return nullptr;
  }

 private:
  const Environment env_;
  Thread* network_thread_;
  std::map<std::string, std::unique_ptr<Network>> interface_by_name_;
  std::map<SocketAddress, InjectableUdpSocket*> udp_socket_by_local_address_;
  // The ICE stack does not like ports below 1024.
  // Give it a nice even number to count up from.
  uint16_t next_udp_port_ = 2001;
  InjectableNetworkSender sender_ = {};
};

std::unique_ptr<InjectableNetwork> CreateInjectableNetwork(
    const Environment& env,
    Thread* network_thread) {
  return std::make_unique<InjectableNetworkImpl>(env, network_thread);
}

// The passed-in sender must live as long as the InjectableNetwork,
// which likely means it must live as long as the PeerConnection.
RUSTEXPORT void Rust_InjectableNetwork_SetSender(
    InjectableNetwork* network_borrowed,
    const InjectableNetworkSender* sender_borrowed) {
  network_borrowed->SetSender(sender_borrowed);
}

RUSTEXPORT void Rust_InjectableNetwork_AddInterface(
    InjectableNetwork* network_borrowed,
    const char* name_borrowed,
    AdapterType type,
    Ip ip,
    uint16_t preference) {
  network_borrowed->AddInterface(name_borrowed, type, ip, preference);
}

RUSTEXPORT void Rust_InjectableNetwork_RemoveInterface(
    InjectableNetwork* network_borrowed,
    const char* name_borrowed) {
  network_borrowed->RemoveInterface(name_borrowed);
}

RUSTEXPORT void Rust_InjectableNetwork_ReceiveUdp(
    InjectableNetwork* network_borrowed,
    IpPort local,
    IpPort remote,
    const uint8_t* data_borrowed,
    size_t size) {
  network_borrowed->ReceiveUdp(local, remote, data_borrowed, size);
}

}  // namespace rffi

}  // namespace webrtc
