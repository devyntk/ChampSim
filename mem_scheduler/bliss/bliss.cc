#include "bliss.h"
#include <array>
#include <numeric>
#include <algorithm>

int last_cpu_req = NUM_CPUS;
int req_served = 0 ;
const int blacklist_threshold = 4;
std::array<bool, NUM_CPUS> is_blacklisted;
const int clearing_interval = 10000;
int clearing_count = 0;

void bliss::initalize_msched() {}
void bliss::msched_cycle_operate() {
  clearing_count++;
  if (clearing_count >= clearing_interval){
    for(long unsigned int i = 0; i < NUM_CPUS; i++)
      is_blacklisted[i] = false;
  }
}
void bliss::add_packet(std::vector<PACKET>::iterator packet, DRAM_CHANNEL& channel, bool is_write){
  MEMORY_CONTROLLER::add_packet(packet, channel, is_write);
}
void bliss::msched_channel_operate(std::array<DRAM_CHANNEL, DRAM_CHANNELS> ::iterator channel_it) {
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
    int cpu = channel.active_request->pkt->cpu;
    if (cpu == last_cpu_req) {
      req_served++;
      // check if the process that we're increasing needs to be blacklisted
      if (req_served > blacklist_threshold){
        is_blacklisted[cpu] = true;
        req_served = 0;
      }
    } else {
      // different application, reset trackers
      req_served = 0;
      last_cpu_req = cpu;
    }
  }
}
bool bliss_sorter(BANK_REQUEST const& rhs, BANK_REQUEST const& lhs) {
  bool lhs_blacklisted = is_blacklisted[lhs.pkt->cpu];
  bool rhs_blacklisted = is_blacklisted[rhs.pkt->cpu];
  if (lhs_blacklisted != rhs_blacklisted) {
    return lhs_blacklisted < rhs_blacklisted;
  }
  if (lhs.row_buffer_hit != rhs.row_buffer_hit)
    return lhs.row_buffer_hit > rhs.row_buffer_hit;
  return lhs.event_cycle < rhs.event_cycle;
}
BANK_REQUEST* bliss::msched_get_request(std::array<DRAM_CHANNEL, DRAM_CHANNELS> ::iterator channel_it) {
  DRAM_CHANNEL& channel = *channel_it;
  auto new_req = std::min_element(std::begin(channel.bank_request),std::end(channel.bank_request), bliss_sorter);

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