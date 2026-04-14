#ifndef BTB_REGION_BTB_REGION_PREDICTOR_H
#define BTB_REGION_BTB_REGION_PREDICTOR_H

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "address.h"
#include "champsim.h"
#include "msl/lru_table.h"

struct region_predictor {
  /*
   * branch_info encodes the type of each branch slot in an R-BTB entry.
   * ALWAYS_TAKEN: unconditional direct branch (always taken)
   * CONDITIONAL:  sometimes-taken conditional branch
   * INDIRECT:     indirect branch (target predicted elsewhere)
   * RETURN:       function return (target from RAS)
   */
  enum class branch_info {
    INDIRECT,
    RETURN,
    ALWAYS_TAKEN,
    CONDITIONAL,
  };

  /*
   * Number of branch slots per R-BTB entry.
   * Each region entry can track up to NUM_BRANCH_SLOTS taken branches
   * within the 64-byte aligned region it covers.
   * Per the paper (Table 1 baseline), 2–3 slots are typical.
   */
  static constexpr std::size_t NUM_BRANCH_SLOTS = 2;

  /*
   * Region size in bytes. A 64B region covers one cache line.
   * All instructions within the same aligned 64B region share one BTB entry.
   */
  static constexpr std::size_t REGION_SIZE = 64;

  /*
   * R-BTB geometry: 512 sets × 6 ways = 3072 entries (mirrors I-BTB sizing
   * from the paper so comparisons are fair at iso-branch-slot count).
   */
  static constexpr std::size_t SETS = 512;
  static constexpr std::size_t WAYS = 6;

  /*
   * A single branch slot within an R-BTB entry.
   *   offset  – byte offset of the branch instruction within the 64B region
   *   target  – predicted branch target address
   *   type    – branch classification
   */
  struct branch_slot_t {
    uint8_t offset = 0;
    champsim::address target{};
    branch_info type = branch_info::ALWAYS_TAKEN;
    bool valid = false;
  };

  /*
   * One R-BTB entry covers an aligned 64-byte region of the instruction stream.
   * ip_tag holds the region-aligned base address used for indexing and tagging.
   * slots[] holds per-branch metadata for up to NUM_BRANCH_SLOTS taken branches
   * in the region.
   */
  struct btb_entry_t {
    champsim::address ip_tag{};                          // region-aligned base PC
    std::array<branch_slot_t, NUM_BRANCH_SLOTS> slots{}; // branch slots

    /*
     * Index and tag are derived from the upper bits of the region-aligned PC.
     * Lower log2(REGION_SIZE) bits are stripped because all addresses in the
     * same 64B region map to the same entry.
     */
    auto index() const
    {
      using namespace champsim::data::data_literals;
      // strip the lower 6 bits (log2(64) = 6) for region alignment
      return ip_tag.slice_upper<6_b>();
    }
    auto tag() const
    {
      using namespace champsim::data::data_literals;
      return ip_tag.slice_upper<6_b>();
    }
  };

  champsim::msl::lru_table<btb_entry_t> BTB{SETS, WAYS};

  /*
   * check_hit – look up the R-BTB with a (possibly unaligned) PC.
   * Returns the matching entry if the region that contains ip is cached.
   */
  std::optional<btb_entry_t> check_hit(champsim::address ip);

  /*
   * update – record a resolved branch outcome in the R-BTB.
   *   ip            – PC of the branch instruction
   *   branch_target – resolved target (0 if not-taken / unknown)
   *   branch_type   – ChampSim branch type constant
   */
  void update(champsim::address ip, champsim::address branch_target, uint8_t branch_type);

private:
  /*
   * align_to_region – return the 64B-aligned base address of the region
   * that contains ip.
   */
  static champsim::address align_to_region(champsim::address ip);

  /*
   * byte_offset – return the byte offset of ip within its 64B region.
   */
  static uint8_t byte_offset(champsim::address ip);
};

#endif
