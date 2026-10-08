#include "ota_signature.h"

#include <cstring>

#include "monocypher-ed25519.h"

namespace ota {

const uint8_t kSignDomain[16] = {'R', 'V', '-', 'L', 'e', 'v', 'e', 'l', ' ', 'O', 'T', 'A', ' ', 'v', '1', 0};

void signedMessage(uint32_t size, const uint8_t sha256[32], uint8_t out[kSignedMessageLen]) {
  std::memcpy(out, kSignDomain, sizeof kSignDomain);
  for (int i = 0; i < 4; ++i) out[16 + i] = static_cast<uint8_t>(size >> (8 * i));
  std::memcpy(out + 20, sha256, 32);
}

bool verifyImage(const uint8_t publicKey[kPublicKeyLen], uint32_t size, const uint8_t sha256[32],
                 const uint8_t signature[kSignatureLen]) {
  uint8_t m[kSignedMessageLen];
  signedMessage(size, sha256, m);
  return crypto_ed25519_check(signature, publicKey, m, sizeof m) == 0;
}

}  // namespace ota
