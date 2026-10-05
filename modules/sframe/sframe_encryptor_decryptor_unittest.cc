/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/sframe/sframe_decryptor_interface.h"
#include "api/sframe/sframe_types.h"
#include "modules/sframe/sframe_decryptor.h"
#include "modules/sframe/sframe_encryptor.h"
#include "modules/sframe/sframe_media_decryptor_interface.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

constexpr uint64_t kKeyId = 7;
const std::vector<uint8_t> kKeyMaterial = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                           0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
                                           0x0c, 0x0d, 0x0e, 0x0f};
const std::vector<uint8_t> kPlaintext = {0xde, 0xad, 0xbe, 0xef, 0x01,
                                         0x02, 0x03, 0x04, 0x05, 0x06};

class SframeEncryptorDecryptorTest : public ::testing::Test {
 protected:
  SframeEncryptorDecryptorTest()
      : encryptor_(
            SframeEncryptor::Create(SframeMode::kPerFrame,
                                    SframeCipherSuite::kAes128GcmSha256_128)),
        decryptor_(
            SframeDecryptor::Create(SframeCipherSuite::kAes128GcmSha256_128)) {}

  scoped_refptr<SframeEncryptor> encryptor_;
  scoped_refptr<SframeDecryptor> decryptor_;
};

TEST_F(SframeEncryptorDecryptorTest, SetEncryptionKeySucceeds) {
  EXPECT_TRUE(encryptor_->SetEncryptionKey(kKeyId, kKeyMaterial).ok());
}

TEST_F(SframeEncryptorDecryptorTest, EncryptProducesCiphertext) {
  ASSERT_TRUE(encryptor_->SetEncryptionKey(kKeyId, kKeyMaterial).ok());

  size_t max_ct_size = encryptor_->GetMaxCiphertextByteSize(kPlaintext.size());
  std::vector<uint8_t> ciphertext(max_ct_size);

  auto result = encryptor_->Encrypt(kPlaintext, /*additional_data=*/{},
                                    std::span<uint8_t>(ciphertext));
  ASSERT_TRUE(result.ok());
  EXPECT_GT(result.value(), kPlaintext.size());
}

TEST_F(SframeEncryptorDecryptorTest, EncryptFailsWithoutKey) {
  // No key set — encryption should fail with INVALID_STATE.
  size_t max_ct_size = encryptor_->GetMaxCiphertextByteSize(kPlaintext.size());
  std::vector<uint8_t> ciphertext(max_ct_size);
  auto result = encryptor_->Encrypt(kPlaintext, /*additional_data=*/{},
                                    std::span<uint8_t>(ciphertext));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error().type(), RTCErrorType::INVALID_STATE);
}

TEST_F(SframeEncryptorDecryptorTest, MultipleKeyRotation) {
  constexpr uint64_t kKeyId2 = 42;
  const std::vector<uint8_t> key2 = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                     0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
                                     0x1c, 0x1d, 0x1e, 0x1f};

  ASSERT_TRUE(encryptor_->SetEncryptionKey(kKeyId, kKeyMaterial).ok());
  size_t max_ct_size = encryptor_->GetMaxCiphertextByteSize(kPlaintext.size());
  std::vector<uint8_t> ct1(max_ct_size);
  ASSERT_TRUE(
      encryptor_
          ->Encrypt(kPlaintext, /*additional_data=*/{}, std::span<uint8_t>(ct1))
          .ok());

  // Rotate to second key and encrypt again.
  ASSERT_TRUE(encryptor_->SetEncryptionKey(kKeyId2, key2).ok());
  std::vector<uint8_t> ct2(max_ct_size);
  ASSERT_TRUE(
      encryptor_
          ->Encrypt(kPlaintext, /*additional_data=*/{}, std::span<uint8_t>(ct2))
          .ok());
  EXPECT_NE(ct1, ct2);
}

TEST_F(SframeEncryptorDecryptorTest, GetMaxCiphertextByteSizeIsLarger) {
  EXPECT_GT(encryptor_->GetMaxCiphertextByteSize(100), 100u);
}

TEST_F(SframeEncryptorDecryptorTest, AddDecryptionKeySucceeds) {
  EXPECT_TRUE(decryptor_->AddDecryptionKey(kKeyId, kKeyMaterial).ok());
}

TEST_F(SframeEncryptorDecryptorTest, RemoveDecryptionKeySucceeds) {
  EXPECT_TRUE(decryptor_->AddDecryptionKey(kKeyId, kKeyMaterial).ok());
  EXPECT_TRUE(decryptor_->RemoveDecryptionKey(kKeyId).ok());
}

TEST_F(SframeEncryptorDecryptorTest, EncryptThenDecryptRoundTrip) {
  ASSERT_TRUE(encryptor_->SetEncryptionKey(kKeyId, kKeyMaterial).ok());
  ASSERT_TRUE(decryptor_->AddDecryptionKey(kKeyId, kKeyMaterial).ok());

  size_t max_ct_size = encryptor_->GetMaxCiphertextByteSize(kPlaintext.size());
  std::vector<uint8_t> ciphertext(max_ct_size);
  auto enc_result = encryptor_->Encrypt(kPlaintext, /*additional_data=*/{},
                                        std::span<uint8_t>(ciphertext));
  ASSERT_TRUE(enc_result.ok());
  ciphertext.resize(enc_result.value());

  std::vector<uint8_t> plaintext(
      decryptor_->GetMaxPlaintextByteSize(ciphertext.size()));
  auto dec_result = decryptor_->Decrypt(ciphertext, /*additional_data=*/{},
                                        std::span<uint8_t>(plaintext));
  auto* success = std::get_if<SframeDecryptSuccess>(&dec_result);
  ASSERT_NE(success, nullptr);
  plaintext.resize(success->bytes_written);

  EXPECT_EQ(plaintext, kPlaintext);
}

TEST_F(SframeEncryptorDecryptorTest, EncryptThenDecryptWithAdditionalData) {
  ASSERT_TRUE(encryptor_->SetEncryptionKey(kKeyId, kKeyMaterial).ok());
  ASSERT_TRUE(decryptor_->AddDecryptionKey(kKeyId, kKeyMaterial).ok());

  const std::vector<uint8_t> aad = {0xaa, 0xbb, 0xcc};

  size_t max_ct_size = encryptor_->GetMaxCiphertextByteSize(kPlaintext.size());
  std::vector<uint8_t> ciphertext(max_ct_size);
  auto enc_result =
      encryptor_->Encrypt(kPlaintext, aad, std::span<uint8_t>(ciphertext));
  ASSERT_TRUE(enc_result.ok());
  ciphertext.resize(enc_result.value());

  std::vector<uint8_t> plaintext(
      decryptor_->GetMaxPlaintextByteSize(ciphertext.size()));
  auto dec_result =
      decryptor_->Decrypt(ciphertext, aad, std::span<uint8_t>(plaintext));
  auto* success = std::get_if<SframeDecryptSuccess>(&dec_result);
  ASSERT_NE(success, nullptr);
  plaintext.resize(success->bytes_written);

  EXPECT_EQ(plaintext, kPlaintext);
}

TEST_F(SframeEncryptorDecryptorTest, DecryptFailsWithWrongKey) {
  ASSERT_TRUE(encryptor_->SetEncryptionKey(kKeyId, kKeyMaterial).ok());

  const std::vector<uint8_t> wrong_key = {0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa,
                                          0xf9, 0xf8, 0xf7, 0xf6, 0xf5, 0xf4,
                                          0xf3, 0xf2, 0xf1, 0xf0};
  ASSERT_TRUE(decryptor_->AddDecryptionKey(kKeyId, wrong_key).ok());

  size_t max_ct_size = encryptor_->GetMaxCiphertextByteSize(kPlaintext.size());
  std::vector<uint8_t> ciphertext(max_ct_size);
  auto enc_result = encryptor_->Encrypt(kPlaintext, /*additional_data=*/{},
                                        std::span<uint8_t>(ciphertext));
  ASSERT_TRUE(enc_result.ok());
  ciphertext.resize(enc_result.value());

  std::vector<uint8_t> plaintext(
      decryptor_->GetMaxPlaintextByteSize(ciphertext.size()));
  auto dec_result = decryptor_->Decrypt(ciphertext, /*additional_data=*/{},
                                        std::span<uint8_t>(plaintext));
  EXPECT_TRUE(std::holds_alternative<SframeDecryptFailure>(dec_result));
}

TEST_F(SframeEncryptorDecryptorTest, DecryptFailsAfterKeyRemoved) {
  ASSERT_TRUE(encryptor_->SetEncryptionKey(kKeyId, kKeyMaterial).ok());
  ASSERT_TRUE(decryptor_->AddDecryptionKey(kKeyId, kKeyMaterial).ok());

  size_t max_ct_size = encryptor_->GetMaxCiphertextByteSize(kPlaintext.size());
  std::vector<uint8_t> ciphertext(max_ct_size);
  auto enc_result = encryptor_->Encrypt(kPlaintext, /*additional_data=*/{},
                                        std::span<uint8_t>(ciphertext));
  ASSERT_TRUE(enc_result.ok());
  ciphertext.resize(enc_result.value());

  ASSERT_TRUE(decryptor_->RemoveDecryptionKey(kKeyId).ok());
  std::vector<uint8_t> plaintext(
      decryptor_->GetMaxPlaintextByteSize(ciphertext.size()));
  auto dec_result = decryptor_->Decrypt(ciphertext, /*additional_data=*/{},
                                        std::span<uint8_t>(plaintext));
  EXPECT_TRUE(std::holds_alternative<SframeDecryptFailure>(dec_result));
}

TEST_F(SframeEncryptorDecryptorTest, DecryptFailsWithWrongAdditionalData) {
  ASSERT_TRUE(encryptor_->SetEncryptionKey(kKeyId, kKeyMaterial).ok());
  ASSERT_TRUE(decryptor_->AddDecryptionKey(kKeyId, kKeyMaterial).ok());

  const std::vector<uint8_t> aad = {0xaa, 0xbb, 0xcc};
  const std::vector<uint8_t> wrong_aad = {0x11, 0x22, 0x33};

  size_t max_ct_size = encryptor_->GetMaxCiphertextByteSize(kPlaintext.size());
  std::vector<uint8_t> ciphertext(max_ct_size);
  auto enc_result =
      encryptor_->Encrypt(kPlaintext, aad, std::span<uint8_t>(ciphertext));
  ASSERT_TRUE(enc_result.ok());
  ciphertext.resize(enc_result.value());

  std::vector<uint8_t> plaintext(
      decryptor_->GetMaxPlaintextByteSize(ciphertext.size()));
  auto dec_result =
      decryptor_->Decrypt(ciphertext, wrong_aad, std::span<uint8_t>(plaintext));
  EXPECT_TRUE(std::holds_alternative<SframeDecryptFailure>(dec_result));
}

TEST_F(SframeEncryptorDecryptorTest, DecryptFailsWithTruncatedCiphertext) {
  ASSERT_TRUE(encryptor_->SetEncryptionKey(kKeyId, kKeyMaterial).ok());
  ASSERT_TRUE(decryptor_->AddDecryptionKey(kKeyId, kKeyMaterial).ok());

  size_t max_ct_size = encryptor_->GetMaxCiphertextByteSize(kPlaintext.size());
  std::vector<uint8_t> ciphertext(max_ct_size);
  auto enc_result = encryptor_->Encrypt(kPlaintext, /*additional_data=*/{},
                                        std::span<uint8_t>(ciphertext));
  ASSERT_TRUE(enc_result.ok());
  ciphertext.resize(enc_result.value());

  std::vector<uint8_t> truncated(ciphertext.begin(),
                                 ciphertext.begin() + ciphertext.size() / 2);
  std::vector<uint8_t> plaintext(
      decryptor_->GetMaxPlaintextByteSize(truncated.size()));
  auto dec_result = decryptor_->Decrypt(truncated, /*additional_data=*/{},
                                        std::span<uint8_t>(plaintext));
  EXPECT_TRUE(std::holds_alternative<SframeDecryptFailure>(dec_result));
}

TEST_F(SframeEncryptorDecryptorTest, DecryptFailsWhenAuthTagIsTampered) {
  ASSERT_TRUE(encryptor_->SetEncryptionKey(kKeyId, kKeyMaterial).ok());
  ASSERT_TRUE(decryptor_->AddDecryptionKey(kKeyId, kKeyMaterial).ok());

  size_t max_ct_size = encryptor_->GetMaxCiphertextByteSize(kPlaintext.size());
  std::vector<uint8_t> ciphertext(max_ct_size);
  auto enc_result = encryptor_->Encrypt(kPlaintext, /*additional_data=*/{},
                                        std::span<uint8_t>(ciphertext));
  ASSERT_TRUE(enc_result.ok());
  ciphertext.resize(enc_result.value());

  // Flip a bit in the last byte — for AES-GCM the auth tag sits at the tail
  // of the ciphertext, so this is guaranteed to invalidate the MAC.
  ciphertext.back() ^= 0x01;

  std::vector<uint8_t> plaintext(
      decryptor_->GetMaxPlaintextByteSize(ciphertext.size()));
  auto dec_result = decryptor_->Decrypt(ciphertext, /*additional_data=*/{},
                                        std::span<uint8_t>(plaintext));
  auto* failure = std::get_if<SframeDecryptFailure>(&dec_result);
  ASSERT_NE(failure, nullptr);
  EXPECT_EQ(failure->type, SframeDecryptErrorType::kAuthentication);
}

TEST_F(SframeEncryptorDecryptorTest, GetMaxPlaintextByteSizeIsAtLeastInput) {
  EXPECT_GE(decryptor_->GetMaxPlaintextByteSize(100), 100u);
}

TEST_F(SframeEncryptorDecryptorTest, DecryptReportsKeyIdErrorForUnknownKey) {
  ASSERT_TRUE(encryptor_->SetEncryptionKey(kKeyId, kKeyMaterial).ok());

  size_t max_ct_size = encryptor_->GetMaxCiphertextByteSize(kPlaintext.size());
  std::vector<uint8_t> ciphertext(max_ct_size);
  auto enc_result = encryptor_->Encrypt(kPlaintext, /*additional_data=*/{},
                                        std::span<uint8_t>(ciphertext));
  ASSERT_TRUE(enc_result.ok());
  ciphertext.resize(enc_result.value());

  // Decryptor has no key registered, so unprotect fails with kKeyId. The
  // parsed key id is not yet surfaced (pending third_party/sframe support).
  std::vector<uint8_t> plaintext(
      decryptor_->GetMaxPlaintextByteSize(ciphertext.size()));
  auto dec_result = decryptor_->Decrypt(ciphertext, /*additional_data=*/{},
                                        std::span<uint8_t>(plaintext));
  auto* failure = std::get_if<SframeDecryptFailure>(&dec_result);
  ASSERT_NE(failure, nullptr);
  EXPECT_EQ(failure->type, SframeDecryptErrorType::kKeyId);
  EXPECT_FALSE(failure->key_id.has_value());
}

}  // namespace
}  // namespace webrtc
