#ifndef BTB_BLOCK_BTB_DIRECT_PREDICTOR_H
#define BTB_BLOCK_BTB_DIRECT_PREDICTOR_H

#include <array>
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

  static constexpr int BLOCK_SIZE = 16; // instructions per block
  static constexpr int MAX_BRANCHES = 2;
  static constexpr int BLOCK_BYTES = BLOCK_SIZE * 4;

  struct btb_branch_slot {
    uint8_t offset = 0; // instruction index within block (0 .. BLOCK_SIZE-1)
    champsim::address target{};
    branch_info type = branch_info::ALWAYS_TAKEN;
    bool valid = false;
  };

  struct btb_entry_t {
    champsim::address ip_tag{}; // block-aligned PC
    bool valid = false;

    uint8_t num_insts = BLOCK_SIZE;
    uint8_t num_branches = 0;

    std::array<btb_branch_slot, MAX_BRANCHES> branches{};

    champsim::address fallthrough{};

    auto index() const
    {
      using namespace champsim::data::data_literals;
      return ip_tag.slice_upper<6_b>();
    }

    auto tag() const
    {
      using namespace champsim::data::data_literals;
      return ip_tag.slice_upper<6_b>();
    }
  };

  champsim::msl::lru_table<btb_entry_t> BTB{sets, ways};
  std::optional<btb_entry_t> check_hit(champsim::address ip);
  void update(champsim::address ip, champsim::address branch_target, uint8_t branch_type);
};

#endif
