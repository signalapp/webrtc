/*
 * Copyright 2019-2021 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#ifndef RFFI_API_MEDIA_H__
#define RFFI_API_MEDIA_H__

#include <cstdint>

#include "rffi/api/rffi_defs.h"
#include "rffi/api/webrtc_common.h"

typedef struct {
  uint32_t width;
  uint32_t height;
  webrtc::VideoRotation rotation;
} RffiVideoFrameMetadata;

namespace webrtc {
class AudioTrackInterface;
class VideoTrackInterface;
class VideoFrameBuffer;
namespace rffi {

class VideoSource;

// Same as AudioTrack::set_enabled
RUSTEXPORT void Rust_setAudioTrackEnabled(
    ptr::BorrowedRc<webrtc::AudioTrackInterface> track_borrowed_rc,
    bool);

// Same as VideoTrack::set_enabled
RUSTEXPORT void Rust_setVideoTrackEnabled(
    ptr::BorrowedRc<webrtc::VideoTrackInterface> track_borrowed_rc,
    bool);

// Same as VideoTrack::set_content_hint with true == kText and false == kNone
RUSTEXPORT void Rust_setVideoTrackContentHint(
    ptr::BorrowedRc<webrtc::VideoTrackInterface> track_borrowed_rc,
    bool);

// Same as VideoSource::PushVideoFrame, to get frames from Rust to C++.
RUSTEXPORT void Rust_pushVideoFrame(
    ptr::BorrowedRc<webrtc::rffi::VideoSource> source_borrowed_rc,
    ptr::BorrowedRc<webrtc::VideoFrameBuffer> buffer_borrowed_rc);

// Same as VideoSource::OnOutputFormatRequest, to apply a maximum resolution
// and framerate to video.
RUSTEXPORT void Rust_adaptOutputVideoFormat(
    ptr::BorrowedRc<webrtc::rffi::VideoSource> source_borrowed_rc,
    uint16_t width,
    uint16_t height,
    uint8_t fps);

// I420 => I420
RUSTEXPORT ptr::OwnedRc<webrtc::VideoFrameBuffer>
Rust_copyVideoFrameBufferFromI420(uint32_t width,
                                  uint32_t height,
                                  ptr::Borrowed<uint8_t> src_borrowed);

// NV12 => I420
RUSTEXPORT ptr::OwnedRc<webrtc::VideoFrameBuffer>
Rust_copyVideoFrameBufferFromNv12(uint32_t width,
                                  uint32_t height,
                                  ptr::Borrowed<uint8_t> src_borrowed);

// RGBA => I420
RUSTEXPORT ptr::OwnedRc<webrtc::VideoFrameBuffer>
Rust_copyVideoFrameBufferFromRgba(uint32_t width,
                                  uint32_t height,
                                  ptr::Borrowed<uint8_t> src_borrowed);

// I420 => RGBA
RUSTEXPORT bool Rust_convertVideoFrameBufferToRgba(
    ptr::BorrowedRc<const webrtc::VideoFrameBuffer> buffer,
    uint8_t* rgba_out);

// I420 direct access, if possible
RUSTEXPORT ptr::Borrowed<const uint8_t> Rust_getVideoFrameBufferAsI420(
    ptr::BorrowedRc<const webrtc::VideoFrameBuffer> buffer);

// See VideoFrameBuffer::Scale. Output will be in I420.
RUSTEXPORT ptr::OwnedRc<webrtc::VideoFrameBuffer> Rust_scaleVideoFrameBuffer(
    ptr::BorrowedRc<webrtc::VideoFrameBuffer> buffer_borrowed_rc,
    int width,
    int height);

// RGBA => I420
RUSTEXPORT ptr::OwnedRc<webrtc::VideoFrameBuffer>
Rust_copyAndRotateVideoFrameBuffer(
    ptr::BorrowedRc<const webrtc::VideoFrameBuffer> buffer_borrowed_rc,
    webrtc::VideoRotation rotation);

RUSTEXPORT ptr::OwnedRc<webrtc::rffi::VideoSource> Rust_createVideoSource();

}  // namespace rffi
}  // namespace webrtc

#endif /* RFFI_API_MEDIA_H__ */
