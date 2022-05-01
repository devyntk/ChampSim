#ifndef fcfs_cc
#define fcfs_cc

#include "dram_controller.h"
#include <array>
#include <numeric>
#include <algorithm>

struct is_unscheduled {
  bool operator()(const PACKET& lhs) { return !lhs.scheduled; }
};
struct next_schedule : public invalid_is_maximal<PACKET, min_event_cycle<PACKET>, PACKET, is_unscheduled, is_unscheduled> {
};

class fcfs_channel: public SPLIT_MEM_CHANNEL<BANK_REQUEST>
{
  void initalize_channel(uint64_t current_cycle) override {};
  req_it get_new_active_request(uint64_t current_cycle) override {};
  PACKET & fill_bank_request() override {};
  void custom_operate() override {};
};

class fcfs: public SPLIT_MEM_CONTROLLER<fcfs_channel, BANK_REQUEST>
{
public:
  explicit fcfs(double freq_scale) : SPLIT_MEM_CONTROLLER<fcfs_channel, BANK_REQUEST>(freq_scale) {};

  // controller specific virtual methods
  void initalize_msched() override {};
  void cycle_operate() override {};
  void print_stats() override {};

};
#endif