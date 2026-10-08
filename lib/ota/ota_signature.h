#pragma once
#include <cstddef>
#include <cstdint>

// Ed25519 signature on a firmware image (docs/protocol.md). The signed message is the domain
// string, the image size (u32 little endian) and the image's SHA-256.
namespace ota {

constexpr size_t kPublicKeyLen = 32;
constexpr size_t kSignatureLen = 64;
constexpr size_t kSignedMessageLen = 52;

extern const uint8_t kSignDomain[16];  // "RV-Level OTA v1\0"

void signedMessage(uint32_t size, const uint8_t sha256[32], uint8_t out[kSignedMessageLen]);
bool verifyImage(const uint8_t publicKey[kPublicKeyLen], uint32_t size, const uint8_t sha256[32],
                 const uint8_t signature[kSignatureLen]);

}  // namespace ota
