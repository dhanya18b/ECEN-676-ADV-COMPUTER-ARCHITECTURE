#include "mbtb_return_stack.h"

std::pair<champsim::address, bool> mbtb_return_stack::prediction()
{
  if (std::empty(stack))
    return {champsim::address{}, true};

  // Peek at the top of the RAS and adjust for the size of the call instruction.
  auto target = stack.back();
  auto size   = call_size_trackers[
      target.slice_lower<champsim::data::bits{
          champsim::msl::lg2(num_call_size_trackers)}>()
      .to<std::size_t>()];

  return {target + size, true};
}

void mbtb_return_stack::push(champsim::address ip)
{
  stack.push_back(ip);
  if (std::size(stack) > max_size)
    stack.pop_front();
}

void mbtb_return_stack::calibrate_call_size(champsim::address branch_target)
{
  if (!std::empty(stack)) {
    auto call_ip = stack.back();
    stack.pop_back();

    static int num_times_returned_backwards = 0;
    if (call_ip > branch_target && num_times_returned_backwards < 10) {
      ++num_times_returned_backwards;
      fmt::print("[MBTB] WARNING: return target is lower than call address. "
                 "This is usually a problem with your trace.\n");
    }

    auto estimated_call_instr_size =
        call_ip > branch_target
            ? champsim::uoffset(branch_target, call_ip)
            : champsim::uoffset(call_ip, branch_target);

    if (estimated_call_instr_size <= 10) {
      call_size_trackers[
          call_ip.slice_lower<champsim::data::bits{
              champsim::msl::lg2(num_call_size_trackers)}>()
          .to<std::size_t>()] = estimated_call_instr_size;
    }
  }
}
