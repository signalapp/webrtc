/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc;

import org.jni_zero.CalledByNative;

/**
 * Fingerprint of a certificate, mirroring the WebIDL RTCDtlsFingerprint
 * dictionary. See https://w3c.github.io/webrtc-pc/#dom-rtcdtlsfingerprint
 */
public class DtlsFingerprint {
  /** The hash function used to compute the fingerprint, e.g. "sha-256". */
  public final String algorithm;
  /**
   * The fingerprint value as colon-separated hexadecimal, using the syntax of
   * the fingerprint in RFC 4572 Section 5, e.g. "AB:CD:...".
   */
  public final String value;

  @CalledByNative
  public DtlsFingerprint(String algorithm, String value) {
    this.algorithm = algorithm;
    this.value = value;
  }
}
