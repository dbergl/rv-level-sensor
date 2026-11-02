#pragma once
#include <cstddef>
#include <cstdint>

#include "bridge.h"
#include "ha_link.h"

// The Home Assistant control service, minus the radio: pairing, the one session and the link it
// is bound to, command authentication, routing bridge results back to the session that asked, and
// when to send status. Runs on one task (the loop); the GATT glue queues writes to it. No heap.
namespace ac {

class HaControl {
 public:
  using Fill = void (*)(uint8_t* out, size_t n);  // cryptographic randomness
  static constexpr uint16_t kNoLink = 0xFFFF;
  static constexpr uint8_t kPair = 0x01;
  static constexpr uint8_t kSession = 0x02;
  static constexpr uint32_t kStatusEveryMs = 5000;
  static constexpr size_t kMaxOut = kStatusLen;  // the longest: status, then the pairing reply
  static_assert(kMaxOut >= 1 + kKeyLen, "pairing reply must fit");
  static constexpr size_t kMaxInFlight = kMaxPending + kMaxResults;

  struct Out {
    uint16_t h = kNoLink;  // the link to notify
    uint8_t d[kMaxOut];
    size_t n = 0;  // 0: nothing to send
  };
  enum class Auth : uint8_t { None, Refused, Paired, Session };

  HaControl(Bridge& b, Fill fill) : bridge_(b), fill_(fill) {}

  void setKey(const uint8_t key[kKeyLen]);
  bool paired() const { return paired_; }
  const uint8_t* key() const { return key_; }  // to store after Auth::Paired
  // Unpair: forgets the key and ends the session (its link is handed to takeDrop).
  void forget();

  // Auth characteristic. 01 || haPub32 pairs, only while pairOpen: reply 01 || espPub32, the new
  // key replaces the old one and the session ends. Anything else starting 01 gets 01 alone. 02
  // starts a session bound to h with a fresh nonce (reply 02 || nonce16), replacing any session;
  // without a key, or with trailing bytes, the reply is 02 alone. Other writes are ignored.
  Auth onAuth(uint16_t h, const uint8_t* d, size_t n, bool pairOpen, Out& reply);
  // Command characteristic: counter || op || value || mac. Only from the session's link and with
  // a valid MAC and a fresh counter; then submitted to the bridge. Otherwise reply BadAuth.
  void onCommand(uint16_t h, const uint8_t* d, size_t n, uint32_t nowMs, Out& reply);
  void onDisconnect(uint16_t h);

  // Drains the bridge's results (the only caller of takeResult). True with a result for the
  // current session to notify; results for the panel (tag 0) and ended sessions are discarded.
  bool nextResult(Out& out);
  // True when the session should get a status notification now (34 bytes in out).
  bool statusDue(uint32_t nowMs, Out& out);

  uint16_t sessionLink() const { return link_; }
  // A link to disconnect: the old session after re-pairing elsewhere or unpairing. Once each.
  uint16_t takeDrop();

 private:
  struct InFlight {
    uint32_t tag;  // the bridge's tag, unique per command
    uint32_t counter;
    uint32_t session;
  };

  void endSession();
  void removeInFlight(size_t i);  // keeps the rest oldest first

  Bridge& bridge_;
  Fill fill_;
  uint8_t key_[kKeyLen] = {};
  bool paired_ = false;
  Session session_;
  uint16_t link_ = kNoLink;
  uint32_t sessionId_ = 0;  // bumps per session; results remember which one asked
  uint16_t drop_ = kNoLink;

  InFlight inFlight_[kMaxInFlight] = {};  // oldest first
  size_t inFlightCount_ = 0;
  uint32_t nextTag_ = 1;  // never 0, the panel's tag

  bool statusSent_ = false;  // since the session began
  uint32_t lastStatusMs_ = 0;
  uint32_t lastVersion_ = 0;
  bool lastResponding_ = false;
};

}  // namespace ac
