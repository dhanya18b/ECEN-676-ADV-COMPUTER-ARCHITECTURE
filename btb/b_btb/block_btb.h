#ifndef BTB_BLOCK_BTB_H
#define BTB_BLOCK_BTB_H

#include "address.h"
#include "direct_predictor.h"
#include "indirect_predictor.h"
#include "modules.h"
#include "return_stack.h"

/*
 * block_btb – Block BTB (B-BTB), ChampSim module
 * ===============================================
 * Based on: Perais & Sheikh, "Branch Target Buffer Organizations", MICRO '23
 * (DOI 10.1145/3613424.3623774).
 *
 * One BTB entry covers a fixed instruction block (BLOCK_SIZE insts × 4 B =
 * 64 B), with up to MAX_BRANCHES branch slots keyed by instruction offset in
 * the block — same structural pattern as region_btb / region_predictor, but
 * with per-instruction offsets instead of byte offsets within a region.
 */
class block_btb : champsim::modules::btb
{
  return_stack ras{};
  indirect_predictor indirect{};
  direct_predictor direct{};

public:
  using btb::btb;
  block_btb() : btb(nullptr) {}

  std::pair<champsim::address, bool> btb_prediction(champsim::address ip);
  void update_btb(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type);
};

#endif
