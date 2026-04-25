#ifndef BTB_MBTB_RETURN_STACK_H
#define BTB_MBTB_RETURN_STACK_H

/*
 * mbtb_return_stack.h
 *
 * Return Address Stack (RAS) for the MultiBlock BTB.
 * Identical in behaviour to the basic_btb return_stack; kept as a separate
 * struct so the mbtb module has its own independent copy.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>

#include "address.h"
#include "champsim.h"

struct mbtb_return_stack {
  static constexpr std::size_t max_size             = 64;
  static constexpr std::size_t num_call_size_trackers = 1024;

  std::deque<champsim::address> stack;

  /*
   * Tracks the byte-size of call instructions so that
   *   return_target = call_ip + call_instr_size
   * can be computed correctly even when calls have variable encoding lengths.
   */
  std::array<typename champsim::address::difference_type,
             num_call_size_trackers> call_size_trackers;

  mbtb_return_stack()
  {
    std::fill(std::begin(call_size_trackers),
              std::end(call_size_trackers), 4);
  }

  std::pair<champsim::address, bool> prediction();
  void push(champsim::address ip);
  void calibrate_call_size(champsim::address branch_target);
};

#endif // BTB_MBTB_RETURN_STACK_H
