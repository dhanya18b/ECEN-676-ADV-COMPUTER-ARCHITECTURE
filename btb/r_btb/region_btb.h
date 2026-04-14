#ifndef BTB_REGION_BTB_H
#define BTB_REGION_BTB_H

#include "address.h"
#include "indirect_predictor.h"
#include "modules.h"
#include "region_predictor.h"
#include "return_stack.h"

/*
 * region_btb – Region BTB (R-BTB) implementation
 * ================================================
 * Based on: "Branch Target Buffer Organizations", Perais & Sheikh, MICRO '23.
 *
 * Architecture overview
 * ---------------------
 * Unlike the basic (Instruction) BTB where each entry tracks a single branch,
 * the R-BTB stores metadata for an aligned 64-byte region of the instruction
 * stream in one entry.  Each entry has NUM_BRANCH_SLOTS branch slots, letting
 * a single lookup provide fetch-PCs for all taken branches within the region.
 *
 * Subcomponents:
 *   region   – the R-BTB proper (region_predictor), organised as a set-
 *              associative LRU table indexed by the 64B-aligned region address.
 *   indirect – gshare-like indirect branch target predictor (reused from
 *              basic_btb).
 *   ras      – Return Address Stack (reused from basic_btb).
 *
 * Prediction flow
 * ---------------
 *  btb_prediction(ip):
 *    1. Look up the region entry covering ip.
 *    2. Walk the branch slots whose byte offset >= offset(ip) within the region
 *       (slots that come *before* ip in program order are irrelevant for this
 *       fetch).
 *    3. Return the target of the first relevant slot, consulting the RAS for
 *       returns and the indirect predictor for indirect branches.
 *    4. If no entry is found, return {0, false} → BTB miss.
 *
 * Update flow
 * -----------
 *  update_btb(ip, target, taken, branch_type):
 *    - Push to RAS on calls.
 *    - Update indirect predictor for indirect branches.
 *    - Calibrate RAS call-size tracker on returns.
 *    - Update the region entry (region_predictor::update).
 */
class region_btb : champsim::modules::btb
{
  return_stack    ras{};
  indirect_predictor indirect{};
  region_predictor   region{};

public:
  using btb::btb;
  region_btb() : btb(nullptr) {}

  std::pair<champsim::address, bool> btb_prediction(champsim::address ip);
  void update_btb(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type);
};

#endif
