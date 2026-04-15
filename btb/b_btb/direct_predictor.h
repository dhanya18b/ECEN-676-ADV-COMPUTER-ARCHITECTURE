#ifndef BTB_BASIC_BTB_DIRECT_PREDICTOR_H
#define BTB_BASIC_BTB_DIRECT_PREDICTOR_H

#include <cstdint>
#include <optional>

#include "address.h"
#include "champsim.h"
#include "msl/lru_table.h"

struct direct_predictor {
  enum class branch_info {
    INDIRECT,
    RETURN,
    ALWAYS_TAKEN,
    CONDITIONAL,
  };

  static constexpr std::size_t sets = 1024;
  static constexpr std::size_t ways = 8;

static constexpr int BLOCK_SIZE = 16;        // instructions
static constexpr int MAX_BRANCHES = 2;

struct btb_branch_slot {
  uint8_t offset;                  // instruction offset in block
  champsim::address target{};
  branch_info type;
};

struct btb_entry_t {
  champsim::address ip_tag{};      // block-aligned PC
  bool valid = false;

  uint8_t num_insts = BLOCK_SIZE;
  uint8_t num_branches = 0;

  btb_branch_slot branches[MAX_BRANCHES];

  champsim::address fallthrough{};

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
 
  champsim::msl::lru_table<btb_entry_t> BTB{sets, ways};
  std::optional<btb_entry_t> check_hit(champsim::address ip);
  void update(champsim::address ip,
            champsim::address branch_target,
            uint8_t branch_type);
};

#endif
