#include "ble_links.h"

namespace blelink {

int Links::find(uint16_t h) const {
  for (int i = 0; i < count_; ++i) {
    if (links_[i].h == h) return i;
  }
  return -1;
}

bool Links::onConnect(uint16_t h, uint32_t nowMs) {
  if (find(h) >= 0) return true;
  if (count_ == kSlots) return false;  // can't track it; NimBLE never gets here
  bool keep = total() + 1 < max_;
  links_[count_++] = {h, nowMs, false, !keep};
  return keep;
}

void Links::onDisconnect(uint16_t h) {
  int i = find(h);
  if (i < 0) return;
  for (; i + 1 < count_; ++i) links_[i] = links_[i + 1];
  --count_;
}

void Links::claim(uint16_t h) {
  int i = find(h);
  if (i >= 0) links_[i].claimed = true;
}

uint16_t Links::toDrop(uint32_t nowMs) {
  // Over the cap (a client link came up): links already being dropped will free their slots.
  int live = clientLinks_;
  for (int i = 0; i < count_; ++i) live += links_[i].dropping ? 0 : 1;
  int pick = -1;
  if (live + 1 > max_) {
    for (int i = count_ - 1; i >= 0 && pick < 0; --i) {
      if (!links_[i].dropping && !links_[i].claimed) pick = i;
    }
    for (int i = count_ - 1; i >= 0 && pick < 0; --i) {
      if (!links_[i].dropping) pick = i;
    }
  }
  for (int i = 0; i < count_ && pick < 0; ++i) {
    const Link& l = links_[i];
    if (!l.dropping && !l.claimed && nowMs - l.sinceMs >= claimTimeoutMs_) pick = i;
  }
  if (pick < 0) return kNoLink;
  links_[pick].dropping = true;
  return links_[pick].h;
}

}  // namespace blelink
