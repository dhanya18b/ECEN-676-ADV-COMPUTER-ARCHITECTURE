#include "region_predictor.h"

#include "instruction.h"

/*
 * align_to_region
 * ---------------
 * Mask off the lower log2(REGION_SIZE) = 6 bits so that every PC within the
 * same 64-byte aligned block maps to the same region base address.
 */
champsim::address region_predictor::align_to_region(champsim::address ip)
{
  // Clear the lower 6 bits to get the 64B-aligned region base.
  auto raw = ip.to<unsigned long long>();
  raw &= ~static_cast<unsigned long long>(REGION_SIZE - 1);
  return champsim::address{raw};
}

/*
 * byte_offset
 * -----------
 * Return the byte offset of ip within its 64B region (0–63).
 */
uint8_t region_predictor::byte_offset(champsim::address ip)
{
  return static_cast<uint8_t>(ip.to<unsigned long long>() & (REGION_SIZE - 1));
}

/*
 * check_hit
 * ---------
 * Look up the R-BTB for the region containing ip.
 *
 * The R-BTB is indexed by the region-aligned PC, not the raw PC, so a single
 * lookup covers all instructions in the 64B region.  If the entry is found,
 * we return it so the caller can inspect which branch slots are relevant for
 * the actual (unaligned) fetch PC.
 */
auto region_predictor::check_hit(champsim::address ip) -> std::optional<btb_entry_t>
{
  champsim::address region_base = align_to_region(ip);
  // Build a probe key using the region-aligned address.
  btb_entry_t probe{};
  probe.ip_tag = region_base;
  return BTB.check_hit(probe);
}

/*
 * update
 * ------
 * Record a resolved branch in the R-BTB.
 *
 * Algorithm:
 *  1. Compute the region-aligned base for ip.
 *  2. Look up the existing entry for that region.
 *  3. Determine the branch_info type from ChampSim's branch_type constant.
 *  4. Search the existing slots for a matching offset.
 *     - If found: update target and type in place.
 *     - If not found: fill the first free slot; if all slots are full,
 *       evict the slot with the largest offset (LRU-by-position heuristic –
 *       the paper notes that many replacement policies can be used).
 *  5. Write the updated (or newly allocated) entry back into the LRU table.
 *
 * Note: not-taken conditional branches (branch_target == 0) are skipped
 * because the paper states that never-taken branches do not need BTB storage.
 */
void region_predictor::update(champsim::address ip, champsim::address branch_target, uint8_t branch_type)
{
  // Not-taken branches with no known target do not need a BTB entry.
  if (branch_target == champsim::address{} && branch_type == BRANCH_CONDITIONAL)
    return;

  // Map ChampSim branch type to our internal enum.
  auto type = branch_info::ALWAYS_TAKEN;
  if (branch_type == BRANCH_INDIRECT || branch_type == BRANCH_INDIRECT_CALL)
    type = branch_info::INDIRECT;
  else if (branch_type == BRANCH_RETURN)
    type = branch_info::RETURN;
  else if (branch_type == BRANCH_CONDITIONAL)
    type = branch_info::CONDITIONAL;

  champsim::address region_base = align_to_region(ip);
  uint8_t offset = byte_offset(ip);

  // Fetch the existing entry for this region (or start fresh).
  btb_entry_t entry{};
  auto opt_entry = check_hit(ip);
  if (opt_entry.has_value()) {
    entry = *opt_entry;
  } else {
    entry.ip_tag = region_base;
  }

  // Search for an existing slot at this offset.
  int match_idx = -1;
  int free_idx  = -1;
  for (int i = 0; i < static_cast<int>(NUM_BRANCH_SLOTS); ++i) {
    if (entry.slots[i].valid && entry.slots[i].offset == offset) {
      match_idx = i;
      break;
    }
    if (!entry.slots[i].valid && free_idx < 0)
      free_idx = i;
  }

  if (match_idx >= 0) {
    // Update the existing slot.
    entry.slots[match_idx].type = type;
    if (branch_target != champsim::address{})
      entry.slots[match_idx].target = branch_target;
  } else if (free_idx >= 0) {
    // Fill the first free slot.
    entry.slots[free_idx].valid  = true;
    entry.slots[free_idx].offset = offset;
    entry.slots[free_idx].type   = type;
    entry.slots[free_idx].target = branch_target;
  } else {
    /*
     * All slots are occupied.  Replace the slot with the highest byte offset
     * (furthest from the region start) – this keeps the "earlier" branches
     * whose slots tend to be reached first in the fetch stream.
     */
    int evict_idx = 0;
    for (int i = 1; i < static_cast<int>(NUM_BRANCH_SLOTS); ++i) {
      if (entry.slots[i].offset > entry.slots[evict_idx].offset)
        evict_idx = i;
    }
    entry.slots[evict_idx].valid  = true;
    entry.slots[evict_idx].offset = offset;
    entry.slots[evict_idx].type   = type;
    entry.slots[evict_idx].target = branch_target;
  }

  BTB.fill(entry);
}
