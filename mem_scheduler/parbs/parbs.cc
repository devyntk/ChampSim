#include "dram_controller.h"
#include "champsim_constants.h"
#include <array>

const int marking_cap = 5;
int total_marked_requests;

std::array<int, NUM_CPUS> reqs_per_thread;
std::array<int, NUM_CPUS*DRAM_RANKS*DRAM_BANKS> reqs_in_bank_per_thread_;

struct parbs_request{
  bool marked;
  union priority {
    int val;
    struct vals {
      bool marked : 1;
      bool row_hit : 1;
      int thread_rank: lg2(NUM_CPUS);
      int request_id;
    };
  };
  int thread_id;
  std::size_t open_row = std::numeric_limits<uint32_t>::max();
  uint64_t event_cycle = 0;
  std::vector<PACKET>::iterator pkt;
};

void parbs::initalize_msched() {}
void parbs::msched_cycle_operate() {}
void parbs::msched_channel_operate(DRAM_CHANNEL& channel) {
}
BANK_REQUEST* parbs::msched_get_request(DRAM_CHANNEL& channel) {
  return std::min_element(std::begin(channel.bank_request), std::end(channel.bank_request), min_event_cycle<BANK_REQUEST>());
}