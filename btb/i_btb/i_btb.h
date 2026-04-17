#ifndef BTB_INSTRUCTION_BTB_H
#define BTB_INSTRUCTION_BTB_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "address.h"
#include "champsim.h"
#include "modules.h"
#include "msl/lru_table.h"
#include "indirect_predictor.h"
#include "return_stack.h"

/*
 * Instruction BTB (I-BTB)
 * Based on: "Branch Target Buffer Organizations", Perais & Sheikh, MICRO '23
 *
 * One entry = one branch.
 * 16 banks = up to 16 fetch PCs/cycle conceptually.
 *
 * Total entry counts matched to paper Table 1 by dividing total sets across
 * banks:
 *   L1    (IBTB_LEVEL 1):  512 total sets / 16 banks = 32 sets/bank,  6 ways
 *   L2    (IBTB_LEVEL 2): 1024 total sets / 16 banks = 64 sets/bank, 13 ways
 *   Ideal (IBTB_LEVEL 0): 16384 total sets / 16 banks = 1024 sets/bank, 32 ways
 */

#ifndef IBTB_LEVEL
#define IBTB_LEVEL 1
#endif

#if IBTB_LEVEL == 0
inline constexpr std::size_t IBTB_SETS = 1024;
inline constexpr std::size_t IBTB_WAYS = 32;
#elif IBTB_LEVEL == 2
inline constexpr std::size_t IBTB_SETS = 64;
inline constexpr std::size_t IBTB_WAYS = 13;
#else // IBTB_LEVEL == 1 (default)
inline constexpr std::size_t IBTB_SETS = 32;
inline constexpr std::size_t IBTB_WAYS = 6;
#endif

inline constexpr std::size_t NUM_BANKS = 16;

struct ibtb_entry_t {
  enum class branch_info {
    INDIRECT,
    RETURN,
    ALWAYS_TAKEN,
    CONDITIONAL,
  };

  champsim::address ip_tag{};
  champsim::address target{};
  branch_info type = branch_info::ALWAYS_TAKEN;

  /*
   * index() selects the set; tag() identifies the entry within the set.
   * Both use slice_upper<2_b>() — matching direct_predictor in basic_btb —
   * which discards the 2 low alignment bits and uses the remaining upper bits.
   * champsim::msl::lru_table uses these two values independently: index() to
   * pick the set, tag() to match within it.
   */
  auto index() const
  {
    using namespace champsim::data::data_literals;
    return ip_tag.slice_upper<2_b>();
  }

  auto tag() const
  {
    using namespace champsim::data::data_literals;
    return ip_tag.slice_upper<2_b>();
  }
};

struct ibtb_bank {
  champsim::msl::lru_table<ibtb_entry_t> table{IBTB_SETS, IBTB_WAYS};

  std::optional<ibtb_entry_t> check_hit(champsim::address ip);
  void update(champsim::address ip, champsim::address target,
              ibtb_entry_t::branch_info type);
};

struct instruction_btb_core {
  std::array<ibtb_bank, NUM_BANKS> banks{};
  return_stack ras{};
  indirect_predictor indirect{};

  /*
   * FIX (Bug 2): bank_of() must use LOWER bits for good distribution.
   * Upper bits are nearly identical across branches in a program
   * (same page/segment), so hashing on them sends everything to the
   * same bank.  Lower bits after the 2-bit alignment offset vary per
   * instruction and spread evenly across NUM_BANKS = 16 banks.
   *
   * slice_lower<4_b>() gives 4 bits = values 0-15 = exactly 16 banks.
   */
  static std::size_t bank_of(champsim::address ip)
  {
    using namespace champsim::data::data_literals;
    return ip.slice_lower<4_b>().to<unsigned long long>() % NUM_BANKS;
  }

  std::pair<champsim::address, bool> predict(champsim::address ip);
  void update(champsim::address ip, champsim::address target,
              bool taken, uint8_t branch_type);

  static ibtb_entry_t::branch_info classify(uint8_t branch_type);
};

class instruction_btb : champsim::modules::btb
{
  instruction_btb_core core{};

public:
  using btb::btb;
  instruction_btb() : btb(nullptr) {}

  std::pair<champsim::address, bool> btb_prediction(champsim::address ip);
  void update_btb(champsim::address ip, champsim::address branch_target,
                  bool taken, uint8_t branch_type);
};

#endif
