/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc;

import androidx.annotation.Nullable;
import java.util.Arrays;
import java.util.Collections;
import java.util.Locale;
import java.util.List;
import java.util.Map;

/**
 * Represent a video codec as encoded in SDP.
 */
public class VideoCodecInfo {
  // Keys for H264 VideoCodecInfo properties.
  public static final String H264_FMTP_PROFILE_LEVEL_ID = "profile-level-id";
  public static final String H264_FMTP_LEVEL_ASYMMETRY_ALLOWED = "level-asymmetry-allowed";
  public static final String H264_FMTP_PACKETIZATION_MODE = "packetization-mode";

  public static final String H264_PROFILE_CONSTRAINED_BASELINE = "42e0";
  public static final String H264_PROFILE_CONSTRAINED_HIGH = "640c";
  public static final String H264_LEVEL_3_1 = "1f"; // 31 in hex.
  public static final String H264_CONSTRAINED_HIGH_3_1 =
      H264_PROFILE_CONSTRAINED_HIGH + H264_LEVEL_3_1;
  public static final String H264_CONSTRAINED_BASELINE_3_1 =
      H264_PROFILE_CONSTRAINED_BASELINE + H264_LEVEL_3_1;

  public final String name;
  public final Map<String, String> params;
  // RingRTC change to include scalability modes into VideoCodecInfo
  public final List<String> scalabilityModes;
  @Deprecated public final int payload;

  @CalledByNative
  public VideoCodecInfo(String name, Map<String, String> params, List<String> scalabilityModes) {
    this.payload = 0;
    this.name = name;
    this.params = params;
    // RingRTC change to include scalability modes into VideoCodecInfo
    this.scalabilityModes =
        scalabilityModes != null ? scalabilityModes : Collections.emptyList();
  }

  public VideoCodecInfo(String name, Map<String, String> params) {
    this(name, params, Collections.emptyList());
  }

  @Deprecated
  public VideoCodecInfo(int payload, String name, Map<String, String> params) {
    this.payload = payload;
    this.name = name;
    this.params = params;
    this.scalabilityModes = Collections.emptyList();
  }

  @Override
  public boolean equals(@Nullable Object obj) {
    if (obj == null)
      return false;
    if (obj == this)
      return true;
    if (!(obj instanceof VideoCodecInfo))
      return false;

    VideoCodecInfo otherInfo = (VideoCodecInfo) obj;
    return name.equalsIgnoreCase(otherInfo.name) && params.equals(otherInfo.params) &&
        scalabilityModes.equals(otherInfo.scalabilityModes);
  }

  @Override
  public int hashCode() {
    Object[] values = {name.toUpperCase(Locale.ROOT), params, scalabilityModes};
    return Arrays.hashCode(values);
  }

  @Override
  public String toString() {
    return "VideoCodec{" + name + " " + params + " " + scalabilityModes + "}";
  }

  @CalledByNative
  String getName() {
    return name;
  }

  @CalledByNative
  Map<String, String> getParams() {
    return params;
  }

  // RingRTC change to include scalability modes into VideoCodecInfo
  @CalledByNative
  List<String> getScalabilityModes() {
    return scalabilityModes;
  }
}
