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
    private int audioSessionId;

    private Builder() {
      Logging.w(TAG, "Builder()");
      this.useSoftwareAcousticEchoCanceler = false;
      this.useSoftwareNoiseSuppressor = false;
      this.useExclusiveSharingMode = true;
      this.audioSessionId = -1;
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
     * Provide an audio session ID generated from application's AudioManager.
     */
    public Builder setAudioSessionId(int audioSessionId) {
      Logging.w(TAG, "setAudioSessionId: " + audioSessionId);
      this.audioSessionId = audioSessionId;
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
          audioSessionId);
    }
  }

  private boolean useSoftwareAcousticEchoCanceler;
  private boolean useSoftwareNoiseSuppressor;
  private boolean useExclusiveSharingMode;
  private int audioSessionId;

  private final Object nativeLock = new Object();
  private long nativeAudioDeviceModule;

  private OboeAudioDeviceModule(
      boolean useSoftwareAcousticEchoCanceler,
      boolean useSoftwareNoiseSuppressor,
      boolean useExclusiveSharingMode,
      int audioSessionId) {
    Logging.w(TAG, "OboeAudioDeviceModule");
    this.useSoftwareAcousticEchoCanceler = useSoftwareAcousticEchoCanceler;
    this.useSoftwareNoiseSuppressor = useSoftwareNoiseSuppressor;
    this.useExclusiveSharingMode = useExclusiveSharingMode;
    this.audioSessionId = audioSessionId;
  }

  @Override
  public long getNativeAudioDeviceModulePointer() {
    Logging.w(TAG, "getNativeAudioDeviceModulePointer");
    synchronized (nativeLock) {
      if (nativeAudioDeviceModule == 0) {
        Logging.w(TAG, "calling nativeCreateAudioDeviceModule");
        nativeAudioDeviceModule = nativeCreateAudioDeviceModule(
            useSoftwareAcousticEchoCanceler,
            useSoftwareNoiseSuppressor,
            useExclusiveSharingMode,
            audioSessionId);
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
      int audioSessionId);
}
