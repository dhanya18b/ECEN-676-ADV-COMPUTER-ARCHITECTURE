/*
 * region_btb.cc – Region BTB (R-BTB) implementation
 * ===================================================
 * Implements btb_prediction and update_btb for the R-BTB organisation
 * described in Perais & Sheikh, "Branch Target Buffer Organizations", MICRO '23.
 *
 * Key design decisions (matching the paper):
 *  • 64-byte aligned regions, 2 branch slots per entry (configurable in
 *    region_predictor.h).
 *  • A single LRU set-associative table (512 sets × 6 ways) indexed by the
 *    region-aligned PC.
 *  • On a hit, the predictor scans slots in offset order and returns the target
 *    of the first slot whose offset is ≥ the fetch PC's offset within the
 *    region.  This models the fetch-PC selection described in Section 3.6.1 of
 *    the paper: the offset of each branch slot must be compared to the unaligned
 *    PC used to access the entry.
 *  • Returns and indirect branches are delegated to the RAS and indirect
 *    predictor respectively (same approach as basic_btb).
 *  • On a BTB miss, {0, false} is returned so the CPU frontend stalls.
 */

#include "region_btb.h"

#include "instruction.h"

/*
 * btb_prediction
 * --------------
 * Look up the R-BTB for the region containing ip and return the predicted
 * target of the first taken branch at or after ip within that region.
 *
 * Return value: {target, always_taken}
 *   always_taken = true  → predict taken unconditionally (fetch redirected)
 *   always_taken = false → branch is conditional; direction from branch pred.
 *   {0, false}           → BTB miss, no prediction available
 */
std::pair<champsim::address, bool> region_btb::btb_prediction(champsim::address ip)
{
  auto opt_entry = region.check_hit(ip);

  // BTB miss – no prediction for this region.
  if (!opt_entry.has_value())
    return {champsim::address{}, false};

  /*
   * Compute the byte offset of ip within its 64-byte region so we can
   * skip slots that belong to instructions *before* ip in program order.
   */
  auto raw_ip    = ip.to<unsigned long long>();
  auto region_base = raw_ip & ~static_cast<unsigned long long>(region_predictor::REGION_SIZE - 1);
  auto ip_offset = static_cast<uint8_t>(raw_ip - region_base);

  /*
   * Walk slots in ascending offset order.
   * The paper notes (Section 3.6.1) that each slot's offset must be compared
   * to the unaligned PC: only slots with offset >= ip_offset are reachable
   * from the current fetch PC.
   *
   * We find the valid slot with the smallest offset that is still >= ip_offset.
   */
  const region_predictor::branch_slot_t* best_slot = nullptr;
  for (const auto& slot : opt_entry->slots) {
    if (!slot.valid)
      continue;
    if (slot.offset < ip_offset)
      continue; // this branch is before the current fetch PC within the region
    if (best_slot == nullptr || slot.offset < best_slot->offset)
      best_slot = &slot;
  }

  // No relevant branch slot found for this fetch PC within the region.
  if (best_slot == nullptr)
    return {champsim::address{}, false};

  /*
   * Dispatch based on branch type, mirroring basic_btb::btb_prediction:
   *   RETURN   → RAS prediction
   *   INDIRECT → indirect target predictor
   *   CONDITIONAL → target known, but direction from branch predictor
   *   ALWAYS_TAKEN → redirect fetch unconditionally
   */
  if (best_slot->type == region_predictor::branch_info::RETURN)
    return ras.prediction();

  if (best_slot->type == region_predictor::branch_info::INDIRECT)
    return indirect.prediction(ip);

  return {best_slot->target,
          best_slot->type != region_predictor::branch_info::CONDITIONAL};
}

/*
 * update_btb
 * ----------
 * Record the resolved branch outcome so future lookups benefit from it.
 *
 * The update order follows basic_btb::update_btb exactly so that the RAS
 * and indirect predictor stay consistent with the rest of the simulator.
 */
void region_btb::update_btb(champsim::address ip, champsim::address branch_target,
                             bool taken, uint8_t branch_type)
{
  // Push the return address onto the RAS for calls.
  if (branch_type == BRANCH_DIRECT_CALL || branch_type == BRANCH_INDIRECT_CALL)
    ras.push(ip);

  // Update the indirect target predictor.
  if (branch_type == BRANCH_INDIRECT || branch_type == BRANCH_INDIRECT_CALL)
    indirect.update_target(ip, branch_target);

  // Shift conditional-branch history used by the indirect predictor.
  if (branch_type == BRANCH_CONDITIONAL)
    indirect.update_direction(taken);

  // Calibrate the per-call-site instruction-size tracker on returns.
  if (branch_type == BRANCH_RETURN)
    ras.calibrate_call_size(branch_target);

  // Update (or allocate) the region entry.
  region.update(ip, branch_target, branch_type);
}
