/*
 * block_btb.cc – Block BTB prediction and update
 * -----------------------------------------------
 * Mirrors region_btb.cc: after a hit on the block-aligned entry, select the
 * first valid branch slot whose instruction offset is >= the fetch PC's
 * offset within the block (Section 3.6.1-style fetch-PC vs slot offset).
 */

#include "block_btb.h"

#include "instruction.h"

std::pair<champsim::address, bool> block_btb::btb_prediction(champsim::address ip)
{
  auto opt_entry = direct.check_hit(ip);

  if (!opt_entry.has_value())
    return {champsim::address{}, false};

  constexpr auto BLOCK_BYTES = direct_predictor::BLOCK_BYTES;
  champsim::address block_ip = ip & ~(static_cast<unsigned long long>(BLOCK_BYTES) - 1);
  uint8_t ip_instr_off = static_cast<uint8_t>((ip - block_ip) / 4);

  const direct_predictor::btb_branch_slot* best = nullptr;
  for (const auto& slot : opt_entry->branches) {
    if (!slot.valid)
      continue;
    if (slot.offset < ip_instr_off)
      continue;
    if (best == nullptr || slot.offset < best->offset)
      best = &slot;
  }

  if (best == nullptr)
    return {champsim::address{}, false};

  if (best->type == direct_predictor::branch_info::RETURN)
    return ras.prediction();

  if (best->type == direct_predictor::branch_info::INDIRECT)
    return indirect.prediction(block_ip + static_cast<unsigned long long>(best->offset) * 4ULL);

  return {best->target, best->type != direct_predictor::branch_info::CONDITIONAL};
}

void block_btb::update_btb(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type)
{
  if (branch_type == BRANCH_DIRECT_CALL || branch_type == BRANCH_INDIRECT_CALL)
    ras.push(ip);

  if ((branch_type == BRANCH_INDIRECT) || (branch_type == BRANCH_INDIRECT_CALL))
    indirect.update_target(ip, branch_target);

  if (branch_type == BRANCH_CONDITIONAL)
    indirect.update_direction(taken);

  if (branch_type == BRANCH_RETURN)
    ras.calibrate_call_size(branch_target);

  direct.update(ip, branch_target, branch_type);
}
