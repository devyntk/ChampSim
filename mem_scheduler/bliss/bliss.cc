#include "bliss.h"
#include <array>
#include <numeric>
#include <algorithm>

extern MEMORY_CONTROLLER DRAM;

uint32_t last_cpu_req = NUM_CPUS;
int req_served = 0 ;
const int blacklist_threshold = 4;
std::array<bool, NUM_CPUS> is_blacklisted;
const int clearing_interval = 10000;
int clearing_count = 0;

bool is_row_hit(const PACKET& packet) {
  uint32_t op_rank = DRAM.dram_get_rank(packet.address), op_bank = DRAM.dram_get_bank(packet.address),
           op_row = DRAM.dram_get_row(packet.address), op_chn = DRAM.dram_get_channel(packet.address);
  return DRAM.channels[op_chn].bank_request[op_rank * DRAM_BANKS + op_bank].open_row == op_row;
}

struct bliss_event_cycle {
  bool operator()(const PACKET& lhs, const PACKET& rhs) {
    bool lhs_blacklisted = is_blacklisted[lhs.cpu];
    bool rhs_blacklisted = is_blacklisted[rhs.cpu];
    if (lhs_blacklisted != rhs_blacklisted) {
      return lhs_blacklisted < rhs_blacklisted;
    }
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
  // Check queue occupancy
  std::size_t wq_occu = std::count_if(std::begin(channel.WQ), std::end(channel.WQ), is_valid<PACKET>());
  std::size_t rq_occu = std::count_if(std::begin(channel.RQ), std::end(channel.RQ), is_valid<PACKET>());

  // Change modes if the queues are unbalanced
  if ((!channel.write_mode && (wq_occu >= DRAM_WRITE_HIGH_WM || (rq_occu == 0 && wq_occu > 0)))
      || (channel.write_mode && (wq_occu == 0 || (rq_occu > 0 && wq_occu < DRAM_WRITE_LOW_WM)))) {
    // Reset scheduled requests
    clear_channel_bank_requests(channel);

    // Add data bus turn-around time
    if (channel.active_request != std::end(channel.bank_request))
      channel.dbus_cycle_available = channel.active_request->event_cycle + DRAM_DBUS_TURN_AROUND_TIME; // After ongoing finish
    else
      channel.dbus_cycle_available = current_cycle + DRAM_DBUS_TURN_AROUND_TIME;

    // Invert the mode
    channel.write_mode = !channel.write_mode;
  }


  // Look for queued packets that have not been scheduled
  std::vector<PACKET>::iterator iter_next_schedule;
  bool is_write;
  if (channel.write_mode) {
    iter_next_schedule = std::min_element(std::begin(channel.WQ), std::end(channel.WQ), next_schedule());
    is_write = true;
  }else {
    iter_next_schedule = std::min_element(std::begin(channel.RQ), std::end(channel.RQ), next_schedule());
    is_write = false;
  }

  MEMORY_CONTROLLER::add_packet(iter_next_schedule, channel, is_write);
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

  if (new_req != channel.active_request){
    // update blacklist
    uint32_t cpu = channel.active_request->pkt->cpu;
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

  return new_req;
}