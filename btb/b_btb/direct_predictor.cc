#include "direct_predictor.h"

#include "instruction.h"

auto direct_predictor::check_hit(champsim::address ip) -> std::optional<btb_entry_t>
{
  champsim::address block_ip = ip & ~(static_cast<unsigned long long>(BLOCK_BYTES) - 1);

  btb_entry_t probe{};
  probe.ip_tag = block_ip;
  return BTB.check_hit(probe);
}

void direct_predictor::update(champsim::address ip, champsim::address branch_target, uint8_t branch_type)
{
  if (branch_target == champsim::address{} && branch_type == BRANCH_CONDITIONAL)
    return;
  if (branch_target == champsim::address{})
    return;

  auto type = branch_info::ALWAYS_TAKEN;
  if ((branch_type == BRANCH_INDIRECT) || (branch_type == BRANCH_INDIRECT_CALL))
    type = branch_info::INDIRECT;
  else if (branch_type == BRANCH_RETURN)
    type = branch_info::RETURN;
  else if (branch_type == BRANCH_CONDITIONAL)
    type = branch_info::CONDITIONAL;

  champsim::address block_ip = ip & ~(static_cast<unsigned long long>(BLOCK_BYTES) - 1);
  uint8_t offset = static_cast<uint8_t>((ip - block_ip) / 4);

  btb_entry_t entry{};
  auto opt_entry = check_hit(ip);
  if (opt_entry.has_value()) {
    entry = *opt_entry;
  } else {
    entry.ip_tag = block_ip;
    entry.num_insts = BLOCK_SIZE;
    entry.fallthrough = block_ip + BLOCK_BYTES;
  }

  int match_idx = -1;
  int free_idx = -1;
  for (std::size_t i = 0; i < entry.branches.size(); ++i) {
    if (entry.branches[i].valid && entry.branches[i].offset == offset) {
      match_idx = static_cast<int>(i);
      break;
    }
    if (!entry.branches[i].valid && free_idx < 0)
      free_idx = static_cast<int>(i);
  }

  if (match_idx >= 0) {
    entry.branches[static_cast<std::size_t>(match_idx)].type = type;
    if (branch_target != champsim::address{})
      entry.branches[static_cast<std::size_t>(match_idx)].target = branch_target;
  } else if (free_idx >= 0) {
    auto& s = entry.branches[static_cast<std::size_t>(free_idx)];
    s.valid = true;
    s.offset = offset;
    s.type = type;
    s.target = branch_target;
  } else {
    int evict_idx = 0;
    for (int i = 1; i < MAX_BRANCHES; ++i) {
      if (entry.branches[static_cast<std::size_t>(i)].offset > entry.branches[static_cast<std::size_t>(evict_idx)].offset)
        evict_idx = i;
    }
    auto& s = entry.branches[static_cast<std::size_t>(evict_idx)];
    s.valid = true;
    s.offset = offset;
    s.type = type;
    s.target = branch_target;
  }

  uint8_t n = 0;
  for (const auto& s : entry.branches) {
    if (s.valid)
      ++n;
  }
  entry.num_branches = n;
  entry.valid = true;

  BTB.fill(entry);
}
