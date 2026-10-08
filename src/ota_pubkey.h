#pragma once
#include <cstdint>

// Ed25519 public key that firmware updates must be signed with. Defined in src/ota_pubkey.cpp,
// which tools/ota_pubkey.py generates at build time from the owner's key (gitignored).
extern const uint8_t kOtaPublicKey[32];
