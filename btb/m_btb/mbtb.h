#ifndef BTB_MBTB_H
#define BTB_MBTB_H

/*
 * mbtb.h  –  MultiBlock BTB top-level module
 *
 * Mirrors basic_btb.h exactly in structure.  Swap "basic_btb" for "mbtb"
 * in your ChampSim JSON config to use this module.
 *
 * Three sub-components (matching basic_btb's three sub-components):
 *
 *   basic_btb sub-component   │  mbtb equivalent
 *   ──────────────────────────┼────────────────────────────────────
 *   return_stack              │  mbtb_return_stack   (same logic)
 *   indirect_predictor        │  mbtb_indirect_predictor (same logic)
 *   direct_predictor (I-BTB)  │  mbtb_direct_predictor  (MB-BTB logic)
 *
 * The mbtb_direct_predictor stores up to NUM_BRANCH_SLOTS branches per
 * entry and chains the target block of qualifying branches into the same
 * entry (MultiBlock BTB, paper §6.4).  Returns and unresolved indirects
 * are still delegated to mbtb_return_stack and mbtb_indirect_predictor
 * respectively, exactly as in basic_btb.
 */

#include "address.h"
#include "mbtb_direct_predictor.h"
#include "mbtb_indirect_predictor.h"
#include "mbtb_return_stack.h"
#include "modules.h"

class mbtb : champsim::modules::btb
{
  mbtb_return_stack       ras{};
  mbtb_indirect_predictor indirect{};
  mbtb_direct_predictor   direct{};

public:
  using btb::btb;
  mbtb() : btb(nullptr) {}

  std::pair<champsim::address, bool> btb_prediction(champsim::address ip);
  void update_btb(champsim::address ip,
                  champsim::address branch_target,
                  bool              taken,
                  uint8_t           branch_type);
};

#endif // BTB_MBTB_H
