#include "dram_controller.h"
#include "util.h"
#include "champsim_constants.h"

uint32_t dram_get_channel(uint64_t address)
{
  int shift = LOG2_BLOCK_SIZE;
  return (address >> shift) & bitmask(lg2(DRAM_CHANNELS));
}

uint32_t dram_get_bank(uint64_t address)
{
  int shift = lg2(DRAM_CHANNELS) + LOG2_BLOCK_SIZE;
  return (address >> shift) & bitmask(lg2(DRAM_BANKS));
}

uint32_t dram_get_column(uint64_t address)
{
  int shift = lg2(DRAM_BANKS) + lg2(DRAM_CHANNELS) + LOG2_BLOCK_SIZE;
  return (address >> shift) & bitmask(lg2(DRAM_COLUMNS));
}

uint32_t dram_get_rank(uint64_t address)
{
  int shift = lg2(DRAM_BANKS) + lg2(DRAM_COLUMNS) + lg2(DRAM_CHANNELS) + LOG2_BLOCK_SIZE;
  return (address >> shift) & bitmask(lg2(DRAM_RANKS));
}

uint32_t dram_get_row(uint64_t address)
{
  int shift = lg2(DRAM_RANKS) + lg2(DRAM_BANKS) + lg2(DRAM_COLUMNS) + lg2(DRAM_CHANNELS) + LOG2_BLOCK_SIZE;
  return (address >> shift) & bitmask(lg2(DRAM_ROWS));
}

uint32_t get_bank_addr(uint32_t addr) {return dram_get_rank(addr) * DRAM_BANKS + dram_get_bank(addr);}