#ifndef fcfs_h
#define fcfs_h

#include "dram_controller.h"
#include <array>
#include <numeric>
#include <algorithm>

class fcfs_channel: public SPLIT_MEM_CHANNEL<BANK_REQUEST>
{
  req_it get_new_active_request() override;
  PACKET & fill_bank_request() override;
};

class fcfs: public SPLIT_MEM_CONTROLLER<fcfs_channel, BANK_REQUEST>
{
public:
  explicit fcfs(double freq_scale) : SPLIT_MEM_CONTROLLER<fcfs_channel, BANK_REQUEST>(freq_scale) {};

};
#endif