#ifndef BTB_INSTRUCTION_BTB_H
#define BTB_INSTRUCTION_BTB_H

/*
 * Instruction BTB (I-BTB)
 * Based on: "Branch Target Buffer Organizations", Perais & Sheikh, MICRO '23
 *
 * One entry = one branch (PC, target, type).
 * NUM_BANKS = 16 bank-interleaved arrays give up to 16 fetch PCs/cycle.
 * Paper Section 3.1: "to generate multiple fetch PCs per cycle, the I-BTB
 * needs to be accessed multiple times … bank-interleaving."
 *
 * All types, API signatures, and lru_table usage patterns match
 * the upstream ChampSim basic_btb / direct_predictor files exactly.
 */

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

/* ── Configuration ──────────────────────────────────────────────────────────
 *
 * Paper Table 1 defines two realistic configs and one idealistic config.
 * Entry counts in the paper are TOTALS across the whole BTB (all banks).
 * Per-bank sizing = total_sets / NUM_BANKS.
 *
 * IBTB_LEVEL 1  →  L1BTB  :  3 072 total  = 16 banks × 32 sets × 6 ways
 * IBTB_LEVEL 2  →  L2BTB  : 13 312 total  = 16 banks × 64 sets × 13 ways
 * IBTB_LEVEL 0  →  ideal  : 524 288 total = 16 banks × 1024 sets × 32 ways
 *
 * Derivation from paper Table 1:
 *   L1BTB: "512 sets / 6 ways" = 512 total sets across the unified BTB.
 *          Per bank = 512 / 16 = 32 sets.  32 × 6 × 16 = 3 072 entries ✓
 *   L2BTB: "1024 sets / 13 ways" total.
 *          Per bank = 1024 / 16 = 64 sets. 64 × 13 × 16 = 13 312 entries ✓
 *   ideal: 512K total ÷ (16 banks × 32 ways) = 1024 sets/bank ✓
 */
#ifndef IBTB_LEVEL
#define IBTB_LEVEL 1
#endif

#if IBTB_LEVEL == 0
  /* Idealistic: ~512K entries total, 0-cycle penalty (paper Section 5) */
  inline constexpr std::size_t IBTB_SETS = 1024;
  inline constexpr std::size_t IBTB_WAYS = 32;
#elif IBTB_LEVEL == 2
  /* Realistic L2BTB: 13 312 entries total, 3-cycle penalty (paper Table 1) */
  inline constexpr std::size_t IBTB_SETS = 64;
  inline constexpr std::size_t IBTB_WAYS = 13;
#else
  /* Realistic L1BTB: 3 072 entries total, 0-cycle penalty (paper Table 1) */
  inline constexpr std::size_t IBTB_SETS = 32;
  inline constexpr std::size_t IBTB_WAYS = 6;
#endif

/*
 * NUM_BANKS = 16  →  "I-BTB 16" configuration in the paper.
 * Paper Section 3.1: each bank is accessed independently to produce one
 * fetch PC; 16 banks allow up to 16 fetch PCs per cycle.
 */
inline constexpr std::size_t NUM_BANKS = 16;

/* ── BTB entry ──────────────────────────────────────────────────────────────
 *
 * Mirrors direct_predictor::btb_entry_t from direct_predictor.h exactly:
 *   - same field names  : ip_tag, target, type
 *   - same enum values  : INDIRECT, RETURN, ALWAYS_TAKEN, CONDITIONAL
 *                         (order matches direct_predictor.h)
 *   - same index()/tag(): both return ip_tag.slice_upper<2_b>()
 *
 * Why slice_upper<2_b> for BOTH index() and tag()?
 * ChampSim's lru_table uses index() for set selection (via internal modulo
 * against the table's set count) and tag() for within-set matching.
 * The upstream direct_predictor.h uses the identical slice for both — this
 * is the established contract for lru_table in this codebase. Diverging
 * from it (e.g. using different slices) breaks lru_table's assumptions.
 */
struct ibtb_entry_t {
  enum class branch_info {
    INDIRECT,      /* indirect jump / indirect call — target from ind. pred. */
    RETURN,        /* return — target from RAS                               */
    ALWAYS_TAKEN,  /* unconditional direct jump / direct call                */
    CONDITIONAL,   /* conditional direct — direction from BPred              */
  };

  champsim::address ip_tag{};
  champsim::address target{};
  branch_info       type = branch_info::ALWAYS_TAKEN;

  auto index() const
  {
    using namespace champsim::data::data_literals;
    return ip_tag.slice_upper<2_b>();   /* identical to direct_predictor.h */
  }
  auto tag() const
  {
    using namespace champsim::data::data_literals;
    return ip_tag.slice_upper<2_b>();   /* identical to direct_predictor.h */
  }
};

/* ── One bank ───────────────────────────────────────────────────────────────
 *
 * A single set-associative LRU table with IBTB_SETS sets and IBTB_WAYS ways.
 * Each branch lives in exactly one bank determined by bank_of().
 * Paper Section 3.1: bank-interleaving lets one access per bank per cycle
 * produce one fetch PC — 16 banks → up to 16 fetch PCs/cycle.
 */
struct ibtb_bank {
  champsim::msl::lru_table<ibtb_entry_t> table{IBTB_SETS, IBTB_WAYS};

  std::optional<ibtb_entry_t> check_hit(champsim::address ip);
  void update(champsim::address ip, champsim::address target,
              ibtb_entry_t::branch_info type);
};

/* ── Core BTB logic ─────────────────────────────────────────────────────────
 *
 * Aggregates all NUM_BANKS banks plus the same RAS and indirect predictor
 * components used by basic_btb (return_stack, indirect_predictor).
 */
struct instruction_btb_core {
  std::array<ibtb_bank, NUM_BANKS> banks{};
  return_stack       ras{};
  indirect_predictor indirect{};

  /*
   * bank_of: map a branch PC to its bank.
   *
   * Follows the same pattern as indirect_predictor.cc:
   *   ip.slice_upper<2_b>().to<unsigned long long>()
   * slice_upper<2_b>() strips the two low alignment bits (always 0 for
   * 4-byte-aligned ISAs) before the modulo, so consecutive instruction
   * addresses distribute evenly across banks rather than aliasing.
   */
  static std::size_t bank_of(champsim::address ip)
  {
    using namespace champsim::data::data_literals;
    return ip.slice_upper<2_b>().to<unsigned long long>() % NUM_BANKS;
  }

  std::pair<champsim::address, bool> predict(champsim::address ip);
  void update(champsim::address ip, champsim::address target,
              bool taken, uint8_t branch_type);

  static ibtb_entry_t::branch_info classify(uint8_t branch_type);
};

/* ── ChampSim module class ──────────────────────────────────────────────────
 *
 * Drop-in replacement for basic_btb.h.
 * Exposes identical btb_prediction() and update_btb() signatures.
 */
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

#endif /* BTB_INSTRUCTION_BTB_H */
