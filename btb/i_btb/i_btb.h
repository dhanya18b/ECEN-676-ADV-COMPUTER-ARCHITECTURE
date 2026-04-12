#ifndef BTB_INSTRUCTION_BTB_H
#define BTB_INSTRUCTION_BTB_H

/*
 * Instruction BTB (I-BTB)
 * Based on: "Branch Target Buffer Organizations", Perais & Sheikh, MICRO '23
 *
 * Organization: one entry tracks exactly one branch (its PC, target, type).
 * Multiple banks let us produce up to NUM_BANKS fetch PCs per cycle.
 * Paper Section 3.1: "to generate multiple fetch PCs per cycle, the I-BTB
 * needs to be accessed multiple times … bank-interleaving."
 *
 * API matches the existing basic_btb / direct_predictor style exactly.
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

/* ── Sizing (mirrors Table 1 from the paper) ────────────────────────────
 *
 *  IBTB_LEVEL 1 → realistic L1BTB: 512 sets × 6 ways = 3 072 entries, 0-cycle
 *  IBTB_LEVEL 2 → realistic L2BTB: 1024 sets × 13 ways = 13 312 entries, 3-cycle
 *  IBTB_LEVEL 0 → idealistic: 16 384 sets × 32 ways ≈ 512 K entries, 0-cycle
 *
 *  NUM_BANKS = 16 replicates the "I-BTB 16" configuration from the paper,
 *  which uses 16 bank-interleaved arrays to supply up to 16 fetch PCs/cycle.
 */
#ifndef IBTB_LEVEL
#define IBTB_LEVEL 1
#endif

#if IBTB_LEVEL == 0
  inline constexpr std::size_t IBTB_SETS  = 16384;
  inline constexpr std::size_t IBTB_WAYS  = 32;
  inline constexpr bool        IBTB_IDEAL = true;
#elif IBTB_LEVEL == 2
  inline constexpr std::size_t IBTB_SETS  = 1024;
  inline constexpr std::size_t IBTB_WAYS  = 13;
  inline constexpr bool        IBTB_IDEAL = false;
#else  /* default: L1BTB */
  inline constexpr std::size_t IBTB_SETS  = 512;
  inline constexpr std::size_t IBTB_WAYS  = 6;
  inline constexpr bool        IBTB_IDEAL = false;
#endif

inline constexpr std::size_t NUM_BANKS = 16;

/* ── Entry type stored in each bank's lru_table ─────────────────────────── */
struct ibtb_entry_t {
  /* Branch type encoding — mirrors direct_predictor::branch_info so that
   * the same indirect_predictor and return_stack components can be reused. */
  enum class branch_info {
    ALWAYS_TAKEN,   /* unconditional direct / direct call                  */
    INDIRECT,       /* indirect jump or indirect call                       */
    RETURN,         /* return — target comes from RAS                       */
    CONDITIONAL,    /* conditional direct — direction from BPred            */
  };

  champsim::address ip_tag{};
  champsim::address target{};
  branch_info       type = branch_info::ALWAYS_TAKEN;

  /*
   * lru_table key interface.
   *
   * lru_table uses index() to select the set and tag() to identify the entry
   * within that set.  They must be DIFFERENT slices of ip_tag so that two
   * branches that map to the same set but have different PCs are not confused.
   *
   * ip_tag bit layout (64-bit address, 4-byte-aligned → bits [1:0] always 0):
   *
   *   bit 63 ──────────────────── bit 11 │ bit 10 ──── bit 2 │ bits 1:0
   *   ◄──────── tag (53 bits) ──────────►│◄── index (9 bits)─►│  (alignment)
   *
   * 9 index bits → 2^9 = 512 sets  ✓ (matches IBTB_SETS for L1BTB)
   *
   * Why not slice_upper for both (as direct_predictor does)?
   * direct_predictor has 1024 sets and ChampSim's lru_table modulos
   * index() down to [0, sets).  Using the same slice for tag() causes
   * two distinct branches in the same set to have identical tags, so
   * check_hit() can return a false positive for the wrong branch PC.
   * Separating the slices eliminates that collision entirely.
   */
  auto index() const {
    using namespace champsim::data::data_literals;
    return ip_tag.slice_lower<9_b>();   /* bits [10:2] → set selection */
  }
  auto tag() const {
    using namespace champsim::data::data_literals;
    return ip_tag.slice_upper<11_b>();  /* bits [63:11] → entry identification */
  }
};

/* ── One bank: a small set-associative LRU table ────────────────────────── */
/*
 * Paper Section 3.1: bank index = (ip >> 2) % NUM_BANKS for 4-byte-aligned
 * ISAs (ARMv8, RISC-V). Each bank holds IBTB_SETS × IBTB_WAYS entries.
 * The total number of cached branch targets equals
 *   NUM_BANKS × IBTB_SETS × IBTB_WAYS
 * which matches the paper's "number of cacheable branch targets" metric.
 */
struct ibtb_bank {
  champsim::msl::lru_table<ibtb_entry_t> table{IBTB_SETS, IBTB_WAYS};

  std::optional<ibtb_entry_t> check_hit(champsim::address ip);
  void update(champsim::address ip, champsim::address target,
              ibtb_entry_t::branch_info type);
};

/* ── Top-level I-BTB: NUM_BANKS banks + shared RAS + indirect predictor ─── */
struct instruction_btb_core {
  std::array<ibtb_bank, NUM_BANKS> banks{};
  return_stack     ras{};
  indirect_predictor indirect{};

  /* Map a PC to its bank. */
  static std::size_t bank_of(champsim::address ip) {
    /* lower 2 bits are always 0 for 4-byte-aligned ISAs; shift them away */
    return (ip.to<unsigned long long>() >> 2) % NUM_BANKS;
  }

  std::pair<champsim::address, bool> predict(champsim::address ip);
  void update(champsim::address ip, champsim::address target,
              bool taken, uint8_t branch_type);

  /* Classify ChampSim branch_type → ibtb_entry_t::branch_info */
  static ibtb_entry_t::branch_info classify(uint8_t branch_type);
};

/* ── ChampSim module class ──────────────────────────────────────────────── */
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
