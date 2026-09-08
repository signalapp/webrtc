/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_API_DEFS_H__
#define RFFI_API_DEFS_H__

/**
 * Common definitions used throughout the Rust RFFI API.
 *
 */

// Public interfaces exported to Rust as "extern C".
#define RUSTEXPORT extern "C" __attribute__((visibility("default")))

namespace webrtc {
namespace rffi {
namespace ptr {
// Transparent "tag" types. These are mostly documentation in the C++ side,
// but rust bindgen will use them to fill in the appropriate pointer type
// (see ringrtc's src/webrtc/ptr.rs)
// The bindgen directive lets us replace the types named here with our own
// definitions. See https://rust-lang.github.io/rust-bindgen/blocklisting.html

/// <div rustbindgen hide></div>
template <typename T>
using Borrowed = T*;

/// <div rustbindgen hide></div>
template <typename T>
using BorrowedRc = T*;

/// <div rustbindgen hide></div>
template <typename T>
using Owned = T*;

/// <div rustbindgen hide></div>
template <typename T>
using OwnedRc = T*;

/// <div rustbindgen hide></div>
template <typename T>
using Unique = T*;

}  // namespace ptr
}  // namespace rffi
}  // namespace webrtc

enum class TransportProtocol {
  kUdp,
  kTcp,
  kTls,
  kUnknown,
};

/* Ice Update Message structure passed between Rust and c++ */
typedef struct {
  webrtc::rffi::ptr::Borrowed<const char> sdp_borrowed;
  bool is_relayed;
  TransportProtocol relay_protocol;
} RustIceCandidate;

#endif /* RFFI_API_DEFS_H__ */
