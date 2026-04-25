#ifndef BTB_MBTB_DIRECT_PREDICTOR_H
#define BTB_MBTB_DIRECT_PREDICTOR_H

/*
 * mbtb_direct_predictor.h
 *
 * MultiBlock BTB direct predictor — the heart of the MB-BTB design.
 *
 * Based on: "Branch Target Buffer Organizations" (Perais & Sheikh, MICRO '23)
 * Sections 6.3 (entry splitting) and 6.4 (MultiBlock BTB).
 *
 * How it differs from basic_btb's direct_predictor
 * ─────────────────────────────────────────────────
 * basic_btb::direct_predictor stores ONE branch per entry (I-BTB style).
 * mbtb_direct_predictor stores up to NUM_BRANCH_SLOTS branches per entry
 * (B-BTB style) AND chains the target block of qualifying branches into the
 * same entry (MB-BTB extension).
 *
 * Entry layout (matches Figure 6 of the paper)
 * ─────────────────────────────────────────────
 *  ┌─────────┬────────────────────────────────────────────────────────┐
 *  │  Tag    │  slot[0]: type | offset | target | blk_id | cnt_at_tgt │
 *  │ (Split) │               │ follow | stabl_ctr                      │
 *  │         │  slot[1]: ...                                           │
 *  └─────────┴────────────────────────────────────────────────────────┘
 *
 * Pull eligibility (AllBr mode — best config per paper §6.5.2)
 * ─────────────────────────────────────────────────────────────
 *  • UNCOND_DIRECT  → always pull (br_follow = true immediately)
 *  • CALL_DIRECT    → always pull (CallDir / AllBr)
 *  • CONDITIONAL    → pull immediately when first seen taken
 *                     (downgrade if later seen not-taken)
 *  • INDIRECT       → pull after INDIRECT_STABILITY_THRESHOLD consecutive
 *                     same-target observations; reset on target change
 *  • RETURN         → never pull (handled by RAS)
 *
 * Splitting (§6.3)
 * ─────────────────
 * When all NUM_BRANCH_SLOTS are occupied and a new taken branch arrives,
 * the entry is split at the last slot.  The split entry records an explicit
 * fall-through address (cannot be computed as IP + block_size in parallel).
 *
 * Last-slot restriction (§6.4.2)
 * ────────────────────────────────
 * The last (NUM_BRANCH_SLOTS-1) slot is NEVER allowed to pull its target,
 * to avoid creating divergent fall-through chains for different call sites
 * of the same function.
 */

#include <array>
#include <cstdint>
#include <optional>

#include "address.h"
#include "champsim.h"
#include "msl/lru_table.h"

struct mbtb_direct_predictor {

  // ── Configuration ────────────────────────────────────────────────────────
  static constexpr std::size_t sets             = 512;   // L1 BTB sets
  static constexpr std::size_t ways             = 6;     // L1 BTB ways
  static constexpr std::size_t NUM_BRANCH_SLOTS = 2;     // branch slots / entry
  static constexpr std::size_t BLOCK_INSNS      = 16;    // max instructions / block
  static constexpr std::size_t INSTR_BYTES      = 4;     // ARMv8 fixed width

  // Stability threshold: indirect branch must hit same target this many times
  // before its target block is pulled into the entry (paper: 63).
  static constexpr uint8_t INDIRECT_STABILITY_THRESHOLD = 63;

  // ── Branch type classification ───────────────────────────────────────────
  enum class branch_info : uint8_t {
    UNCOND_DIRECT,  // unconditional direct jump / always-taken
    CALL_DIRECT,    // direct call
    CONDITIONAL,    // conditional branch
    INDIRECT,       // indirect jump or indirect call
    RETURN,         // return (RAS handles prediction)
  };

  // ── Per-slot metadata (Figure 6 of the paper) ───────────────────────────
  struct branch_slot_t {
    champsim::address ip_tag{};           // absolute PC of this branch
    champsim::address target{};           // predicted target address
    uint32_t          offset        = 0;  // byte offset within its block
    uint8_t           blk_id        = 0;  // which chained block (0 = first)
    uint32_t          cnt_at_target = 0;  // byte-length of pulled target block
    branch_info       type          = branch_info::CONDITIONAL;
    bool              follow        = false; // target block chained?
    uint8_t           stabl_ctr     = 0;    // stability counter (indirect only)
    bool              valid         = false;
  };

  // ── BTB entry ────────────────────────────────────────────────────────────
  struct btb_entry_t {
    champsim::address ip_tag{};     // PC of the start of the first block
    bool is_split             = false;
    champsim::address split_fallthrough{}; // explicit fall-through when split

    std::array<branch_slot_t, NUM_BRANCH_SLOTS> slots{};

    // LRU table helpers
    auto index() const {
      using namespace champsim::data::data_literals;
      return ip_tag.slice_upper<2_b>();
    }
    auto tag() const {
      using namespace champsim::data::data_literals;
      return ip_tag.slice_upper<2_b>();
    }
  };

  // ── Prediction result ────────────────────────────────────────────────────
  struct prediction_t {
    champsim::address target{};
    branch_info       type         = branch_info::CONDITIONAL;
    bool              follow       = false; // chained block available?
    champsim::address chained_start{};      // start PC of chained block
    bool              valid        = false; // BTB hit?
  };

  // ── Storage ──────────────────────────────────────────────────────────────
  champsim::msl::lru_table<btb_entry_t> BTB{sets, ways};

  // ── Public interface ─────────────────────────────────────────────────────

  /**
   * check_hit
   *   Look up the MB-BTB for fetch PC @ip.
   *   Returns the winning prediction_t, or an empty one on miss.
   */
  prediction_t check_hit(champsim::address ip);

  /**
   * update
   *   Called on branch resolution.  @branch_type_raw uses ChampSim BRANCH_*.
   */
  void update(champsim::address ip,
              champsim::address branch_target,
              bool              taken,
              uint8_t           branch_type_raw);

  // ── Helpers ──────────────────────────────────────────────────────────────
  static branch_info classify(uint8_t branch_type_raw);

private:
  void try_pull_target(btb_entry_t& entry,
                       std::size_t  slot_idx,
                       champsim::address target);

  void split_entry(btb_entry_t& entry, std::size_t split_at);
};

#endif // BTB_MBTB_DIRECT_PREDICTOR_H
