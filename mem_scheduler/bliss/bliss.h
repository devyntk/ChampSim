//
// Created by devyn on 5/1/22.
//

#ifndef CHAMPSIM_BLISS_H
#define CHAMPSIM_BLISS_H

#include "dram_controller.h"
#include <array>
#include <numeric>
#include <algorithm>

class bliss_channel: public SPLIT_MEM_CHANNEL<BANK_REQUEST>
{
public:
  req_it get_new_active_request() override;
  PACKET & fill_bank_request() override;

//  std::array<bool, NUM_CPUS> is_blacklisted;
  uint32_t last_cpu = 0, requests_served = 0;
//  uint32_t clearing_count = 0;
//  uint32_t clearing_interval = 10000;
//  uint32_t blacklist_threshold = 4;
  void business_operate() override;
  bool operator()(const BANK_REQUEST& lhs, const BANK_REQUEST& rhs);

  bliss_channel() = default;
};

class bliss: public SPLIT_MEM_CONTROLLER<bliss_channel, BANK_REQUEST>
{
public:
  explicit bliss(double freq_scale) : SPLIT_MEM_CONTROLLER<bliss_channel, BANK_REQUEST>(freq_scale) {};


};

#endif // CHAMPSIM_BLISS_H
