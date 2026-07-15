#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <mbedtls/gcm.h>

#include "equine_protocol.h"

namespace EquineCrypto {

inline bool encrypt(
    const uint8_t key[EquineProtocol::AEAD_KEY_SIZE],
    const EquineProtocol::SecureFrameHeaderV2& header,
    const uint8_t* plaintext,
    size_t plaintext_size,
    uint8_t* ciphertext,
    uint8_t tag[EquineProtocol::AEAD_TAG_SIZE]) {
  if (!key || (!plaintext && plaintext_size) || !ciphertext || !tag) return false;
  uint8_t nonce[EquineProtocol::AEAD_NONCE_SIZE];
  EquineProtocol::makeAeadNonce(header, nonce);
  mbedtls_gcm_context context;
  mbedtls_gcm_init(&context);
  bool ok = mbedtls_gcm_setkey(
      &context, MBEDTLS_CIPHER_ID_AES, key,
      EquineProtocol::AEAD_KEY_SIZE * 8) == 0;
  if (ok) {
    ok = mbedtls_gcm_crypt_and_tag(
      &context, MBEDTLS_GCM_ENCRYPT, plaintext_size,
      nonce, sizeof(nonce),
      reinterpret_cast<const uint8_t*>(&header), sizeof(header),
      plaintext, ciphertext,
      EquineProtocol::AEAD_TAG_SIZE, tag) == 0;
  }
  mbedtls_gcm_free(&context);
  return ok;
}

inline bool decrypt(
    const uint8_t key[EquineProtocol::AEAD_KEY_SIZE],
    const EquineProtocol::SecureFrameHeaderV2& header,
    const uint8_t* ciphertext,
    size_t ciphertext_size,
    const uint8_t tag[EquineProtocol::AEAD_TAG_SIZE],
    uint8_t* plaintext) {
  if (!key || (!ciphertext && ciphertext_size) || !tag || !plaintext) return false;
  uint8_t nonce[EquineProtocol::AEAD_NONCE_SIZE];
  EquineProtocol::makeAeadNonce(header, nonce);
  mbedtls_gcm_context context;
  mbedtls_gcm_init(&context);
  bool ok = mbedtls_gcm_setkey(
      &context, MBEDTLS_CIPHER_ID_AES, key,
      EquineProtocol::AEAD_KEY_SIZE * 8) == 0;
  if (ok) {
    ok = mbedtls_gcm_auth_decrypt(
      &context, ciphertext_size,
      nonce, sizeof(nonce),
      reinterpret_cast<const uint8_t*>(&header), sizeof(header),
      tag, EquineProtocol::AEAD_TAG_SIZE,
      ciphertext, plaintext) == 0;
  }
  mbedtls_gcm_free(&context);
  return ok;
}

inline bool selfTest() {
  uint8_t key[EquineProtocol::AEAD_KEY_SIZE];
  for (size_t i = 0; i < sizeof(key); i++) key[i] = static_cast<uint8_t>(i);
  const auto header = EquineProtocol::makeSecureFrameHeader(
    EquineProtocol::MessageType::HISTORY,
    EquineProtocol::HISTORY_SCHEMA_VERSION,
    0x0102030405060708ULL,
    0x1112131415161718ULL,
    7,
    42);
  const uint8_t plaintext[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  uint8_t ciphertext[sizeof(plaintext)]{};
  uint8_t recovered[sizeof(plaintext)]{};
  uint8_t tag[EquineProtocol::AEAD_TAG_SIZE]{};
  if (!encrypt(key, header, plaintext, sizeof(plaintext), ciphertext, tag) ||
      !decrypt(key, header, ciphertext, sizeof(ciphertext), tag, recovered) ||
      memcmp(plaintext, recovered, sizeof(plaintext)) != 0) {
    return false;
  }
  tag[0] ^= 1;
  return !decrypt(key, header, ciphertext, sizeof(ciphertext), tag, recovered);
}

}  // namespace EquineCrypto
