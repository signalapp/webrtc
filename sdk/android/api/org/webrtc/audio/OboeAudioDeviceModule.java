/*
 * Copyright 2024 Signal Messenger, LLC
 * SPDX-License-Identifier: AGPL-3.0-only
 */

package org.webrtc.audio;

import org.webrtc.JniCommon;
import org.webrtc.Logging;

/**
 * AudioDeviceModule implemented using Oboe.
 */
public class OboeAudioDeviceModule implements AudioDeviceModule {
  private static final String TAG = "OboeAudioDeviceModule";

  public static Builder builder() {
    return new Builder();
  }

  public static class Builder {
    // By default, hardware AEC and NS will be used. These are *usually* available to
    // streams by configuration for VOICE_COMMUNICATION usage. The Client will force
    // WebRTC's software AEC3 and NS on a case-by-case basis.
    private boolean useSoftwareAcousticEchoCanceler;
    private boolean useSoftwareNoiseSuppressor;
    private boolean useExclusiveSharingMode;
    private boolean useInputLowLatency;
    private boolean useInputVoiceCommPreset;

    private Builder() {
      Logging.w(TAG, "Builder()");
      this.useSoftwareAcousticEchoCanceler = false;
      this.useSoftwareNoiseSuppressor = false;
      this.useExclusiveSharingMode = true;
      this.useInputLowLatency = true;
      this.useInputVoiceCommPreset = true;
    }

    /**
     * Control if the software noise suppressor should be used or not.
     */
    public Builder setUseSoftwareNoiseSuppressor(boolean useSoftwareNoiseSuppressor) {
      Logging.w(TAG, "setUseSoftwareNoiseSuppressor: " + useSoftwareNoiseSuppressor);
      this.useSoftwareNoiseSuppressor = useSoftwareNoiseSuppressor;
      return this;
    }

    /**
     * Control if the software acoustic echo canceler should be used or not.
     */
    public Builder setUseSoftwareAcousticEchoCanceler(boolean useSoftwareAcousticEchoCanceler) {
      Logging.w(TAG, "setUseSoftwareAcousticEchoCanceler: " + useSoftwareAcousticEchoCanceler);
      this.useSoftwareAcousticEchoCanceler = useSoftwareAcousticEchoCanceler;
      return this;
    }

    /**
     * Control if the streams should be opened with exclusive sharing mode enabled or not. The
     * default is on, but if exclusive mode can't be obtained, then shared mode will be used.
     */
    public Builder setExclusiveSharingMode(boolean useExclusiveSharingMode) {
      Logging.w(TAG, "setExclusiveSharingMode: " + useExclusiveSharingMode);
      this.useExclusiveSharingMode = useExclusiveSharingMode;
      return this;
    }

    /**
     * Control if the input performance mode should use Low Latency setting or not.
     */
    public Builder setInputLowLatency(boolean useInputLowLatency) {
      Logging.w(TAG, "setInputLowLatency: " + useInputLowLatency);
      this.useInputLowLatency = useInputLowLatency;
      return this;
    }

    /**
     * Control if the input preset should use voice communications setting or not. If not, the
     * voice recognition setting will be used.
     */
    public Builder setInputVoiceCommPreset(boolean useInputVoiceCommPreset) {
      Logging.w(TAG, "setInputVoiceCommPreset: " + useInputVoiceCommPreset);
      this.useInputVoiceCommPreset = useInputVoiceCommPreset;
      return this;
    }

    /**
     * Construct an AudioDeviceModule based on the supplied arguments. The caller takes ownership
     * and is responsible for calling release().
     */
    public OboeAudioDeviceModule createAudioDeviceModule() {
      Logging.w(TAG, "createAudioDeviceModule");
      return new OboeAudioDeviceModule(
          useSoftwareAcousticEchoCanceler,
          useSoftwareNoiseSuppressor,
          useExclusiveSharingMode,
          useInputLowLatency,
          useInputVoiceCommPreset);
    }
  }

  private boolean useSoftwareAcousticEchoCanceler;
  private boolean useSoftwareNoiseSuppressor;
  private boolean useExclusiveSharingMode;
  private boolean useInputLowLatency;
  private boolean useInputVoiceCommPreset;

  private final Object nativeLock = new Object();
  private long nativeAudioDeviceModule;

  private OboeAudioDeviceModule(
      boolean useSoftwareAcousticEchoCanceler,
      boolean useSoftwareNoiseSuppressor,
      boolean useExclusiveSharingMode,
      boolean useInputLowLatency,
      boolean useInputVoiceCommPreset) {
    Logging.w(TAG, "OboeAudioDeviceModule");
    this.useSoftwareAcousticEchoCanceler = useSoftwareAcousticEchoCanceler;
    this.useSoftwareNoiseSuppressor = useSoftwareNoiseSuppressor;
    this.useExclusiveSharingMode = useExclusiveSharingMode;
    this.useInputLowLatency = useInputLowLatency;
    this.useInputVoiceCommPreset = useInputVoiceCommPreset;
  }

  @Override
  public long getNative(long webrtcEnvRef) {
    Logging.w(TAG, "getNative");
    synchronized (nativeLock) {
      if (nativeAudioDeviceModule == 0) {
        Logging.w(TAG, "calling nativeCreateAudioDeviceModule");
        nativeAudioDeviceModule = nativeCreateAudioDeviceModule(
            useSoftwareAcousticEchoCanceler,
            useSoftwareNoiseSuppressor,
            useExclusiveSharingMode,
            useInputLowLatency,
            useInputVoiceCommPreset);
      }
      return nativeAudioDeviceModule;
    }
  }

  @Override
  public void release() {
    Logging.w(TAG, "release");
    synchronized (nativeLock) {
      if (nativeAudioDeviceModule != 0) {
        JniCommon.nativeReleaseRef(nativeAudioDeviceModule);
        nativeAudioDeviceModule = 0;
      }
    }
  }

  @Override
  public void setSpeakerMute(boolean mute) {
    Logging.e(TAG, "Not supported; setSpeakerMute: " + mute);
  }

  @Override
  public void setMicrophoneMute(boolean mute) {
    Logging.e(TAG, "Not supported; setMicrophoneMute: " + mute);
  }

  @Override
  public boolean setNoiseSuppressorEnabled(boolean enabled) {
    Logging.e(TAG, "Not supported; setNoiseSuppressorEnabled: " + enabled);
    return false;
  }

  private static native long nativeCreateAudioDeviceModule(
      boolean useSoftwareAcousticEchoCanceler,
      boolean useSoftwareNoiseSuppressor,
      boolean useExclusiveSharingMode,
      boolean useInputLowLatency,
      boolean useInputVoiceCommPreset);
}
