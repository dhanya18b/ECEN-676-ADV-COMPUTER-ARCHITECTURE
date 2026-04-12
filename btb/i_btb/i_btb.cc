/*
 * Instruction BTB (I-BTB) — implementation
 * Based on: "Branch Target Buffer Organizations", Perais & Sheikh, MICRO '23
 *
 * Drop-in replacement for basic_btb.cc.  Uses the same:
 *   - champsim::address / champsim::msl::lru_table types
 *   - indirect_predictor  (4 K-entry gshare indirect target predictor)
 *   - return_stack        (64-entry RAS with call-size calibration)
 *   - btb_prediction / update_btb signatures
 */

#include "instruction_btb.h"
#include "instruction.h"   /* BRANCH_* constants */

/* ═══════════════════════════════════════════════════════════════════════════
 * ibtb_bank
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * check_hit
 * ---------
 * Looks up a branch PC in this bank's LRU table.
 * Returns the matching entry if found (hit), or std::nullopt (miss).
 *
 * Paper Section 3.1: each bank is accessed independently; a hit in bank b
 * means the branch at (ip) is stored in that bank's set-associative array.
 */
std::optional<ibtb_entry_t> ibtb_bank::check_hit(champsim::address ip)
{
  return table.check_hit({ip, champsim::address{}, ibtb_entry_t::branch_info::ALWAYS_TAKEN});
}

/*
 * update
 * ------
 * Allocates or refreshes an entry.  lru_table::fill handles LRU eviction
 * transparently — exactly one existing branch is displaced when the set is
 * full (paper Section 3.5: "in I-BTB, allocating a new branch displaces
 * another one").
 */
void ibtb_bank::update(champsim::address ip,
                        champsim::address target,
                        ibtb_entry_t::branch_info type)
{
  /*
   * FIX: check_hit must probe by IP (tag) only.
   *
   * Passing `target` or `type` into the probe key causes a miss whenever
   * the stored values differ from the resolved ones — e.g. an indirect
   * branch that retargets, or a branch whose type changed.  lru_table
   * matches solely on index() and tag(), both derived from ip_tag, so
   * the dummy target and default type here are ignored during matching.
   * This mirrors direct_predictor::check_hit() exactly.
   */
  auto existing = table.check_hit({ip, champsim::address{}, ibtb_entry_t::branch_info::ALWAYS_TAKEN});
  if (existing.has_value()) {
    /* Entry present: refresh target and type in-place. */
    existing->target = target;
    existing->type   = type;
  }
  /* fill() inserts a new entry or promotes the existing one to MRU.
   * When the set is full, lru_table evicts the LRU entry — exactly one
   * other branch is displaced (paper Section 3.5). */
  table.fill(existing.value_or(ibtb_entry_t{ip, target, type}));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * instruction_btb_core
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * classify
 * --------
 * Maps ChampSim's branch_type byte (instruction.h) to our internal enum.
 *
 *  BRANCH_DIRECT_JUMP   → ALWAYS_TAKEN
 *  BRANCH_DIRECT_CALL   → ALWAYS_TAKEN  (target stored; RAS push in update)
 *  BRANCH_INDIRECT      → INDIRECT
 *  BRANCH_INDIRECT_CALL → INDIRECT
 *  BRANCH_RETURN        → RETURN        (target from RAS)
 *  BRANCH_CONDITIONAL   → CONDITIONAL   (direction from BPred)
 */
ibtb_entry_t::branch_info instruction_btb_core::classify(uint8_t branch_type)
{
  using bi = ibtb_entry_t::branch_info;
  switch (branch_type) {
    case BRANCH_INDIRECT:
    case BRANCH_INDIRECT_CALL: return bi::INDIRECT;
    case BRANCH_RETURN:        return bi::RETURN;
    case BRANCH_CONDITIONAL:   return bi::CONDITIONAL;
    default:                   return bi::ALWAYS_TAKEN;  /* direct jump/call */
  }
}

/*
 * predict
 * -------
 * Looks up the branch PC in its assigned bank and returns
 * {predicted_target, always_taken}.
 *
 * Paper Section 3.1: each branch resides in exactly one bank
 * (bank_of(ip) = (ip >> 2) % NUM_BANKS).  A single lookup per branch PC
 * is performed; to generate N fetch PCs the caller loops over N sequential
 * instruction addresses and calls predict() on each.
 *
 * Return value matches basic_btb::btb_prediction convention:
 *   first  = predicted target address (champsim::address{} on miss)
 *   second = always_taken flag (false → conditional, direction from BPred)
 */
std::pair<champsim::address, bool> instruction_btb_core::predict(champsim::address ip)
{
  using bi = ibtb_entry_t::branch_info;

  auto& bank  = banks[bank_of(ip)];
  auto  entry = bank.check_hit(ip);

  /* BTB miss — no prediction for this PC. */
  if (!entry.has_value())
    return {champsim::address{}, false};

  /* Returns: use the Return Address Stack. */
  if (entry->type == bi::RETURN)
    return ras.prediction();

  /* Indirect: use the indirect target predictor (gshare over history). */
  if (entry->type == bi::INDIRECT)
    return indirect.prediction(ip);

  /*
   * Conditional direct: target is known (stored in BTB), but direction
   * comes from the branch predictor (caller).  Return always_taken=false
   * so the pipeline knows to consult BPred for direction.
   *
   * Paper Section 2: "never-taken conditional branches do not require BTB
   * storage" — they are filtered in update() below, so if we hit here the
   * branch was taken at least once.
   */
  if (entry->type == bi::CONDITIONAL)
    return {entry->target, false};

  /* ALWAYS_TAKEN: unconditional direct jump or call. */
  return {entry->target, true};
}

/*
 * update
 * ------
 * Called after a branch resolves.  Mirrors basic_btb::update_btb logic.
 *
 * Key decisions tied to the paper:
 *
 * 1. Never-taken conditional branches are NOT allocated (paper Section 2).
 *    A conditional branch only enters the BTB on its first taken execution.
 *
 * 2. Return entries do not store the target (it comes from the RAS), but
 *    we still allocate an entry so predict() knows to query the RAS.
 *
 * 3. Indirect branches store the target in the indirect_predictor, not in
 *    the BTB entry itself (same as basic_btb).
 *
 * 4. One entry per branch PC (paper Section 3.5).  lru_table evicts the
 *    LRU entry in the same set when a new branch needs space.
 */
void instruction_btb_core::update(champsim::address ip,
                                   champsim::address target,
                                   bool taken,
                                   uint8_t branch_type)
{
  using bi = ibtb_entry_t::branch_info;

  /* ── RAS management (same as basic_btb) ── */
  if (branch_type == BRANCH_DIRECT_CALL || branch_type == BRANCH_INDIRECT_CALL)
    ras.push(ip);

  if (branch_type == BRANCH_RETURN)
    ras.calibrate_call_size(target);

  /* ── Indirect predictor update ── */
  if (branch_type == BRANCH_INDIRECT || branch_type == BRANCH_INDIRECT_CALL)
    indirect.update_target(ip, target);

  /* Update global branch history for the indirect predictor. */
  if (branch_type == BRANCH_CONDITIONAL)
    indirect.update_direction(taken);

  /* ── BTB allocation guard ──
   * Paper Section 2: "never taken conditional direct branches do not
   * require BTB storage."  Skip allocation if this conditional branch
   * was not taken — it will not appear in the BTB and future lookups
   * will simply miss (treated as not-taken by the pipeline).
   */
  if (branch_type == BRANCH_CONDITIONAL && !taken)
    return;

  /* No target to store means nothing useful to cache. */
  if (target == champsim::address{})
    return;

  bi type = classify(branch_type);
  banks[bank_of(ip)].update(ip, target, type);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * instruction_btb  (ChampSim module interface)
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * btb_prediction
 * --------------
 * Called by ChampSim each cycle for the current fetch PC.
 * Delegates to instruction_btb_core::predict().
 *
 * Return value:
 *   {champsim::address{}, false}  → BTB miss, no prediction
 *   {target, false}               → conditional branch hit (BPred decides dir)
 *   {target, true}                → unconditional / always-taken hit
 */
std::pair<champsim::address, bool> instruction_btb::btb_prediction(champsim::address ip)
{
  return core.predict(ip);
}

/*
 * update_btb
 * ----------
 * Called by ChampSim after each branch resolves.
 * Delegates to instruction_btb_core::update().
 */
void instruction_btb::update_btb(champsim::address ip,
                                  champsim::address branch_target,
                                  bool taken,
                                  uint8_t branch_type)
{
  core.update(ip, branch_target, taken, branch_type);
}
