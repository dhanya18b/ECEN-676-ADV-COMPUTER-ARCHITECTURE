#include "btb_block.h"
#include <cstdlib>

#define LOG2_BLOCK_SIZE 4   // 16 instructions
#define INST_SIZE 4         // assume 4B ISA

BlockBTB::BlockBTB(int sets, int ways) {
    NUM_SETS = sets;
    NUM_WAYS = ways;

    table = new BlockBTBEntry*[NUM_SETS];
    for (int i = 0; i < NUM_SETS; i++) {
        table[i] = new BlockBTBEntry[NUM_WAYS];
        for (int j = 0; j < NUM_WAYS; j++) {
            table[i][j].valid = false;
        }
    }
}

uint64_t BlockBTB::get_index(uint64_t pc) {
    return (pc >> LOG2_BLOCK_SIZE) % NUM_SETS;
}

uint64_t BlockBTB::get_tag(uint64_t pc) {
    return pc >> (LOG2_BLOCK_SIZE);
}
