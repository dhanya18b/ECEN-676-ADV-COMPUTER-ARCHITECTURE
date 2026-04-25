#ifndef BTB_MBTB_INDIRECT_PREDICTOR_H
#define BTB_MBTB_INDIRECT_PREDICTOR_H

/*
 * mbtb_indirect_predictor.h
 *
 * Indirect branch target predictor for the MultiBlock BTB.
 *
 * Uses a hash of the branch PC XOR'd with a global conditional history
 * register to index a flat table of predicted targets — identical to the
 * basic_btb indirect_predictor.
 *
 * Within the MB-BTB flow this predictor is consulted only for indirect
 * branches whose target block has NOT yet been chained (br_follow == false),
 * i.e. before the INDIRECT_STABILITY_THRESHOLD is reached.  Once the
 * stability threshold is met, the mbtb_direct_predictor takes over and
 * caches the target directly inside the BTB entry.
 */

#include <array>
#include <bitset>
#include <cstdint>
#include <utility>

#include "address.h"
#include "champsim.h"
#include "msl/bits.h"

struct mbtb_indirect_predictor {
  static constexpr std::size_t size = 4096;

  std::array<champsim::address, size> predictor        = {};
  std::bitset<champsim::msl::lg2(size)> conditional_history = {};

  /** Return the predicted target for an indirect branch at @ip. */
  std::pair<champsim::address, bool> prediction(champsim::address ip);

  /** Record the resolved target for an indirect branch at @ip. */
  void update_target(champsim::address ip, champsim::address branch_target);

  /** Shift the global conditional history with the outcome of a conditional branch. */
  void update_direction(bool taken);
};

#endif // BTB_MBTB_INDIRECT_PREDICTOR_H
