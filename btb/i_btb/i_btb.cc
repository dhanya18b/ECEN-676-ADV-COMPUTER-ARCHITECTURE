#include "i_btb.h"       // FIX (Bug 1): was "instruction_btb.h" — must match actual filename
#include "instruction.h"

std::optional<ibtb_entry_t> ibtb_bank::check_hit(champsim::address ip)
{
  return table.check_hit({ip, champsim::address{}, ibtb_entry_t::branch_info::ALWAYS_TAKEN});
}

void ibtb_bank::update(champsim::address ip,
                       champsim::address target,
                       ibtb_entry_t::branch_info type)
{
  /*
   * FIX (Bug 4): check_hit() returns a VALUE COPY — mutating opt_entry does
   * NOT write back to the table.  The original code modified the local copy
   * and lost the changes.
   *
   * Fix: build the entry we want to write (updating fields if it already
   * existed), then always call table.fill() so the change is committed.
   */
  if (target == champsim::address{})
    return;

  // Probe by IP only (dummy target/type for the lookup key)
  auto opt_entry = table.check_hit({ip, champsim::address{}, ibtb_entry_t::branch_info::ALWAYS_TAKEN});

  // Start from the existing entry (if any), otherwise a fresh one
  ibtb_entry_t to_fill = opt_entry.value_or(ibtb_entry_t{ip, target, type});
  to_fill.type   = type;
  to_fill.target = target;

  table.fill(to_fill);  // always write back
}

ibtb_entry_t::branch_info instruction_btb_core::classify(uint8_t branch_type)
{
  using bi = ibtb_entry_t::branch_info;

  switch (branch_type) {
    case BRANCH_INDIRECT:
    case BRANCH_INDIRECT_CALL:
      return bi::INDIRECT;

    case BRANCH_RETURN:
      return bi::RETURN;

    case BRANCH_CONDITIONAL:
      return bi::CONDITIONAL;

    default:
      return bi::ALWAYS_TAKEN;
  }
}

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

void instruction_btb_core::update(champsim::address ip,
                                  champsim::address target,
                                  bool taken,
                                  uint8_t branch_type)
{
  if (branch_type == BRANCH_DIRECT_CALL || branch_type == BRANCH_INDIRECT_CALL)
    ras.push(ip);

  if (branch_type == BRANCH_INDIRECT || branch_type == BRANCH_INDIRECT_CALL)
    indirect.update_target(ip, target);

  if (branch_type == BRANCH_CONDITIONAL)
    indirect.update_direction(taken);

  if (branch_type == BRANCH_RETURN)
    ras.calibrate_call_size(target);

  /*
   * Paper rule (Section 2):
   * Never-taken conditional branches do not require BTB storage.
   */
  if (branch_type == BRANCH_CONDITIONAL && !taken)
    return;

  banks[bank_of(ip)].update(ip, target, classify(branch_type));
}

std::pair<champsim::address, bool> instruction_btb::btb_prediction(champsim::address ip)
{
  return core.predict(ip);
}

void instruction_btb::update_btb(champsim::address ip,
                                 champsim::address branch_target,
                                 bool taken,
                                 uint8_t branch_type)
{
  core.update(ip, branch_target, taken, branch_type);
}
