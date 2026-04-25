/*
 * mbtb_direct_predictor.cc
 *
 * MultiBlock BTB direct predictor implementation.
 * See mbtb_direct_predictor.h for a full design description.
 */

#include "mbtb_direct_predictor.h"
#include "instruction.h" // BRANCH_* constants

// ─── classify ────────────────────────────────────────────────────────────────
mbtb_direct_predictor::branch_info
mbtb_direct_predictor::classify(uint8_t branch_type_raw)
{
  switch (branch_type_raw) {
    case BRANCH_DIRECT_JUMP:   return branch_info::UNCOND_DIRECT;
    case BRANCH_DIRECT_CALL:   return branch_info::CALL_DIRECT;
    case BRANCH_CONDITIONAL:   return branch_info::CONDITIONAL;
    case BRANCH_INDIRECT:      return branch_info::INDIRECT;
    case BRANCH_INDIRECT_CALL: return branch_info::INDIRECT;
    case BRANCH_RETURN:        return branch_info::RETURN;
    default:                   return branch_info::CONDITIONAL;
  }
}

// ─── try_pull_target ─────────────────────────────────────────────────────────
// Pull the target block of a branch into the entry.
// Per §6.4.2: the last slot (index NUM_BRANCH_SLOTS-1) is never allowed to
// pull, to avoid divergent fall-through chains for multiple call sites.
void mbtb_direct_predictor::try_pull_target(btb_entry_t&      entry,
                                              std::size_t       slot_idx,
                                              champsim::address target)
{
  if (slot_idx >= NUM_BRANCH_SLOTS - 1) // last slot restriction
    return;

  auto& slot         = entry.slots[slot_idx];
  slot.follow        = true;
  slot.target        = target;
  // Approximate pulled-block byte-length as one full block.
  slot.cnt_at_target = static_cast<uint32_t>(BLOCK_INSNS * INSTR_BYTES);
}

// ─── split_entry ─────────────────────────────────────────────────────────────
// Split the entry at slot split_at.
// Slots [0 .. split_at] remain; higher slots are cleared.
// The explicit fall-through is set to the instruction after slot[split_at].
void mbtb_direct_predictor::split_entry(btb_entry_t& entry,
                                         std::size_t  split_at)
{
  entry.is_split = true;

  if (split_at < NUM_BRANCH_SLOTS) {
    const auto& last = entry.slots[split_at];
    entry.split_fallthrough =
        entry.ip_tag + static_cast<champsim::address::difference_type>(
                           last.offset + INSTR_BYTES);
  }

  // Invalidate slots beyond the split point.
  for (std::size_t i = split_at + 1; i < NUM_BRANCH_SLOTS; ++i)
    entry.slots[i] = branch_slot_t{};
}

// ─── check_hit ───────────────────────────────────────────────────────────────
mbtb_direct_predictor::prediction_t
mbtb_direct_predictor::check_hit(champsim::address ip)
{
  prediction_t result{};

  auto opt = BTB.check_hit({ip});
  if (!opt.has_value())
    return result; // miss

  const auto& entry = *opt;

  for (std::size_t i = 0; i < NUM_BRANCH_SLOTS; ++i) {
    const auto& slot = entry.slots[i];
    if (!slot.valid) continue;

    // Skip branches that are before the current fetch PC.
    if (slot.ip_tag < ip) continue;

    result.valid  = true;
    result.target = slot.target;
    result.type   = slot.type;
    result.follow = slot.follow;

    // If this slot has a chained block, expose its start address.
    if (slot.follow && slot.target != champsim::address{})
      result.chained_start = slot.target;

    break; // first reachable slot wins
  }

  return result;
}

// ─── update ──────────────────────────────────────────────────────────────────
void mbtb_direct_predictor::update(champsim::address ip,
                                    champsim::address branch_target,
                                    bool              taken,
                                    uint8_t           branch_type_raw)
{
  branch_info bt = classify(branch_type_raw);

  // Never-taken conditionals do not need BTB storage (paper §2).
  if (bt == branch_info::CONDITIONAL && !taken) return;
  // Returns are handled by the RAS.
  if (bt == branch_info::RETURN)               return;
  // Nothing to cache without a resolved target.
  if (branch_target == champsim::address{})    return;

  // ── Look for an existing entry that covers this IP ─────────────────────
  auto opt = BTB.check_hit({ip});

  if (opt.has_value()) {
    auto& entry = *opt;

    // Search for an existing slot matching this branch PC.
    bool found = false;
    for (std::size_t i = 0; i < NUM_BRANCH_SLOTS; ++i) {
      auto& slot = entry.slots[i];
      if (!slot.valid || slot.ip_tag != ip) continue;

      found          = true;
      slot.type      = bt;
      slot.target    = branch_target;

      // ── Indirect pull logic ─────────────────────────────────────────
      if (bt == branch_info::INDIRECT) {
        if (slot.target == branch_target) {
          if (slot.stabl_ctr < INDIRECT_STABILITY_THRESHOLD)
            ++slot.stabl_ctr;
          if (slot.stabl_ctr == INDIRECT_STABILITY_THRESHOLD)
            try_pull_target(entry, i, branch_target);
        } else {
          // Target changed → reset stability and un-chain.
          slot.stabl_ctr = 0;
          slot.follow    = false;
          slot.target    = branch_target;
        }
      }

      // ── Conditional pull logic ──────────────────────────────────────
      if (bt == branch_info::CONDITIONAL) {
        if (taken && !slot.follow)
          try_pull_target(entry, i, branch_target);
        else if (!taken && slot.follow) {
          // Downgrade: branch is no longer always taken.
          slot.follow        = false;
          slot.cnt_at_target = 0;
        }
      }

      // ── Unconditional / call always pull ───────────────────────────
      if (bt == branch_info::UNCOND_DIRECT || bt == branch_info::CALL_DIRECT)
        try_pull_target(entry, i, branch_target);

      BTB.fill(entry);
      break;
    }

    if (!found) {
      // Find a free slot in the existing entry.
      std::size_t free_idx = NUM_BRANCH_SLOTS; // sentinel = "none"
      for (std::size_t i = 0; i < NUM_BRANCH_SLOTS; ++i) {
        if (!entry.slots[i].valid) { free_idx = i; break; }
      }

      if (free_idx < NUM_BRANCH_SLOTS) {
        // ── Add branch to a free slot ─────────────────────────────────
        auto& slot      = entry.slots[free_idx];
        slot.valid      = true;
        slot.ip_tag     = ip;
        slot.type       = bt;
        slot.target     = branch_target;
        slot.follow     = false;
        slot.stabl_ctr  = 0;
        slot.blk_id     = 0;

        if (ip >= entry.ip_tag)
          slot.offset = static_cast<uint32_t>(
              champsim::uoffset(entry.ip_tag, ip));

        if (bt == branch_info::UNCOND_DIRECT || bt == branch_info::CALL_DIRECT)
          try_pull_target(entry, free_idx, branch_target);
        else if (bt == branch_info::CONDITIONAL && taken)
          try_pull_target(entry, free_idx, branch_target);

        BTB.fill(entry);

      } else {
        // ── All slots full → split at last slot, allocate new entry ──
        split_entry(entry, NUM_BRANCH_SLOTS - 1);
        BTB.fill(entry);

        btb_entry_t new_entry{};
        new_entry.ip_tag = ip;
        auto& slot       = new_entry.slots[0];
        slot.valid       = true;
        slot.ip_tag      = ip;
        slot.type        = bt;
        slot.target      = branch_target;
        slot.offset      = 0;
        slot.blk_id      = 0;
        slot.follow      = false;
        slot.stabl_ctr   = 0;

        if (bt == branch_info::UNCOND_DIRECT || bt == branch_info::CALL_DIRECT)
          try_pull_target(new_entry, 0, branch_target);
        else if (bt == branch_info::CONDITIONAL && taken)
          try_pull_target(new_entry, 0, branch_target);

        BTB.fill(new_entry);
      }

    }

  } else {
    // ── BTB miss: allocate a fresh entry ──────────────────────────────────
    btb_entry_t new_entry{};
    new_entry.ip_tag = ip;
    auto& slot       = new_entry.slots[0];
    slot.valid       = true;
    slot.ip_tag      = ip;
    slot.type        = bt;
    slot.target      = branch_target;
    slot.offset      = 0;
    slot.blk_id      = 0;
    slot.follow      = false;
    slot.stabl_ctr   = 0;

    if (bt == branch_info::UNCOND_DIRECT || bt == branch_info::CALL_DIRECT)
      try_pull_target(new_entry, 0, branch_target);
    else if (bt == branch_info::CONDITIONAL && taken)
      try_pull_target(new_entry, 0, branch_target);

    BTB.fill(new_entry);
  }
}
