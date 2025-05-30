// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022-2024 Chilledheart  */
#include "crypto/aead_sodium_decrypter.hpp"

#include "core/logging.hpp"
#include "net/protocol.hpp"

#include "third_party/boringssl/src/include/openssl/crypto.h"
#include "third_party/boringssl/src/include/openssl/err.h"

// In debug builds only, log OpenSSL error stack. Then clear OpenSSL error
// stack.
static void DLogOpenSslErrors() {
#if !DCHECK_IS_ON()
  ERR_clear_error();
#else
  while (uint32_t error = ERR_get_error()) {
    char buf[120];
    ERR_error_string_n(error, buf, sizeof(buf));
    DLOG(ERROR) << "OpenSSL error: " << buf;
  }
#endif
}

namespace crypto {

AeadSodiumDecrypter::AeadSodiumDecrypter(const EVP_AEAD* (*aead_getter)(),
                                         size_t key_size,
                                         size_t auth_tag_size,
                                         size_t nonce_size)
    : AeadBaseDecrypter(key_size, auth_tag_size, nonce_size), aead_alg_(aead_getter()) {
  DCHECK_EQ(EVP_AEAD_key_length(aead_alg_), key_size);
  DCHECK_EQ(EVP_AEAD_nonce_length(aead_alg_), nonce_size);
  DCHECK_GE(EVP_AEAD_max_tag_len(aead_alg_), auth_tag_size);
}

AeadSodiumDecrypter::~AeadSodiumDecrypter() = default;

bool AeadSodiumDecrypter::SetKey(const char* key, size_t key_len) {
  if (!AeadBaseDecrypter::SetKey(key, key_len)) {
    return false;
  }
  EVP_AEAD_CTX_cleanup(ctx_.get());
  if (!EVP_AEAD_CTX_init(ctx_.get(), aead_alg_, key_, key_size_, auth_tag_size_, nullptr)) {
    DLogOpenSslErrors();
    return false;
  }
  return true;
}

bool AeadSodiumDecrypter::DecryptPacket(uint64_t packet_number,
                                        const char* associated_data,
                                        size_t associated_data_len,
                                        const char* ciphertext,
                                        size_t ciphertext_len,
                                        char* output,
                                        size_t* output_length,
                                        size_t max_output_length) {
  if (ciphertext_len < auth_tag_size_) {
    return false;
  }

  if (have_preliminary_key_) {
    LOG(ERROR) << "Unable to decrypt while key diversification is pending";
    return false;
  }

  uint8_t nonce[kMaxNonceSize] = {};
  memcpy(nonce, iv_, nonce_size_);

  // for libsodium, packet number is written ahead
  PacketNumberToNonceSodium(nonce, nonce_size_, packet_number);

  DumpHex("DE-NONCE", nonce, nonce_size_);

  if (!EVP_AEAD_CTX_open(ctx_.get(), reinterpret_cast<uint8_t*>(output), output_length, max_output_length, nonce,
                         nonce_size_, reinterpret_cast<const uint8_t*>(ciphertext), ciphertext_len,
                         reinterpret_cast<const uint8_t*>(associated_data), associated_data_len)) {
    DLogOpenSslErrors();
    return false;
  }
  return true;
}

}  // namespace crypto
