// ScoreMod.h -- the wire order of the smod= launch argument
//
// Accessory score multipliers travel from the menu to the render exe as a
// positional CSV:
//
//     smod=camo,radar,scent,double,tranq,observer
//
// No accessory name appears in the string, so the order is the whole
// contract. If the menu emitted one accessory earlier than the engine
// expected to read it, that accessory would quietly take another one's
// multiplier -- the hunt still runs, just with the wrong scores, and nothing
// in the log would say so. Both sides build and read through
// kScoreModWireOrder below so the two cannot drift apart.
//
// This is not the same numbering as the menu's accessory indices
// (kAccCamo ... kAccTranq in Menu/Menu.cpp): those include NightVision, which
// is selectable but neutral for scoring, and so has no slot here.
#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

enum class ScoreModSlot : int {
  Camo = 0,
  Radar,
  Scent,
  Double,
  Tranq,
  Observer,
  Count
};

inline constexpr int kScoreModSlotCount = static_cast<int>(ScoreModSlot::Count);

// The contract. Emit and read in exactly this order.
inline constexpr ScoreModSlot kScoreModWireOrder[kScoreModSlotCount] = {
    ScoreModSlot::Camo,  ScoreModSlot::Radar,  ScoreModSlot::Scent,
    ScoreModSlot::Double, ScoreModSlot::Tranq, ScoreModSlot::Observer};

// Same order, for tests and diagnostics.
inline constexpr const char* kScoreModSlotName[kScoreModSlotCount] = {
    "camo", "radar", "scent", "double", "tranq", "observer"};

// Serialize one value per slot, in wire order. `values` is indexed by slot,
// not by wire position, so the caller never has to know the order. Returns
// the payload length, or 0 if the buffer cannot hold the whole thing.
inline size_t BuildScoreModPayload(const float values[kScoreModSlotCount],
                                   char* out, size_t capacity)
{
  if (!out || capacity == 0) return 0;
  out[0] = '\0';

  size_t used = 0;
  for (int i = 0; i < kScoreModSlotCount; ++i) {
    const float value = values[static_cast<int>(kScoreModWireOrder[i])];
    char field[32];
    // %g is what the menu's operator<< produced, so the emitted string is
    // unchanged by routing it through here.
    const int written = std::snprintf(field, sizeof(field), i == 0 ? "%g" : ",%g",
                                      static_cast<double>(value));
    if (written < 0 || used + static_cast<size_t>(written) + 1 > capacity) {
      out[0] = '\0';
      return 0;
    }
    std::memcpy(out + used, field, static_cast<size_t>(written) + 1);
    used += static_cast<size_t>(written);
  }
  return used;
}

// Read one value per slot, in wire order, stopping at the first field that is
// not a number. Fewer fields than slots leaves the remaining slots untouched
// -- the engine's original sscanf chain assigned each score only when sscanf
// had reported that many conversions. Returns how many slots were filled.
inline int ParseScoreModPayload(const char* payload,
                                float values[kScoreModSlotCount])
{
  if (!payload) return 0;

  int got = 0;
  const char* cursor = payload;
  while (got < kScoreModSlotCount) {
    char* end = nullptr;
    const float value = std::strtof(cursor, &end);
    if (end == cursor) break;  // no digits here: stop, as sscanf did
    values[static_cast<int>(kScoreModWireOrder[got])] = value;
    ++got;
    if (*end != ',') break;    // no further fields
    cursor = end + 1;
  }
  return got;
}
