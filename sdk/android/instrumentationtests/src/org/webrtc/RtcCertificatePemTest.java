/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc;

import static com.google.common.truth.Truth.assertThat;

import androidx.test.filters.SmallTest;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.webrtc.PeerConnection;
import org.webrtc.RtcCertificatePem;

/** Tests for RtcCertificatePem.java. */
public class RtcCertificatePemTest {
  @Before
  public void setUp() {
    System.loadLibrary(TestConstants.NATIVE_LIBRARY);
  }

  @Test
  @SmallTest
  public void testConstructor() {
    RtcCertificatePem original = RtcCertificatePem.generateCertificate();
    RtcCertificatePem recreated = new RtcCertificatePem(original.privateKey, original.certificate);
    assertThat(original.privateKey).isEqualTo(recreated.privateKey);
    assertThat(original.certificate).isEqualTo(recreated.certificate);
  }

  @Test
  @SmallTest
  public void testGenerateCertificateDefaults() {
    RtcCertificatePem rtcCertificate = RtcCertificatePem.generateCertificate();
    assertThat(rtcCertificate.privateKey).isNotEmpty();
    assertThat(rtcCertificate.certificate).isNotEmpty();
  }

  @Test
  @SmallTest
  public void testGenerateCertificateCustomKeyTypeDefaultExpires() {
    RtcCertificatePem rtcCertificate =
        RtcCertificatePem.generateCertificate(PeerConnection.KeyType.RSA);
    assertThat(rtcCertificate.privateKey).isNotEmpty();
    assertThat(rtcCertificate.certificate).isNotEmpty();
  }

  @Test
  @SmallTest
  public void testGenerateCertificateCustomExpiresDefaultKeyType() {
    RtcCertificatePem rtcCertificate = RtcCertificatePem.generateCertificate(60 * 60 * 24);
    assertThat(rtcCertificate.privateKey).isNotEmpty();
    assertThat(rtcCertificate.certificate).isNotEmpty();
  }

  @Test
  @SmallTest
  public void testGenerateCertificateCustomKeyTypeAndExpires() {
    RtcCertificatePem rtcCertificate =
        RtcCertificatePem.generateCertificate(PeerConnection.KeyType.RSA, 60 * 60 * 24);
    assertThat(rtcCertificate.privateKey).isNotEmpty();
    assertThat(rtcCertificate.certificate).isNotEmpty();
  }

  @Test
  @SmallTest
  public void testGeneratedCertificateHasFingerprint() {
    RtcCertificatePem rtcCertificate = RtcCertificatePem.generateCertificate();
    List<DtlsFingerprint> fingerprints = rtcCertificate.getFingerprints();
    assertThat(fingerprints).hasSize(1);

    DtlsFingerprint fingerprint = fingerprints.get(0);
    // The algorithm is an IANA hash function textual name, e.g. "sha-256".
    assertThat(fingerprint.algorithm).matches("[a-z0-9\\-]+");
    // The value is colon-separated hexadecimal per RFC 4572, e.g. "AB:CD:...".
    assertThat(fingerprint.value).matches("([0-9A-F]{2}:)+[0-9A-F]{2}");
  }

  @Test
  @SmallTest
  public void testFingerprintSurvivesPemRoundTrip() {
    RtcCertificatePem original = RtcCertificatePem.generateCertificate();
    RtcCertificatePem recreated = new RtcCertificatePem(original.privateKey, original.certificate);

    List<DtlsFingerprint> originalFingerprints = original.getFingerprints();
    List<DtlsFingerprint> recreatedFingerprints = recreated.getFingerprints();
    assertThat(originalFingerprints).hasSize(1);
    assertThat(recreatedFingerprints).hasSize(1);
    assertThat(originalFingerprints.get(0).algorithm)
        .isEqualTo(recreatedFingerprints.get(0).algorithm);
    assertThat(originalFingerprints.get(0).value).isEqualTo(recreatedFingerprints.get(0).value);
  }

  @Test
  @SmallTest
  public void testCertificateFromInvalidPemHasNoFingerprint() {
    // A certificate built from PEM strings that are not valid certificates has
    // no computable fingerprint.
    RtcCertificatePem rtcCertificate = new RtcCertificatePem("private", "certificate");
    assertThat(rtcCertificate.getFingerprints()).isEmpty();
  }
}
