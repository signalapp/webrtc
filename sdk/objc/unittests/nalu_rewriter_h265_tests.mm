/*
 * Copyright 2026 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>

#include <cmath>
#include <optional>
#include <vector>

#include "rtc_base/buffer.h"
#include "sdk/objc/components/video_codec/nalu_rewriter.h"
#include "test/gtest.h"

#if defined(RTC_ENABLE_H265)

namespace webrtc {
namespace {

using Nalu = std::vector<uint8_t>;

// Parameter sets from VideoToolbox's HEVC encoder at 1280x720.
// clang-format off
const Nalu kVps = {
  0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00,
  0xb0, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5d, 0x17, 0x02, 0x40};
const Nalu kSps = {
  0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0xb0, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x03, 0x00, 0x5d, 0xa0, 0x02, 0x80, 0x80, 0x2d, 0x16,
  0x20, 0x5e, 0xe4, 0x59, 0x14, 0xbf, 0xf2, 0xe7, 0xf1, 0x3f, 0xa9, 0xb8,
  0x10, 0x10, 0x10, 0x04};
// Same id and resolution as kSps, from an intra-only encode.
const Nalu kIntraOnlySps = {
  0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0xb0, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x03, 0x00, 0x5d, 0xa0, 0x02, 0x80, 0x80, 0x2d, 0x16,
  0x20, 0x5e, 0xe4, 0x59, 0x54, 0xdc, 0x08, 0x08, 0x08, 0x02};
// Same id as kSps, but 640x360.
const Nalu kSps640x360 = {
  0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0xb0, 0x00, 0x00,
  0x03, 0x00, 0x00, 0x03, 0x00, 0x3f, 0xa0, 0x05, 0x02, 0x01, 0x71, 0xf2,
  0xe2, 0x05, 0xee, 0x45, 0x91, 0x4b, 0xff, 0x2e, 0x7f, 0x13, 0xfa, 0x9b,
  0x81, 0x01, 0x01, 0x00, 0x40};
const Nalu kPps = {0x44, 0x01, 0xc0, 0x72, 0xf4, 0x53, 0x64};
// kPps with pps_pic_parameter_set_id 1.
const Nalu kPpsId1 = {0x44, 0x01, 0x50, 0x1c, 0xbd, 0x14, 0xd9};
const Nalu kIdrSlice = {0x28, 0x01, 0xaf, 0x09};
const Nalu kTrailSlice = {0x02, 0x01, 0xd0, 0x09};
// clang-format on

Nalu AnnexB(const std::vector<Nalu>& nalus) {
  Nalu buffer;
  for (const Nalu& nalu : nalus) {
    buffer.insert(buffer.end(), {0x00, 0x00, 0x00, 0x01});
    buffer.insert(buffer.end(), nalu.begin(), nalu.end());
  }
  return buffer;
}

CMVideoFormatDescriptionRef CreateFormat(const std::vector<Nalu>& nalus) {
  return CreateH265VideoFormatDescription(AnnexB(nalus));
}

// Returns the length-prefixed NALUs in `sample_buffer`.
std::vector<Nalu> SampleNalus(CMSampleBufferRef sample_buffer) {
  CMBlockBufferRef block_buffer = CMSampleBufferGetDataBuffer(sample_buffer);
  Nalu data(CMBlockBufferGetDataLength(block_buffer));
  CMBlockBufferCopyDataBytes(block_buffer, 0, data.size(), data.data());
  std::vector<Nalu> nalus;
  size_t offset = 0;
  while (offset + 4 <= data.size()) {
    uint32_t size = (data[offset] << 24) | (data[offset + 1] << 16) |
        (data[offset + 2] << 8) | data[offset + 3];
    offset += 4;
    if (offset + size > data.size()) {
      break;
    }
    nalus.emplace_back(data.begin() + offset, data.begin() + offset + size);
    offset += size;
  }
  return nalus;
}

// Converts `annexb` with `format` and returns the NALUs in the sample buffer,
// or nullopt if the conversion fails.
std::optional<std::vector<Nalu>> Convert(const Nalu& annexb,
                                         CMVideoFormatDescriptionRef format) {
  CMMemoryPoolRef memory_pool = CMMemoryPoolCreate(nil);
  CMSampleBufferRef sample_buffer = nullptr;
  bool ok = H265AnnexBBufferToCMSampleBuffer(
      annexb, format, &sample_buffer, memory_pool);
  std::optional<std::vector<Nalu>> nalus;
  if (ok) {
    nalus = SampleNalus(sample_buffer);
    CFRelease(sample_buffer);
  }
  CMMemoryPoolInvalidate(memory_pool);
  CFRelease(memory_pool);
  return nalus;
}

size_t ParameterSetCount(CMVideoFormatDescriptionRef format) {
  size_t count = 0;
  CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
      format, 0, nullptr, nullptr, &count, nullptr);
  return count;
}

// Encoded HEVC stream from VideoToolbox, for decoding tests.
struct TestStream {
  std::vector<CMSampleBufferRef> samples;
  std::vector<CVPixelBufferRef> frames;
  ~TestStream() {
    for (CMSampleBufferRef sample : samples) {
      CFRelease(sample);
    }
    for (CVPixelBufferRef frame : frames) {
      CVPixelBufferRelease(frame);
    }
  }
};

const OSType kPixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;

// A ramp with a block that moves each frame, so a wrongly decoded frame has a
// low PSNR.
CVPixelBufferRef CreateTestFrame(int width, int height, int index) {
  NSDictionary* attributes =
      @{(NSString*)kCVPixelBufferIOSurfacePropertiesKey : @{}};
  CVPixelBufferRef frame = nullptr;
  CVPixelBufferCreate(nullptr,
                      width,
                      height,
                      kPixelFormat,
                      (__bridge CFDictionaryRef)attributes,
                      &frame);
  CVPixelBufferLockBaseAddress(frame, 0);
  auto* y = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(frame, 0));
  size_t y_stride = CVPixelBufferGetBytesPerRowOfPlane(frame, 0);
  int block_x = (index * 8) % (width - 64);
  int block_y = (index * 4) % (height - 64);
  for (int row = 0; row < height; ++row) {
    for (int col = 0; col < width; ++col) {
      int value = (col * 255 / width + row * 128 / height) & 0xff;
      if (col >= block_x && col < block_x + 64 && row >= block_y &&
          row < block_y + 64) {
        value = 255 - value;
      }
      y[row * y_stride + col] = static_cast<uint8_t>(value);
    }
  }
  auto* uv =
      static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(frame, 1));
  size_t uv_stride = CVPixelBufferGetBytesPerRowOfPlane(frame, 1);
  for (int row = 0; row < height / 2; ++row) {
    for (int col = 0; col < width; ++col) {
      uv[row * uv_stride + col] = 128;
    }
  }
  CVPixelBufferUnlockBaseAddress(frame, 0);
  return frame;
}

void EncodeCallback(void* refcon,
                    void*,
                    OSStatus status,
                    VTEncodeInfoFlags,
                    CMSampleBufferRef sample) {
  if (status == noErr && sample) {
    CFRetain(sample);
    static_cast<TestStream*>(refcon)->samples.push_back(sample);
  }
}

// Encodes `frame_count` 1280x720 frames with the settings
// RTCVideoEncoderH264 uses for H265. Returns false if there is no encoder.
bool EncodeTestStream(int frame_count,
                      bool allow_temporal_compression,
                      TestStream& stream) {
  const int kWidth = 1280;
  const int kHeight = 720;
  NSDictionary* source_attributes = @{
    (NSString*)kCVPixelBufferPixelFormatTypeKey : @(kPixelFormat),
    (NSString*)kCVPixelBufferIOSurfacePropertiesKey : @{},
  };
  VTCompressionSessionRef session = nullptr;
  if (VTCompressionSessionCreate(nullptr,
                                 kWidth,
                                 kHeight,
                                 kCMVideoCodecType_HEVC,
                                 nullptr,
                                 (__bridge CFDictionaryRef)source_attributes,
                                 nullptr,
                                 EncodeCallback,
                                 &stream,
                                 &session) != noErr) {
    return false;
  }
  VTSessionSetProperty(
      session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
  VTSessionSetProperty(session,
                       kVTCompressionPropertyKey_ProfileLevel,
                       kVTProfileLevel_HEVC_Main_AutoLevel);
  VTSessionSetProperty(
      session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
  VTSessionSetProperty(session,
                       kVTCompressionPropertyKey_AverageBitRate,
                       (__bridge CFTypeRef) @(2'000'000));
  VTSessionSetProperty(
      session,
      kVTCompressionPropertyKey_AllowTemporalCompression,
      allow_temporal_compression ? kCFBooleanTrue : kCFBooleanFalse);
  for (int i = 0; i < frame_count; ++i) {
    CVPixelBufferRef frame = CreateTestFrame(kWidth, kHeight, i);
    stream.frames.push_back(frame);
    NSDictionary* properties = i == 0 ?
        @{(NSString*)kVTEncodeFrameOptionKey_ForceKeyFrame : @YES} :
        nil;
    VTCompressionSessionEncodeFrame(session,
                                    frame,
                                    CMTimeMake(i, 30),
                                    CMTimeMake(1, 30),
                                    (__bridge CFDictionaryRef)properties,
                                    nullptr,
                                    nullptr);
  }
  VTCompressionSessionCompleteFrames(session, kCMTimeInvalid);
  VTCompressionSessionInvalidate(session);
  CFRelease(session);
  return stream.samples.size() == static_cast<size_t>(frame_count);
}

double LumaPsnr(CVPixelBufferRef a, CVPixelBufferRef b) {
  size_t width = CVPixelBufferGetWidth(a);
  size_t height = CVPixelBufferGetHeight(a);
  if (width != CVPixelBufferGetWidth(b) ||
      height != CVPixelBufferGetHeight(b)) {
    return 0;
  }
  CVPixelBufferLockBaseAddress(a, kCVPixelBufferLock_ReadOnly);
  CVPixelBufferLockBaseAddress(b, kCVPixelBufferLock_ReadOnly);
  auto* a_y = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(a, 0));
  auto* b_y = static_cast<uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(b, 0));
  size_t a_stride = CVPixelBufferGetBytesPerRowOfPlane(a, 0);
  size_t b_stride = CVPixelBufferGetBytesPerRowOfPlane(b, 0);
  double squared_error = 0;
  for (size_t row = 0; row < height; ++row) {
    for (size_t col = 0; col < width; ++col) {
      double diff =
          double(a_y[row * a_stride + col]) - double(b_y[row * b_stride + col]);
      squared_error += diff * diff;
    }
  }
  CVPixelBufferUnlockBaseAddress(a, kCVPixelBufferLock_ReadOnly);
  CVPixelBufferUnlockBaseAddress(b, kCVPixelBufferLock_ReadOnly);
  double mse = squared_error / double(width * height);
  return mse == 0 ? 99 : 10 * log10(255.0 * 255.0 / mse);
}

struct DecodeOutput {
  OSStatus status = noErr;
  CVImageBufferRef image = nullptr;
};

void DecodeCallback(void* refcon,
                    void*,
                    OSStatus status,
                    VTDecodeInfoFlags,
                    CVImageBufferRef image,
                    CMTime,
                    CMTime) {
  auto* output = static_cast<DecodeOutput*>(refcon);
  output->status = status;
  if (image) {
    output->image = CVPixelBufferRetain(image);
  }
}

TEST(NaluRewriterH265Test, DropsParameterSetsInFormat) {
  CMVideoFormatDescriptionRef format = CreateFormat({kVps, kSps, kPps});
  ASSERT_TRUE(format);

  std::optional<std::vector<Nalu>> nalus =
      Convert(AnnexB({kVps, kSps, kPps, kIdrSlice}), format);
  ASSERT_TRUE(nalus);
  EXPECT_EQ(*nalus, std::vector<Nalu>({kIdrSlice}));

  CFRelease(format);
}

TEST(NaluRewriterH265Test, KeepsParameterSetsNotInFormat) {
  CMVideoFormatDescriptionRef format =
      CreateFormat({kVps, kIntraOnlySps, kPps});
  ASSERT_TRUE(format);

  // The SPS differs from the format's, so all parameter sets are kept, in
  // order.
  std::optional<std::vector<Nalu>> nalus =
      Convert(AnnexB({kVps, kSps, kPps, kIdrSlice}), format);
  ASSERT_TRUE(nalus);
  EXPECT_EQ(*nalus, std::vector<Nalu>({kVps, kSps, kPps, kIdrSlice}));

  CFRelease(format);
}

TEST(NaluRewriterH265Test, KeepsParameterSetOnDeltaFrame) {
  CMVideoFormatDescriptionRef format =
      CreateFormat({kVps, kIntraOnlySps, kPps});
  ASSERT_TRUE(format);

  std::optional<std::vector<Nalu>> nalus =
      Convert(AnnexB({kSps, kTrailSlice}), format);
  ASSERT_TRUE(nalus);
  EXPECT_EQ(*nalus, std::vector<Nalu>({kSps, kTrailSlice}));

  CFRelease(format);
}

TEST(NaluRewriterH265Test, FailsWithoutFrameData) {
  CMVideoFormatDescriptionRef format =
      CreateFormat({kVps, kIntraOnlySps, kPps});
  ASSERT_TRUE(format);

  EXPECT_FALSE(Convert(AnnexB({kSps}), format));

  CFRelease(format);
}

TEST(NaluRewriterH265Test, FormatRequiresEveryParameterSetType) {
  EXPECT_FALSE(CreateFormat({kVps, kSps, kIdrSlice}));
  EXPECT_FALSE(CreateFormat({kSps, kPps, kIdrSlice}));
}

TEST(NaluRewriterH265Test, FormatKeepsEveryParameterSet) {
  CMVideoFormatDescriptionRef format =
      CreateFormat({kVps, kSps, kPps, kPpsId1, kIdrSlice});
  ASSERT_TRUE(format);
  EXPECT_EQ(4u, ParameterSetCount(format));

  // Both PPS are in the format, so the sample doesn't need either of them.
  std::optional<std::vector<Nalu>> nalus =
      Convert(AnnexB({kVps, kSps, kPps, kPpsId1, kIdrSlice}), format);
  ASSERT_TRUE(nalus);
  EXPECT_EQ(*nalus, std::vector<Nalu>({kIdrSlice}));

  CFRelease(format);
}

TEST(NaluRewriterH265Test, FormatUsesLastSpsWithSameId) {
  CMVideoFormatDescriptionRef format =
      CreateFormat({kVps, kSps, kSps640x360, kPps});
  ASSERT_TRUE(format);
  CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(format);
  EXPECT_EQ(640, dimensions.width);
  EXPECT_EQ(360, dimensions.height);
  CFRelease(format);

  format = CreateFormat({kVps, kSps640x360, kSps, kPps});
  ASSERT_TRUE(format);
  dimensions = CMVideoFormatDescriptionGetDimensions(format);
  EXPECT_EQ(1280, dimensions.width);
  EXPECT_EQ(720, dimensions.height);
  CFRelease(format);
}

// Decodes a VideoToolbox stream with a session whose format has a different
// SPS than the stream, as happens when the stream's SPS can't be put into a
// new format. The stream's SPS is only sent on the keyframe, so this passes
// only if VideoToolbox applies the in-band SPS and keeps using it.
TEST(NaluRewriterH265Test, DecodesWithParameterSetsNotInFormat) {
  if (!VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC)) {
    GTEST_SKIP() << "No hardware HEVC decoder";
  }
  const int kFrameCount = 10;
  TestStream stream;
  TestStream intra_only_stream;
  if (!EncodeTestStream(
          kFrameCount, /*allow_temporal_compression=*/true, stream) ||
      !EncodeTestStream(
          1, /*allow_temporal_compression=*/false, intra_only_stream)) {
    GTEST_SKIP() << "No HEVC encoder";
  }
  CMVideoFormatDescriptionRef stale_format =
      CMSampleBufferGetFormatDescription(intra_only_stream.samples[0]);
  CMVideoFormatDescriptionRef stream_format =
      CMSampleBufferGetFormatDescription(stream.samples[0]);
  ASSERT_FALSE(CMFormatDescriptionEqual(stale_format, stream_format));

  NSDictionary* attributes =
      @{(NSString*)kCVPixelBufferPixelFormatTypeKey : @(kPixelFormat)};
  DecodeOutput output;
  VTDecompressionOutputCallbackRecord callback = {DecodeCallback, &output};
  VTDecompressionSessionRef session = nullptr;
  ASSERT_EQ(noErr,
            VTDecompressionSessionCreate(nullptr,
                                         stale_format,
                                         nullptr,
                                         (__bridge CFDictionaryRef)attributes,
                                         &callback,
                                         &session));
  CMMemoryPoolRef memory_pool = CMMemoryPoolCreate(nil);

  for (int i = 0; i < kFrameCount; ++i) {
    SCOPED_TRACE(i);
    Buffer annexb;
    ASSERT_TRUE(H265CMSampleBufferToAnnexBBuffer(
        stream.samples[i], /*is_keyframe=*/i == 0, &annexb));
    CMSampleBufferRef sample = nullptr;
    ASSERT_TRUE(H265AnnexBBufferToCMSampleBuffer(
        annexb, stale_format, &sample, memory_pool));

    output = DecodeOutput();
    EXPECT_EQ(noErr,
              VTDecompressionSessionDecodeFrame(
                  session, sample, 0, nullptr, nullptr));
    VTDecompressionSessionWaitForAsynchronousFrames(session);
    CFRelease(sample);
    EXPECT_EQ(noErr, output.status);
    EXPECT_TRUE(output.image);
    if (output.image) {
      EXPECT_GT(LumaPsnr(output.image, stream.frames[i]), 40);
      CVPixelBufferRelease(output.image);
    }
  }

  VTDecompressionSessionInvalidate(session);
  CFRelease(session);
  CMMemoryPoolInvalidate(memory_pool);
  CFRelease(memory_pool);
}

}  // namespace
}  // namespace webrtc

#endif  // defined(RTC_ENABLE_H265)
