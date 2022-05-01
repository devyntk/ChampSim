//
// Created by devyn on 5/1/22.
//
#include "util.h"
#include "champsim_constants.h"

#ifndef CHAMPSIM_DRAM_ADDR_H
#define CHAMPSIM_DRAM_ADDR_H

uint32_t dram_get_channel(uint64_t address);
uint32_t dram_get_bank(uint64_t address);
uint32_t dram_get_column(uint64_t address);
uint32_t dram_get_rank(uint64_t address);
uint32_t dram_get_row(uint64_t address);
uint32_t get_bank_addr(uint32_t addr);

#endif // CHAMPSIM_DRAM_ADDR_H
