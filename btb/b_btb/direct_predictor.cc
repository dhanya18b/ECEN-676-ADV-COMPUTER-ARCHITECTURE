#include "direct_predictor.h"

#include "instruction.h"

auto direct_predictor::check_hit(champsim::address ip) -> std::optional<btb_entry_t>
{
  constexpr int BLOCK_BYTES = BLOCK_SIZE * 4;

  champsim::address block_ip = ip & ~(BLOCK_BYTES - 1);

  return BTB.check_hit({block_ip, champsim::address{}, branch_info::ALWAYS_TAKEN});
}



void direct_predictor::update(champsim::address ip,
                             champsim::address branch_target,
                             uint8_t branch_type)
{
  if (branch_target == champsim::address{})
    return;

  // classify branch type
  auto type = branch_info::ALWAYS_TAKEN;
  if ((branch_type == BRANCH_INDIRECT) || (branch_type == BRANCH_INDIRECT_CALL))
    type = branch_info::INDIRECT;
  else if (branch_type == BRANCH_RETURN)
    type = branch_info::RETURN;
  else if (branch_type == BRANCH_CONDITIONAL)
    type = branch_info::CONDITIONAL;

  constexpr int BLOCK_BYTES = BLOCK_SIZE * 4;

  // align to block
  champsim::address block_ip = ip & ~(BLOCK_BYTES - 1);

  // compute offset inside block
  uint8_t offset = static_cast<uint8_t>((ip - block_ip) / 4);

  // create new entry
  btb_entry_t entry;
  entry.ip_tag = block_ip;
  entry.valid = true;

  entry.num_insts = BLOCK_SIZE;
  entry.num_branches = 1;

  entry.branches[0] = {offset, branch_target, type};

  entry.fallthrough = block_ip + BLOCK_BYTES;

  // insert into BTB
  BTB.fill(entry);
}
