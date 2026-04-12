/*
 * Instruction BTB (I-BTB) — implementation
 * Based on: "Branch Target Buffer Organizations", Perais & Sheikh, MICRO '23
 *
 * Drop-in replacement for basic_btb.cc.
 * Uses identical ChampSim components:
 *   indirect_predictor  — 4K-entry gshare indirect target predictor
 *   return_stack        — 64-entry RAS with call-size calibration
 *
 * All internal patterns (check_hit probe key, update flow, fill call)
 * mirror direct_predictor.cc exactly.
 */

#include "instruction_btb.h"
#include "instruction.h"    /* BRANCH_DIRECT_CALL, BRANCH_CONDITIONAL, … */

/* ═══════════════════════════════════════════════════════════════════════════
 * ibtb_bank
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * check_hit
 * ---------
 * Probe the LRU table for branch PC `ip`.
 *
 * The probe key is {ip, champsim::address{}, ALWAYS_TAKEN}.
 * lru_table matches on index() and tag(), both of which derive solely from
 * ip_tag (= the first constructor argument).  The dummy target and type
 * fields are never compared — they just satisfy the struct constructor.
 * This is exactly how direct_predictor::check_hit() works.
 */
std::optional<ibtb_entry_t> ibtb_bank::check_hit(champsim::address ip)
{
  return table.check_hit({ip, champsim::address{}, ibtb_entry_t::branch_info::ALWAYS_TAKEN});
}

/*
 * update
 * ------
 * Allocate a new entry or refresh an existing one, then promote to MRU.
 *
 * Pattern mirrors direct_predictor::update() from direct_predictor.cc:
 *   1. Probe for an existing entry with the real {ip, target, type} key.
 *   2. If found, refresh its type and target in-place.
 *   3. Call fill() with the updated (or new) entry — lru_table handles
 *      LRU eviction transparently when the set is full.
 *
 * Paper Section 3.5: "in I-BTB, allocating a new branch displaces another
 * one" — this is exactly what lru_table::fill() does.
 */
void ibtb_bank::update(champsim::address ip,
                        champsim::address target,
                        ibtb_entry_t::branch_info type)
{
  auto opt_entry = table.check_hit({ip, target, type});
  if (opt_entry.has_value()) {
    opt_entry->type = type;
    if (target != champsim::address{})
      opt_entry->target = target;
  }

  if (target != champsim::address{})
    table.fill(opt_entry.value_or(ibtb_entry_t{ip, target, type}));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * instruction_btb_core
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * classify
 * --------
 * Map ChampSim's branch_type byte → ibtb_entry_t::branch_info.
 * Enum value ordering matches direct_predictor::branch_info exactly.
 *
 *  BRANCH_INDIRECT / BRANCH_INDIRECT_CALL  →  INDIRECT
 *  BRANCH_RETURN                           →  RETURN
 *  BRANCH_CONDITIONAL                      →  CONDITIONAL
 *  BRANCH_DIRECT_JUMP / BRANCH_DIRECT_CALL →  ALWAYS_TAKEN
 */
ibtb_entry_t::branch_info instruction_btb_core::classify(uint8_t branch_type)
{
  using bi = ibtb_entry_t::branch_info;
  switch (branch_type) {
    case BRANCH_INDIRECT:
    case BRANCH_INDIRECT_CALL: return bi::INDIRECT;
    case BRANCH_RETURN:        return bi::RETURN;
    case BRANCH_CONDITIONAL:   return bi::CONDITIONAL;
    default:                   return bi::ALWAYS_TAKEN;
  }
}

/*
 * predict
 * -------
 * Look up `ip` in its bank and return {predicted_target, always_taken}.
 *
 * Mirrors basic_btb::btb_prediction() from basic_btb.cc:
 *   - miss              → {champsim::address{}, false}
 *   - RETURN hit        → ras.prediction()
 *   - INDIRECT hit      → indirect.prediction(ip)
 *   - CONDITIONAL hit   → {target, false}   (BPred decides direction)
 *   - ALWAYS_TAKEN hit  → {target, true}
 *
 * The `always_taken` bool (second return value) is false for CONDITIONAL
 * branches, matching basic_btb line 28:
 *   return {btb_entry->target,
 *           btb_entry->type != direct_predictor::branch_info::CONDITIONAL};
 */
std::pair<champsim::address, bool> instruction_btb_core::predict(champsim::address ip)
{
  using bi = ibtb_entry_t::branch_info;

  auto entry = banks[bank_of(ip)].check_hit(ip);

  if (!entry.has_value())
    return {champsim::address{}, false};

  if (entry->type == bi::RETURN)
    return ras.prediction();

  if (entry->type == bi::INDIRECT)
    return indirect.prediction(ip);

  return {entry->target, entry->type != bi::CONDITIONAL};
}

/*
 * update
 * ------
 * Called after a branch resolves.  Mirrors basic_btb::update_btb() exactly,
 * with one paper-mandated addition: never-taken conditional branches are NOT
 * allocated in the BTB (paper Section 2).
 *
 * basic_btb::update_btb() order (basic_btb.cc lines 33-47):
 *   1. RAS push for calls
 *   2. indirect.update_target for indirect branches
 *   3. indirect.update_direction for conditionals
 *   4. ras.calibrate_call_size for returns
 *   5. direct.update(ip, branch_target, branch_type)   ← always, unconditional
 *
 * We follow the same order.  Step 5 becomes banks[bank_of(ip)].update(),
 * guarded by the paper's never-taken-conditional rule.
 *
 * Paper Section 2: "never taken conditional branches do not require BTB
 * storage."  We skip BTB allocation for not-taken conditionals.
 * The RAS, indirect predictor, and history updates still happen first
 * (steps 1-4) because those are independent of BTB storage.
 */
void instruction_btb_core::update(champsim::address ip,
                                   champsim::address target,
                                   bool taken,
                                   uint8_t branch_type)
{
  /* Step 1: RAS push for calls (same as basic_btb line 34-35) */
  if (branch_type == BRANCH_DIRECT_CALL || branch_type == BRANCH_INDIRECT_CALL)
    ras.push(ip);

  /* Step 2: indirect target update (same as basic_btb lines 38-39) */
  if (branch_type == BRANCH_INDIRECT || branch_type == BRANCH_INDIRECT_CALL)
    indirect.update_target(ip, target);

  /* Step 3: conditional history update (same as basic_btb lines 41-42) */
  if (branch_type == BRANCH_CONDITIONAL)
    indirect.update_direction(taken);

  /* Step 4: RAS call-size calibration for returns (same as basic_btb 44-45) */
  if (branch_type == BRANCH_RETURN)
    ras.calibrate_call_size(target);

  /* Step 5: BTB allocation.
   *
   * Paper Section 2: never-taken conditional branches do not require BTB
   * storage — skip allocation.  All other branch types (unconditional jumps,
   * calls, returns, taken conditionals) always allocate.
   */
  if (branch_type == BRANCH_CONDITIONAL && !taken)
    return;

  banks[bank_of(ip)].update(ip, target, classify(branch_type));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * instruction_btb  (ChampSim module interface)
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * btb_prediction — ChampSim entry point (matches basic_btb.cc line 13)
 */
std::pair<champsim::address, bool> instruction_btb::btb_prediction(champsim::address ip)
{
  return core.predict(ip);
}

/*
 * update_btb — ChampSim entry point (matches basic_btb.cc line 31)
 */
void instruction_btb::update_btb(champsim::address ip,
                                  champsim::address branch_target,
                                  bool taken,
                                  uint8_t branch_type)
{
  core.update(ip, branch_target, taken, branch_type);
}
