/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 */

#include "sdk/objc/components/video_codec/nalu_rewriter.h"

#include <CoreFoundation/CoreFoundation.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "common_video/h264/h264_common.h"
#if defined(RTC_ENABLE_H265)
#include "common_video/h265/h265_common.h"
#endif
#include "rtc_base/buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {

using H264::kAud;
using H264::kSps;
using H264::NaluIndex;
using H264::NaluType;
using H264::ParseNaluType;

const char kAnnexBHeaderBytes[4] = {0, 0, 0, 1};
const size_t kAvccHeaderByteSize = sizeof(uint32_t);

bool H264CMSampleBufferToAnnexBBuffer(CMSampleBufferRef avcc_sample_buffer,
                                      bool is_keyframe,
                                      webrtc::Buffer* annexb_buffer) {
  RTC_DCHECK(avcc_sample_buffer);

  // Get format description from the sample buffer.
  CMVideoFormatDescriptionRef description =
      CMSampleBufferGetFormatDescription(avcc_sample_buffer);
  if (description == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to get sample buffer's description.";
    return false;
  }

  // Get parameter set information.
  int nalu_header_size = 0;
  size_t param_set_count = 0;
  OSStatus status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
      description, 0, nullptr, nullptr, &param_set_count, &nalu_header_size);
  if (status != noErr) {
    RTC_LOG(LS_ERROR) << "Failed to get parameter set.";
    return false;
  }
  RTC_CHECK_EQ(nalu_header_size, kAvccHeaderByteSize);
  RTC_DCHECK_EQ(param_set_count, 2);

  // Truncate any previous data in the buffer without changing its capacity.
  annexb_buffer->SetSize(0);

  // Place all parameter sets at the front of buffer.
  if (is_keyframe) {
    size_t param_set_size = 0;
    const uint8_t* param_set = nullptr;
    for (size_t i = 0; i < param_set_count; ++i) {
      status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
          description, i, &param_set, &param_set_size, nullptr, nullptr);
      if (status != noErr) {
        RTC_LOG(LS_ERROR) << "Failed to get parameter set.";
        return false;
      }
      // Update buffer.
      annexb_buffer->AppendData(kAnnexBHeaderBytes, sizeof(kAnnexBHeaderBytes));
      annexb_buffer->AppendData(reinterpret_cast<const char*>(param_set),
                                param_set_size);
    }
  }

  // Get block buffer from the sample buffer.
  CMBlockBufferRef block_buffer =
      CMSampleBufferGetDataBuffer(avcc_sample_buffer);
  if (block_buffer == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to get sample buffer's block buffer.";
    return false;
  }
  CMBlockBufferRef contiguous_buffer = nullptr;
  // Make sure block buffer is contiguous.
  if (!CMBlockBufferIsRangeContiguous(block_buffer, 0, 0)) {
    status = CMBlockBufferCreateContiguous(
        nullptr, block_buffer, nullptr, nullptr, 0, 0, 0, &contiguous_buffer);
    if (status != noErr) {
      RTC_LOG(LS_ERROR) << "Failed to flatten non-contiguous block buffer: "
                        << status;
      return false;
    }
  } else {
    contiguous_buffer = block_buffer;
    // Retain to make cleanup easier.
    CFRetain(contiguous_buffer);
    block_buffer = nullptr;
  }

  // Now copy the actual data.
  char* data_ptr = nullptr;
  size_t block_buffer_size = CMBlockBufferGetDataLength(contiguous_buffer);
  status = CMBlockBufferGetDataPointer(contiguous_buffer, 0, nullptr, nullptr,
                                       &data_ptr);
  if (status != noErr) {
    RTC_LOG(LS_ERROR) << "Failed to get block buffer data.";
    CFRelease(contiguous_buffer);
    return false;
  }
  size_t bytes_remaining = block_buffer_size;
  while (bytes_remaining > 0) {
    // The size type here must match `nalu_header_size`, we expect 4 bytes.
    // Read the length of the next packet of data. Must convert from big endian
    // to host endian.
    RTC_DCHECK_GE(bytes_remaining, (size_t)nalu_header_size);
    uint32_t* uint32_data_ptr = reinterpret_cast<uint32_t*>(data_ptr);
    uint32_t packet_size = CFSwapInt32BigToHost(*uint32_data_ptr);
    // Update buffer.
    annexb_buffer->AppendData(kAnnexBHeaderBytes, sizeof(kAnnexBHeaderBytes));
    annexb_buffer->AppendData(data_ptr + nalu_header_size, packet_size);

    size_t bytes_written = packet_size + sizeof(kAnnexBHeaderBytes);
    bytes_remaining -= bytes_written;
    data_ptr += bytes_written;
  }
  RTC_DCHECK_EQ(bytes_remaining, (size_t)0);

  CFRelease(contiguous_buffer);
  return true;
}

bool H264AnnexBBufferToCMSampleBuffer(std::span<const uint8_t> annexb_buffer,
                                      CMVideoFormatDescriptionRef video_format,
                                      CMSampleBufferRef* out_sample_buffer,
                                      CMMemoryPoolRef memory_pool) {
  RTC_DCHECK(out_sample_buffer);
  RTC_DCHECK(video_format);
  *out_sample_buffer = nullptr;

  AnnexBBufferReader reader(annexb_buffer);
  if (reader.SeekToNextNaluOfType(kSps)) {
    // Buffer contains an SPS NALU - skip it and the following PPS
    std::span<const uint8_t> data;
    if (!reader.ReadNalu(data)) {
      RTC_LOG(LS_ERROR) << "Failed to read SPS";
      return false;
    }
    if (!reader.ReadNalu(data)) {
      RTC_LOG(LS_ERROR) << "Failed to read PPS";
      return false;
    }
  } else {
    // No SPS NALU - start reading from the first NALU in the buffer
    reader.SeekToStart();
  }

  // Allocate memory as a block buffer.
  CMBlockBufferRef block_buffer = nullptr;
  CFAllocatorRef block_allocator = CMMemoryPoolGetAllocator(memory_pool);
  OSStatus status = CMBlockBufferCreateWithMemoryBlock(
      kCFAllocatorDefault, nullptr, reader.BytesRemaining(), block_allocator,
      nullptr, 0, reader.BytesRemaining(), kCMBlockBufferAssureMemoryNowFlag,
      &block_buffer);
  if (status != kCMBlockBufferNoErr) {
    RTC_LOG(LS_ERROR) << "Failed to create block buffer.";
    return false;
  }

  // Make sure block buffer is contiguous.
  CMBlockBufferRef contiguous_buffer = nullptr;
  if (!CMBlockBufferIsRangeContiguous(block_buffer, 0, 0)) {
    status = CMBlockBufferCreateContiguous(kCFAllocatorDefault, block_buffer,
                                           block_allocator, nullptr, 0, 0, 0,
                                           &contiguous_buffer);
    if (status != noErr) {
      RTC_LOG(LS_ERROR) << "Failed to flatten non-contiguous block buffer: "
                        << status;
      CFRelease(block_buffer);
      return false;
    }
  } else {
    contiguous_buffer = block_buffer;
    block_buffer = nullptr;
  }

  // Get a raw pointer into allocated memory.
  size_t block_buffer_size = 0;
  char* data_ptr = nullptr;
  status = CMBlockBufferGetDataPointer(contiguous_buffer, 0, nullptr,
                                       &block_buffer_size, &data_ptr);
  if (status != kCMBlockBufferNoErr) {
    RTC_LOG(LS_ERROR) << "Failed to get block buffer data pointer.";
    CFRelease(contiguous_buffer);
    return false;
  }
  RTC_DCHECK(block_buffer_size == reader.BytesRemaining());

  // Write Avcc NALUs into block buffer memory.
  AvccBufferWriter writer(
      std::span(reinterpret_cast<uint8_t*>(data_ptr), block_buffer_size));
  while (reader.BytesRemaining() > 0) {
    std::span<const uint8_t> nalu_data;
    if (reader.ReadNalu(nalu_data)) {
      writer.WriteNalu(nalu_data);
    }
  }

  // Create sample buffer.
  status = CMSampleBufferCreate(kCFAllocatorDefault, contiguous_buffer, true,
                                nullptr, nullptr, video_format, 1, 0, nullptr,
                                0, nullptr, out_sample_buffer);
  if (status != noErr) {
    RTC_LOG(LS_ERROR) << "Failed to create sample buffer.";
    CFRelease(contiguous_buffer);
    return false;
  }
  CFRelease(contiguous_buffer);
  return true;
}

CMVideoFormatDescriptionRef CreateVideoFormatDescription(
    std::span<const uint8_t> annexb_buffer) {
  const uint8_t* param_set_ptrs[2] = {};
  size_t param_set_sizes[2] = {};
  AnnexBBufferReader reader(annexb_buffer);
  // Skip everyting before the SPS, then read the SPS and PPS
  if (!reader.SeekToNextNaluOfType(kSps)) {
    return nullptr;
  }
  std::span<const uint8_t> param_set;
  if (reader.ReadNalu(param_set)) {
    param_set_ptrs[0] = param_set.data();
    param_set_sizes[0] = param_set.size();
  } else {
    RTC_LOG(LS_ERROR) << "Failed to read SPS";
    return nullptr;
  }
  if (reader.ReadNalu(param_set)) {
    param_set_ptrs[1] = param_set.data();
    param_set_sizes[1] = param_set.size();
  } else {
    RTC_LOG(LS_ERROR) << "Failed to read PPS";
    return nullptr;
  }

  // Parse the SPS and PPS into a CMVideoFormatDescription.
  CMVideoFormatDescriptionRef description = nullptr;
  OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
      kCFAllocatorDefault, 2, param_set_ptrs, param_set_sizes, 4, &description);
  if (status != noErr) {
    RTC_LOG(LS_ERROR) << "Failed to create video format description.";
    return nullptr;
  }
  return description;
}

// RingRTC change to add support for H265
#if defined(RTC_ENABLE_H265)
namespace {

bool IsH265ParameterSet(H265::NaluType type) {
  return type == H265::kVps || type == H265::kSps || type == H265::kPps;
}

// Returns true if `nalu` is byte-identical to a parameter set in `format`.
bool IsInH265VideoFormat(CMVideoFormatDescriptionRef format,
                         std::span<const uint8_t> nalu) {
  size_t count = 0;
  if (CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
          format, 0, nullptr, nullptr, &count, nullptr) != noErr) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
            format, i, &data, &size, nullptr, nullptr) != noErr) {
      return false;
    }
    if (size == nalu.size() && memcmp(data, nalu.data(), size) == 0) {
      return true;
    }
  }
  return false;
}

// Matches the existing logic in H264
// Copies the 4-byte length-prefixed NALUs in `sample_buffer` into
// `annexb_buffer`, replacing each length prefix with an Annex B start code.
bool AppendLengthPrefixedNalusAsAnnexB(CMSampleBufferRef sample_buffer,
                                       webrtc::Buffer* annexb_buffer) {
  // Get block buffer from the sample buffer.
  CMBlockBufferRef block_buffer = CMSampleBufferGetDataBuffer(sample_buffer);
  if (block_buffer == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to get sample buffer's block buffer.";
    return false;
  }
  CMBlockBufferRef contiguous_buffer = nullptr;
  OSStatus status = noErr;
  // Make sure block buffer is contiguous.
  if (!CMBlockBufferIsRangeContiguous(block_buffer, 0, 0)) {
    status = CMBlockBufferCreateContiguous(
        nullptr, block_buffer, nullptr, nullptr, 0, 0, 0, &contiguous_buffer);
    if (status != noErr) {
      RTC_LOG(LS_ERROR) << "Failed to flatten non-contiguous block buffer: "
                        << status;
      return false;
    }
  } else {
    contiguous_buffer = block_buffer;
    // Retain to make cleanup easier.
    CFRetain(contiguous_buffer);
    block_buffer = nullptr;
  }

  // Now copy the actual data.
  char* data_ptr = nullptr;
  size_t bytes_remaining = CMBlockBufferGetDataLength(contiguous_buffer);
  status = CMBlockBufferGetDataPointer(contiguous_buffer, 0, nullptr, nullptr,
                                       &data_ptr);
  if (status != noErr) {
    RTC_LOG(LS_ERROR) << "Failed to get block buffer data.";
    CFRelease(contiguous_buffer);
    return false;
  }
  while (bytes_remaining > 0) {
    // The size type here must match `nalu_header_size`, we expect 4 bytes.
    // Read the length of the next packet of data. Must convert from big endian
    // to host endian.
    if (bytes_remaining < kAvccHeaderByteSize) {
      RTC_LOG(LS_ERROR) << "Truncated HEVC NALU length prefix.";
      CFRelease(contiguous_buffer);
      return false;
    }
    uint32_t packet_size = 0;
    memcpy(&packet_size, data_ptr, sizeof(packet_size));
    packet_size = CFSwapInt32BigToHost(packet_size);
    if (packet_size > bytes_remaining - kAvccHeaderByteSize) {
      RTC_LOG(LS_ERROR) << "HEVC NALU length exceeds sample buffer size.";
      CFRelease(contiguous_buffer);
      return false;
    }
    annexb_buffer->AppendData(kAnnexBHeaderBytes, sizeof(kAnnexBHeaderBytes));
    annexb_buffer->AppendData(data_ptr + kAvccHeaderByteSize, packet_size);

    size_t bytes_read = packet_size + kAvccHeaderByteSize;
    bytes_remaining -= bytes_read;
    data_ptr += bytes_read;
  }
  CFRelease(contiguous_buffer);
  return true;
}

}  // namespace

bool H265CMSampleBufferToAnnexBBuffer(CMSampleBufferRef hvcc_sample_buffer,
                                      bool is_keyframe,
                                      webrtc::Buffer* annexb_buffer) {
  RTC_DCHECK(hvcc_sample_buffer);

  CMVideoFormatDescriptionRef description =
      CMSampleBufferGetFormatDescription(hvcc_sample_buffer);
  if (description == nullptr) {
    RTC_LOG(LS_ERROR) << "Failed to get sample buffer's description.";
    return false;
  }

  int nalu_header_size = 0;
  size_t param_set_count = 0;
  OSStatus status = CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
      description, 0, nullptr, nullptr, &param_set_count, &nalu_header_size);
  if (status != noErr) {
    RTC_LOG(LS_ERROR) << "Failed to get HEVC parameter set count.";
    return false;
  }
  // we use kAvccHeaderByteSize because we reuse AvccBufferWriter which uses
  // this same constant
  if (nalu_header_size != static_cast<int>(kAvccHeaderByteSize)) {
    RTC_LOG(LS_ERROR) << "Unexpected HEVC NALU length size: "
                      << nalu_header_size;
    return false;
  }

  // Truncate any previous data in the buffer without changing its capacity.
  annexb_buffer->SetSize(0);

  // Place all parameter sets at the front of buffer.
  if (is_keyframe) {
    size_t param_set_size = 0;
    const uint8_t* param_set = nullptr;
    for (size_t i = 0; i < param_set_count; ++i) {
      status = CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
          description, i, &param_set, &param_set_size, nullptr, nullptr);
      if (status != noErr) {
        RTC_LOG(LS_ERROR) << "Failed to get HEVC parameter set " << i;
        return false;
      }
      // Update buffer.
      annexb_buffer->AppendData(kAnnexBHeaderBytes, sizeof(kAnnexBHeaderBytes));
      annexb_buffer->AppendData(reinterpret_cast<const char*>(param_set),
                                param_set_size);
    }
  }

  return AppendLengthPrefixedNalusAsAnnexB(hvcc_sample_buffer, annexb_buffer);
}

bool H265AnnexBBufferToCMSampleBuffer(std::span<const uint8_t> annexb_buffer,
                                      CMVideoFormatDescriptionRef video_format,
                                      CMSampleBufferRef* out_sample_buffer,
                                      CMMemoryPoolRef memory_pool) {
  RTC_DCHECK(out_sample_buffer);
  RTC_DCHECK(video_format);
  *out_sample_buffer = nullptr;

  std::vector<std::span<const uint8_t>> nalus;
  bool has_frame_data = false;
  bool has_new_parameter_set = false;
  for (const H265::NaluIndex& index : H265::FindNaluIndices(annexb_buffer)) {
    if (index.payload_size < H265::kNaluHeaderSize) {
      continue;
    }
    std::span<const uint8_t> nalu =
        annexb_buffer.subspan(index.payload_start_offset, index.payload_size);
    if (IsH265ParameterSet(H265::ParseNaluType(nalu[0]))) {
      if (!IsInH265VideoFormat(video_format, nalu)) {
        has_new_parameter_set = true;
      }
    } else {
      has_frame_data = true;
    }
    nalus.push_back(nalu);
  }
  // VideoToolbox will error if there is no slice data. Pre-empt it one frame
  // early here with a more specific log message
  if (!has_frame_data) {
    RTC_LOG(LS_ERROR) << "No HEVC slice data in buffer.";
    return false;
  }

  // Drop the parameter sets if the existing `video_format` already has every
  // single one. If there is any drift, we must, keep all of them in order so
  // that VideoToolbox applies them the same order.
  if (!has_new_parameter_set) {
    std::erase_if(nalus, [](std::span<const uint8_t> nalu) {
      return IsH265ParameterSet(H265::ParseNaluType(nalu[0]));
    });
  }
  size_t hvcc_size = 0;
  for (std::span<const uint8_t> nalu : nalus) {
    hvcc_size += kAvccHeaderByteSize + nalu.size();
  }

  // Allocate memory as a block buffer.
  CMBlockBufferRef block_buffer = nullptr;
  CFAllocatorRef block_allocator = CMMemoryPoolGetAllocator(memory_pool);
  OSStatus status = CMBlockBufferCreateWithMemoryBlock(
      kCFAllocatorDefault, nullptr, hvcc_size, block_allocator,
      nullptr, 0, hvcc_size, kCMBlockBufferAssureMemoryNowFlag,
      &block_buffer);
  if (status != kCMBlockBufferNoErr) {
    RTC_LOG(LS_ERROR) << "Failed to create block buffer.";
    return false;
  }

  // Make sure block buffer is contiguous.
  CMBlockBufferRef contiguous_buffer = nullptr;
  if (!CMBlockBufferIsRangeContiguous(block_buffer, 0, 0)) {
    status = CMBlockBufferCreateContiguous(kCFAllocatorDefault, block_buffer,
                                           block_allocator, nullptr, 0, 0, 0,
                                           &contiguous_buffer);
    CFRelease(block_buffer);
    if (status != noErr) {
      RTC_LOG(LS_ERROR) << "Failed to flatten non-contiguous block buffer: "
                        << status;
      return false;
    }
  } else {
    contiguous_buffer = block_buffer;
    block_buffer = nullptr;
  }

  // Get a raw pointer into allocated memory.
  size_t block_buffer_size = 0;
  char* data_ptr = nullptr;
  status = CMBlockBufferGetDataPointer(contiguous_buffer, 0, nullptr,
                                       &block_buffer_size, &data_ptr);
  if (status != kCMBlockBufferNoErr) {
    RTC_LOG(LS_ERROR) << "Failed to get block buffer data pointer.";
    CFRelease(contiguous_buffer);
    return false;
  }
  RTC_DCHECK_EQ(block_buffer_size, hvcc_size);

  AvccBufferWriter writer(
      std::span(reinterpret_cast<uint8_t*>(data_ptr), block_buffer_size));
  for (std::span<const uint8_t> nalu : nalus) {
    if (!writer.WriteNalu(nalu)) {
      RTC_LOG(LS_ERROR) << "Failed to write HEVC NALU into sample buffer.";
      CFRelease(contiguous_buffer);
      return false;
    }
  }

  // Create sample buffer.
  status = CMSampleBufferCreate(kCFAllocatorDefault, contiguous_buffer, true,
                                nullptr, nullptr, video_format, 1, 0, nullptr,
                                0, nullptr, out_sample_buffer);
  CFRelease(contiguous_buffer);
  if (status != noErr) {
    RTC_LOG(LS_ERROR) << "Failed to create sample buffer.";
    return false;
  }
  return true;
}

CMVideoFormatDescriptionRef CreateH265VideoFormatDescription(
    std::span<const uint8_t> annexb_buffer) {
  // Keep every parameter set, grouped by type, so a sender that uses several
  // SPS or PPS ids gets all of them in the format. When two have the same id,
  // VideoToolbox keeps the last one; the one the decoder actually uses.
  std::vector<std::span<const uint8_t>> vps_list;
  std::vector<std::span<const uint8_t>> sps_list;
  std::vector<std::span<const uint8_t>> pps_list;
  for (const H265::NaluIndex& index : H265::FindNaluIndices(annexb_buffer)) {
    if (index.payload_size < H265::kNaluHeaderSize) {
      continue;
    }
    std::span<const uint8_t> nalu =
        annexb_buffer.subspan(index.payload_start_offset, index.payload_size);
    switch (H265::ParseNaluType(nalu[0])) {
      case H265::kVps:
        vps_list.push_back(nalu);
        break;
      case H265::kSps:
        sps_list.push_back(nalu);
        break;
      case H265::kPps:
        pps_list.push_back(nalu);
        break;
      default:
        break;
    }
  }
  // fail if any parameter set is missing
  if (vps_list.empty() || sps_list.empty() || pps_list.empty()) {
    return nullptr;
  }

  std::vector<const uint8_t*> param_set_ptrs;
  std::vector<size_t> param_set_sizes;
  for (const auto* list : {&vps_list, &sps_list, &pps_list}) {
    for (std::span<const uint8_t> param_set : *list) {
      param_set_ptrs.push_back(param_set.data());
      param_set_sizes.push_back(param_set.size());
    }
  }
  // Parse the VPS, SPS, and PPS into a CMVideoFormatDescription.
  CMVideoFormatDescriptionRef description = nullptr;
  OSStatus status = CMVideoFormatDescriptionCreateFromHEVCParameterSets(
      kCFAllocatorDefault, param_set_ptrs.size(), param_set_ptrs.data(),
      param_set_sizes.data(), kAvccHeaderByteSize, nullptr, &description);
  if (status != noErr) {
    RTC_LOG(LS_ERROR) << "Failed to create HEVC video format description: "
                      << status;
    return nullptr;
  }
  return description;
}
#endif  // defined(RTC_ENABLE_H265)
// end RingRTC change to add support for H265

AnnexBBufferReader::AnnexBBufferReader(std::span<const uint8_t> annexb_buffer)
    : buffer_(annexb_buffer) {
  offsets_ = H264::FindNaluIndices(annexb_buffer);
  offset_ = offsets_.begin();
}

AnnexBBufferReader::~AnnexBBufferReader() = default;

bool AnnexBBufferReader::ReadNalu(std::span<const uint8_t>& out_nalu) {
  if (offset_ == offsets_.end()) {
    return false;
  }
  out_nalu =
      buffer_.subspan(offset_->payload_start_offset, offset_->payload_size);
  ++offset_;
  return true;
}

size_t AnnexBBufferReader::BytesRemaining() const {
  if (offset_ == offsets_.end()) {
    return 0;
  }
  return buffer_.size() - offset_->start_offset;
}

void AnnexBBufferReader::SeekToStart() {
  offset_ = offsets_.begin();
}

bool AnnexBBufferReader::SeekToNextNaluOfType(NaluType type) {
  for (; offset_ != offsets_.end(); ++offset_) {
    if (offset_->payload_size < 1)
      continue;
    if (ParseNaluType(buffer_[offset_->payload_start_offset]) == type)
      return true;
  }
  return false;
}

AvccBufferWriter::AvccBufferWriter(std::span<uint8_t> avcc_buffer)
    : buffer_(avcc_buffer) {}

bool AvccBufferWriter::WriteNalu(std::span<const uint8_t> data) {
  // Check if we can write this length of data.
  if (data.size() + kAvccHeaderByteSize > BytesRemaining()) {
    return false;
  }
  // Write length header, which needs to be big endian.
  uint32_t big_endian_length = CFSwapInt32HostToBig(data.size());
  memcpy(buffer_.data(), &big_endian_length, sizeof(big_endian_length));
  buffer_ = buffer_.subspan(sizeof(big_endian_length));
  // Write data.
  memcpy(buffer_.data(), data.data(), data.size());
  buffer_ = buffer_.subspan(data.size());
  return true;
}

size_t AvccBufferWriter::BytesRemaining() const {
  return buffer_.size();
}

}  // namespace webrtc
