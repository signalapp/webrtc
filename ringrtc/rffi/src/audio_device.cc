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

RingRTCAudioDeviceModule::RingRTCAudioDeviceModule() = default;
RingRTCAudioDeviceModule::~RingRTCAudioDeviceModule() = default;

// static
rtc::scoped_refptr<AudioDeviceModule> RingRTCAudioDeviceModule::Create() {
  TRACE_LOG;
  return rtc::make_ref_counted<RingRTCAudioDeviceModule>();
}

int32_t RingRTCAudioDeviceModule::ActiveAudioLayer(
    AudioLayer* audioLayer) const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::RegisterAudioCallback(
    AudioTransport* audioCallback) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::Init() {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::Terminate() {
  TRACE_LOG;
  return -1;
}

bool RingRTCAudioDeviceModule::Initialized() const {
  TRACE_LOG;
  return false;
}

int16_t RingRTCAudioDeviceModule::PlayoutDevices() {
  TRACE_LOG;
  return -1;
}

int16_t RingRTCAudioDeviceModule::RecordingDevices() {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::PlayoutDeviceName(
    uint16_t index,
    char name[kAdmMaxDeviceNameSize],
    char guid[kAdmMaxGuidSize]) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::RecordingDeviceName(
    uint16_t index,
    char name[kAdmMaxDeviceNameSize],
    char guid[kAdmMaxGuidSize]) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::SetPlayoutDevice(uint16_t index) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::SetPlayoutDevice(WindowsDeviceType device) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::SetRecordingDevice(uint16_t index) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::SetRecordingDevice(WindowsDeviceType device) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::PlayoutIsAvailable(bool* available) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::InitPlayout() {
  TRACE_LOG;
  return -1;
}

bool RingRTCAudioDeviceModule::PlayoutIsInitialized() const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::RecordingIsAvailable(bool* available) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::InitRecording() {
  TRACE_LOG;
  return -1;
}

bool RingRTCAudioDeviceModule::RecordingIsInitialized() const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::StartPlayout() {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::StopPlayout() {
  TRACE_LOG;
  return -1;
}

bool RingRTCAudioDeviceModule::Playing() const {
  TRACE_LOG;
  return false;
}

int32_t RingRTCAudioDeviceModule::StartRecording() {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::StopRecording() {
  TRACE_LOG;
  return -1;
}

bool RingRTCAudioDeviceModule::Recording() const {
  TRACE_LOG;
  return false;
}

int32_t RingRTCAudioDeviceModule::InitSpeaker() {
  TRACE_LOG;
  return -1;
}

bool RingRTCAudioDeviceModule::SpeakerIsInitialized() const {
  TRACE_LOG;
  return false;
}

int32_t RingRTCAudioDeviceModule::InitMicrophone() {
  TRACE_LOG;
  return -1;
}

bool RingRTCAudioDeviceModule::MicrophoneIsInitialized() const {
  TRACE_LOG;
  return false;
}

int32_t RingRTCAudioDeviceModule::SpeakerVolumeIsAvailable(bool* available) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::SetSpeakerVolume(uint32_t volume) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::SpeakerVolume(uint32_t* volume) const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::MaxSpeakerVolume(uint32_t* maxVolume) const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::MinSpeakerVolume(uint32_t* minVolume) const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::MicrophoneVolumeIsAvailable(bool* available) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::SetMicrophoneVolume(uint32_t volume) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::MicrophoneVolume(uint32_t* volume) const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::MaxMicrophoneVolume(
    uint32_t* maxVolume) const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::MinMicrophoneVolume(
    uint32_t* minVolume) const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::SpeakerMuteIsAvailable(bool* available) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::SetSpeakerMute(bool enable) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::SpeakerMute(bool* enabled) const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::MicrophoneMuteIsAvailable(bool* available) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::SetMicrophoneMute(bool enable) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::MicrophoneMute(bool* enabled) const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::StereoPlayoutIsAvailable(
    bool* available) const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::SetStereoPlayout(bool enable) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::StereoPlayout(bool* enabled) const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::StereoRecordingIsAvailable(
    bool* available) const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::SetStereoRecording(bool enable) {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::StereoRecording(bool* enabled) const {
  TRACE_LOG;
  return -1;
}

int32_t RingRTCAudioDeviceModule::PlayoutDelay(uint16_t* delayMS) const {
  TRACE_LOG;
  return -1;
}

}  // namespace rffi
}  // namespace webrtc
