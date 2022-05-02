#include "atlas.h"
#include <array>
#include <numeric>
#include <algorithm>

const int quantum_length = 10000000; //cycles
const double alpha = 0.875;
const int T = 100000;

int quantum_cycles;

std::array<int, NUM_CPUS> per_thread_as;
std::array<int, NUM_CPUS> per_thread_total_as;
std::array<int, NUM_CPUS> thread_rank;

extern MEMORY_CONTROLLER DRAM;

bool is_row_hit(const PACKET& packet) {
  uint32_t op_rank = DRAM.dram_get_rank(packet.address), op_bank = DRAM.dram_get_bank(packet.address),
           op_row = DRAM.dram_get_row(packet.address), op_chn = DRAM.dram_get_channel(packet.address);
  return DRAM.channels[op_chn].bank_request[op_rank * DRAM_BANKS + op_bank].open_row == op_row;
}

bool over_threshold(const PACKET& packet){
  return (packet.event_cycle+T > DRAM.current_cycle);
}

struct bliss_event_cycle {
  bool operator()(const PACKET& lhs, const PACKET& rhs) {
    if (over_threshold(lhs) != over_threshold(rhs))
      return over_threshold(lhs) > over_threshold(rhs);
    if (lhs.cpu != rhs.cpu)
      return thread_rank[lhs.cpu] > thread_rank[rhs.cpu];
    if (is_row_hit(lhs) != is_row_hit(rhs))
      return is_row_hit(lhs) > is_row_hit(rhs);
    return lhs.event_cycle < rhs.event_cycle;
  }
};
struct is_unscheduled {
  bool operator()(const PACKET& lhs) { return !lhs.scheduled; }
};
struct next_schedule : public invalid_is_maximal<PACKET, invalid_is_maximal<PACKET, bliss_event_cycle>, PACKET, is_unscheduled, is_unscheduled> {
};

void atlas::initalize_msched() {}
void atlas::msched_cycle_operate() {
  quantum_cycles++;


  if (quantum_cycles >= quantum_length) {
    // end of quantum, update TotalAS
    for(long unsigned i=0; i<NUM_CPUS; i++) {
      per_thread_total_as.at(i) = alpha*(per_thread_total_as.at(i)) + (1-alpha)*per_thread_as[i];
      per_thread_as.at(i) = 0;
    }
    // got the total AS for all threads, create an order of them now
    std::iota(thread_rank.begin(),thread_rank.end(), 0);
    std::sort( thread_rank.begin(),thread_rank.end(), [&](int i,int j){return per_thread_total_as[i]<per_thread_total_as[j];} );
  }
}
void atlas::add_packet(std::vector<PACKET>::iterator packet, DRAM_CHANNEL& channel, bool is_write){
  if (is_valid<PACKET>()(*packet) && packet->event_cycle <= current_cycle) {
    uint32_t op_rank = dram_get_rank(packet->address), op_bank = dram_get_bank(packet->address),
             op_row = dram_get_row(packet->address);

    auto op_idx = op_rank * DRAM_BANKS + op_bank;

    if (!channel.bank_request[op_idx].valid) {
      bool row_buffer_hit = (channel.bank_request[op_idx].open_row == op_row);

      // this bank is now busy
      channel.bank_request[op_idx] = {true, is_write, row_buffer_hit, op_row, current_cycle + tCAS + (row_buffer_hit ? 0 : tRP + tRCD), packet};

      packet->scheduled = true;
      packet->event_cycle = std::numeric_limits<uint64_t>::max();
      auto req = std::begin(channel.bank_request);
      std::advance(req, op_idx);
    }
  }

}
void atlas::msched_channel_operate(std::array<DRAM_CHANNEL, DRAM_CHANNELS> ::iterator channel_it) {
  DRAM_CHANNEL& channel = *channel_it;
  // parse the read and write queues and add to our own channel ATLAS queue
  for (auto it = std::begin(channel.RQ); it != std::end(channel.RQ); it++){
    add_packet(it, channel, false);
  }
  for (auto it = std::begin(channel.WQ); it != std::end(channel.WQ); it++){
    add_packet(it, channel, true);
  }

  if (channel.active_request != std::end(channel.bank_request)) {
    // part of a memory episode, increase attained service for this thread (cpu)
    per_thread_as[channel.active_request->pkt->cpu] += 1;
  }
}
bool atlas_sorter(BANK_REQUEST const& rhs, BANK_REQUEST const& lhs) {
  bliss_event_cycle cmp;
  return cmp(*rhs.pkt, *lhs.pkt);
}
BANK_REQUEST* atlas::msched_get_request(std::array<DRAM_CHANNEL, DRAM_CHANNELS> ::iterator channel_it) {
  DRAM_CHANNEL& channel = *channel_it;
  auto new_req = std::min_element(std::begin(channel.bank_request),std::end(channel.bank_request), atlas_sorter);

  // check if we're switching read/write mode, if so, add penalty
  if (new_req->is_write != channel.write_mode) {
    // Add data bus turn-around time
    if (channel.active_request != std::end(channel.bank_request))
      channel.dbus_cycle_available = channel.active_request->event_cycle + DRAM_DBUS_TURN_AROUND_TIME; // After ongoing finish
    else
      channel.dbus_cycle_available = current_cycle + DRAM_DBUS_TURN_AROUND_TIME;
    // Set the channel mode
    channel.write_mode = new_req->is_write;
  }

  return new_req;
}