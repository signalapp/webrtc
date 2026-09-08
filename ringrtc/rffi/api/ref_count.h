/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

/*
 * Rust friendly wrappers for:
 *
 *   webrtc::RefCountInterface::Release();
 *   webrtc::RefCountInterface::AddRef();
 */

#ifndef RFFI_API_SCOPED_REFPTR_H__
#define RFFI_API_SCOPED_REFPTR_H__

#include "rffi/api/rffi_defs.h"

namespace webrtc {
class RefCountInterface;

namespace rffi {
// Decrements the ref count of a ref-counted object.
// If the ref count goes to zero, the object is deleted.
RUSTEXPORT void Rust_decRc(ptr::OwnedRc<webrtc::RefCountInterface> owned_rc);

// Increments the ref count of a ref-counted object.
// The borrowed RC becomes an owned RC.
RUSTEXPORT void Rust_incRc(
    ptr::BorrowedRc<webrtc::RefCountInterface> borrowed_rc);
}  // namespace rffi
}  // namespace webrtc

#endif /* RFFI_API_SCOPED_REFPTR_H__ */
