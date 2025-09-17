/*
 * Copyright 2024 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "rffi/src/audio_device.h"

#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "rtc_base/logging.h"

#define TRACE_LOG \
  RTC_LOG(LS_VERBOSE) << "RingRTCAudioDeviceModule::" << __func__

namespace webrtc {
namespace rffi {

RUSTEXPORT int32_t
Rust_recordedDataIsAvailable(AudioTransport* audio_callback,
                             const void* audio_samples,
                             size_t n_samples,
                             size_t n_bytes_per_sample,
                             size_t n_channels,
                             uint32_t samples_per_sec,
                             uint32_t total_delay_ms,
                             int32_t clock_drift,
                             uint32_t current_mic_level,
                             bool key_pressed,
                             uint32_t* new_mic_level,
                             int64_t estimated_capture_time_ns) {
  if (!audio_callback) {
    return -1;
  }
  std::optional<int64_t> estimated_capture_time_ns_opt;
  if (estimated_capture_time_ns >= 0) {
    estimated_capture_time_ns_opt = estimated_capture_time_ns;
  }
  return audio_callback->RecordedDataIsAvailable(
      audio_samples, n_samples, n_bytes_per_sample, n_channels, samples_per_sec,
      total_delay_ms, clock_drift, current_mic_level, key_pressed,
      *new_mic_level, estimated_capture_time_ns_opt);
}

RUSTEXPORT int32_t Rust_needMorePlayData(AudioTransport* audio_callback,
                                         size_t n_samples,
                                         size_t n_bytes_per_sample,
                                         size_t n_channels,
                                         uint32_t samples_per_sec,
                                         void* audio_samples,
                                         size_t* n_samples_out,
                                         int64_t* elapsed_time_ms,
                                         int64_t* ntp_time_ms) {
  if (!audio_callback) {
    return -1;
  }
  return audio_callback->NeedMorePlayData(
      n_samples, n_bytes_per_sample, n_channels, samples_per_sec, audio_samples,
      *n_samples_out, elapsed_time_ms, ntp_time_ms);
}

RingRTCAudioDeviceModule::RingRTCAudioDeviceModule(
    void* adm_borrowed,
    const AudioDeviceCallbacks* callbacks)
    : adm_borrowed_(adm_borrowed), rust_callbacks_(*callbacks) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
}

RingRTCAudioDeviceModule::~RingRTCAudioDeviceModule() {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  Terminate();
}

// static
scoped_refptr<RingRTCAudioDeviceModule> RingRTCAudioDeviceModule::Create(
    void* adm_borrowed,
    const AudioDeviceCallbacks* callbacks) {
  TRACE_LOG;
  RTC_DCHECK(adm_borrowed);
  RTC_DCHECK(callbacks);
  return make_ref_counted<RingRTCAudioDeviceModule>(adm_borrowed, callbacks);
}

int32_t RingRTCAudioDeviceModule::ActiveAudioLayer(
    AudioLayer* audio_layer) const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(audio_layer);
  return rust_callbacks_.activeAudioLayer(adm_borrowed_, audio_layer);
}

int32_t RingRTCAudioDeviceModule::RegisterAudioCallback(
    AudioTransport* audio_callback) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.registerAudioCallback(adm_borrowed_, audio_callback);
}

int32_t RingRTCAudioDeviceModule::Init() {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.init(adm_borrowed_);
}

int32_t RingRTCAudioDeviceModule::Terminate() {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.terminate(adm_borrowed_);
}

bool RingRTCAudioDeviceModule::Initialized() const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.initialized(adm_borrowed_);
}

int16_t RingRTCAudioDeviceModule::PlayoutDevices() {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.playoutDevices(adm_borrowed_);
}

int16_t RingRTCAudioDeviceModule::RecordingDevices() {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.recordingDevices(adm_borrowed_);
}

int32_t RingRTCAudioDeviceModule::PlayoutDeviceName(
    uint16_t index,
    char name[kAdmMaxDeviceNameSize],
    char guid[kAdmMaxGuidSize]) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.playoutDeviceName(adm_borrowed_, index, name, guid);
}

int32_t RingRTCAudioDeviceModule::RecordingDeviceName(
    uint16_t index,
    char name[kAdmMaxDeviceNameSize],
    char guid[kAdmMaxGuidSize]) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.recordingDeviceName(adm_borrowed_, index, name, guid);
}

int32_t RingRTCAudioDeviceModule::SetPlayoutDevice(uint16_t index) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.setPlayoutDevice(adm_borrowed_, index);
}

int32_t RingRTCAudioDeviceModule::SetPlayoutDevice(WindowsDeviceType device) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.setPlayoutDeviceWin(adm_borrowed_, device);
}

int32_t RingRTCAudioDeviceModule::SetRecordingDevice(uint16_t index) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.setRecordingDevice(adm_borrowed_, index);
}

int32_t RingRTCAudioDeviceModule::SetRecordingDevice(WindowsDeviceType device) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.setRecordingDeviceWin(adm_borrowed_, device);
}

int32_t RingRTCAudioDeviceModule::PlayoutIsAvailable(bool* available) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(available);
  return rust_callbacks_.playoutIsAvailable(adm_borrowed_, available);
}

int32_t RingRTCAudioDeviceModule::InitPlayout() {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.initPlayout(adm_borrowed_);
}

bool RingRTCAudioDeviceModule::PlayoutIsInitialized() const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.playoutIsInitialized(adm_borrowed_);
}

int32_t RingRTCAudioDeviceModule::RecordingIsAvailable(bool* available) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(available);
  return rust_callbacks_.recordingIsAvailable(adm_borrowed_, available);
}

int32_t RingRTCAudioDeviceModule::InitRecording() {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.initRecording(adm_borrowed_);
}

bool RingRTCAudioDeviceModule::RecordingIsInitialized() const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.recordingIsInitialized(adm_borrowed_);
}

int32_t RingRTCAudioDeviceModule::StartPlayout() {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.startPlayout(adm_borrowed_);
}

int32_t RingRTCAudioDeviceModule::StopPlayout() {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.stopPlayout(adm_borrowed_);
}

bool RingRTCAudioDeviceModule::Playing() const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.playing(adm_borrowed_);
}

int32_t RingRTCAudioDeviceModule::StartRecording() {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.startRecording(adm_borrowed_);
}

int32_t RingRTCAudioDeviceModule::StopRecording() {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.stopRecording(adm_borrowed_);
}

bool RingRTCAudioDeviceModule::Recording() const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.recording(adm_borrowed_);
}

int32_t RingRTCAudioDeviceModule::InitSpeaker() {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.initSpeaker(adm_borrowed_);
}

bool RingRTCAudioDeviceModule::SpeakerIsInitialized() const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.speakerIsInitialized(adm_borrowed_);
}

int32_t RingRTCAudioDeviceModule::InitMicrophone() {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.initMicrophone(adm_borrowed_);
}

bool RingRTCAudioDeviceModule::MicrophoneIsInitialized() const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.microphoneIsInitialized(adm_borrowed_);
}

int32_t RingRTCAudioDeviceModule::SpeakerVolumeIsAvailable(bool* available) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(available);
  return rust_callbacks_.speakerVolumeIsAvailable(adm_borrowed_, available);
}

int32_t RingRTCAudioDeviceModule::SetSpeakerVolume(uint32_t volume) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.setSpeakerVolume(adm_borrowed_, volume);
}

int32_t RingRTCAudioDeviceModule::SpeakerVolume(uint32_t* volume) const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(volume);
  return rust_callbacks_.speakerVolume(adm_borrowed_, volume);
}

int32_t RingRTCAudioDeviceModule::MaxSpeakerVolume(uint32_t* max_volume) const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(max_volume);
  return rust_callbacks_.maxSpeakerVolume(adm_borrowed_, max_volume);
}

int32_t RingRTCAudioDeviceModule::MinSpeakerVolume(uint32_t* min_volume) const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(min_volume);
  return rust_callbacks_.minSpeakerVolume(adm_borrowed_, min_volume);
}

int32_t RingRTCAudioDeviceModule::MicrophoneVolumeIsAvailable(bool* available) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(available);
  return rust_callbacks_.microphoneVolumeIsAvailable(adm_borrowed_, available);
}

int32_t RingRTCAudioDeviceModule::SetMicrophoneVolume(uint32_t volume) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.setMicrophoneVolume(adm_borrowed_, volume);
}

int32_t RingRTCAudioDeviceModule::MicrophoneVolume(uint32_t* volume) const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(volume);
  return rust_callbacks_.microphoneVolume(adm_borrowed_, volume);
}

int32_t RingRTCAudioDeviceModule::MaxMicrophoneVolume(
    uint32_t* max_volume) const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(max_volume);
  return rust_callbacks_.maxMicrophoneVolume(adm_borrowed_, max_volume);
}

int32_t RingRTCAudioDeviceModule::MinMicrophoneVolume(
    uint32_t* min_volume) const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(min_volume);
  return rust_callbacks_.minMicrophoneVolume(adm_borrowed_, min_volume);
}

int32_t RingRTCAudioDeviceModule::SpeakerMuteIsAvailable(bool* available) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(available);
  return rust_callbacks_.speakerMuteIsAvailable(adm_borrowed_, available);
}

int32_t RingRTCAudioDeviceModule::SetSpeakerMute(bool enable) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.setSpeakerMute(adm_borrowed_, enable);
}

int32_t RingRTCAudioDeviceModule::SpeakerMute(bool* enabled) const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(enabled);
  return rust_callbacks_.speakerMute(adm_borrowed_, enabled);
}

int32_t RingRTCAudioDeviceModule::MicrophoneMuteIsAvailable(bool* available) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(available);
  return rust_callbacks_.microphoneMuteIsAvailable(adm_borrowed_, available);
}

int32_t RingRTCAudioDeviceModule::SetMicrophoneMute(bool enable) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.setMicrophoneMute(adm_borrowed_, enable);
}

int32_t RingRTCAudioDeviceModule::MicrophoneMute(bool* enabled) const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(enabled);
  return rust_callbacks_.microphoneMute(adm_borrowed_, enabled);
}

int32_t RingRTCAudioDeviceModule::StereoPlayoutIsAvailable(
    bool* available) const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(available);
  return rust_callbacks_.stereoPlayoutIsAvailable(adm_borrowed_, available);
}

int32_t RingRTCAudioDeviceModule::SetStereoPlayout(bool enable) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.setStereoPlayout(adm_borrowed_, enable);
}

int32_t RingRTCAudioDeviceModule::StereoPlayout(bool* enabled) const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(enabled);
  return rust_callbacks_.stereoPlayout(adm_borrowed_, enabled);
}

int32_t RingRTCAudioDeviceModule::StereoRecordingIsAvailable(
    bool* available) const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(available);
  return rust_callbacks_.stereoRecordingIsAvailable(adm_borrowed_, available);
}

int32_t RingRTCAudioDeviceModule::SetStereoRecording(bool enable) {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rust_callbacks_.setStereoRecording(adm_borrowed_, enable);
}

int32_t RingRTCAudioDeviceModule::StereoRecording(bool* enabled) const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(enabled);
  return rust_callbacks_.stereoRecording(adm_borrowed_, enabled);
}

int32_t RingRTCAudioDeviceModule::PlayoutDelay(uint16_t* delayMS) const {
  TRACE_LOG;
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(delayMS);
  return rust_callbacks_.playoutDelay(adm_borrowed_, delayMS);
}

}  // namespace rffi
}  // namespace webrtc
