/*
 * Instruction BTB (I-BTB) baseline for ChampSim
 *
 * Key I-BTB behavior:
 *   - one branch per BTB entry
 *   - returns handled by RAS
 *   - indirect branches handled by indirect predictor
 *   - only taken branches allocate in the BTB
 */

#include "i-btb.h"

#include "instruction.h"

std::pair<champsim::address, bool> i_btb::btb_prediction(champsim::address ip)
{
  auto btb_entry = direct.check_hit(ip);

  if (!btb_entry.has_value())
    return {champsim::address{}, false};

  if (btb_entry->type == direct_predictor::branch_info::RETURN)
    return ras.prediction();

  if (btb_entry->type == direct_predictor::branch_info::INDIRECT)
    return indirect.prediction(ip);

  return {btb_entry->target, btb_entry->type != direct_predictor::branch_info::CONDITIONAL};
}

void i_btb::update_btb(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type)
{
  if (branch_type == BRANCH_DIRECT_CALL || branch_type == BRANCH_INDIRECT_CALL)
    ras.push(ip);

  if (branch_type == BRANCH_INDIRECT || branch_type == BRANCH_INDIRECT_CALL)
    indirect.update_target(ip, branch_target);

  if (branch_type == BRANCH_CONDITIONAL)
    indirect.update_direction(taken);

  if (branch_type == BRANCH_RETURN)
    ras.calibrate_call_size(branch_target);

  if (taken)
    direct.update(ip, branch_target, branch_type);
}