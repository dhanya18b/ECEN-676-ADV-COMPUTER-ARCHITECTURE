/*
 * mbtb.cc  –  MultiBlock BTB top-level module
 *
 * Mirrors basic_btb.cc exactly in structure.
 *
 * btb_prediction flow
 * ───────────────────
 *  1. Query mbtb_direct_predictor::check_hit(ip).
 *
 *  2. On miss  → return {0, false}  (no prediction).
 *
 *  3. On hit:
 *       RETURN   → delegate to mbtb_return_stack::prediction()  (same as basic_btb)
 *       INDIRECT, follow==false
 *                → delegate to mbtb_indirect_predictor::prediction()  (same as basic_btb)
 *       INDIRECT, follow==true   (single-target indirect, stability threshold met)
 *       CONDITIONAL, follow==true (always-taken conditional, target pulled in)
 *       UNCOND_DIRECT / CALL_DIRECT
 *                → return stored target directly; always_taken = true for
 *                  unconditionals/calls, false for conditionals (BPred decides)
 *
 * update_btb flow
 * ───────────────
 *  Mirrors basic_btb::update_btb exactly, calling mbtb_* equivalents.
 */

#include "mbtb.h"
#include "instruction.h" // BRANCH_* constants

std::pair<champsim::address, bool>
mbtb::btb_prediction(champsim::address ip)
{
  auto result = direct.check_hit(ip);

  if (!result.valid)
    return {champsim::address{}, false};

  // ── Return: delegate to RAS (same as basic_btb) ─────────────────────────
  if (result.type == mbtb_direct_predictor::branch_info::RETURN)
    return ras.prediction();

  // ── Unresolved indirect: delegate to indirect predictor ─────────────────
  // (follow==false means the stability threshold has not been reached yet)
  if (result.type == mbtb_direct_predictor::branch_info::INDIRECT
      && !result.follow)
    return indirect.prediction(ip);

  // ── All other cases: use the target stored in the MB-BTB entry ──────────
  // always_taken is true for unconditionals and calls;
  // for conditionals (even chained ones) the branch predictor still decides.
  bool always_taken =
      (result.type == mbtb_direct_predictor::branch_info::UNCOND_DIRECT ||
       result.type == mbtb_direct_predictor::branch_info::CALL_DIRECT);

  return {result.target, always_taken};
}

void mbtb::update_btb(champsim::address ip,
                       champsim::address branch_target,
                       bool              taken,
                       uint8_t           branch_type)
{
  // ── RAS maintenance (identical to basic_btb) ────────────────────────────
  if (branch_type == BRANCH_DIRECT_CALL || branch_type == BRANCH_INDIRECT_CALL)
    ras.push(ip);

  if (branch_type == BRANCH_RETURN)
    ras.calibrate_call_size(branch_target);

  // ── Indirect target predictor (identical to basic_btb) ──────────────────
  if (branch_type == BRANCH_INDIRECT || branch_type == BRANCH_INDIRECT_CALL)
    indirect.update_target(ip, branch_target);

  if (branch_type == BRANCH_CONDITIONAL)
    indirect.update_direction(taken);

  // ── MB-BTB direct predictor update ──────────────────────────────────────
  direct.update(ip, branch_target, taken, branch_type);
}
