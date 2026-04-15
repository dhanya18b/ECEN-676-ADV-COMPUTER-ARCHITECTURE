#pragma once
#include <cstdint>

#define BLOCK_SIZE 16              // instructions per block
#define MAX_BRANCH_SLOTS 2         // configurable

struct BlockBTBBranch {
    uint8_t  offset;        // instruction offset in block
    uint64_t target;
    bool     is_conditional;
    bool     is_indirect;
};

struct BlockBTBEntry {
    uint64_t tag;
    bool     valid;

    uint8_t  num_insts;
    uint8_t  num_branches;

    BlockBTBBranch branches[MAX_BRANCH_SLOTS];

    uint64_t fallthrough;
};

class BlockBTB {
public:
    BlockBTB(int sets, int ways);

    BlockBTBEntry* lookup(uint64_t pc);
    void update(uint64_t pc, uint64_t target, bool taken,
                bool is_conditional, bool is_indirect);

private:
    int NUM_SETS;
    int NUM_WAYS;

    BlockBTBEntry **table;

    uint64_t get_index(uint64_t pc);
    uint64_t get_tag(uint64_t pc);
};
