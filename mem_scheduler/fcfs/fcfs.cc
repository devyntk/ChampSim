#include "dram_controller.h"
#include <array>
#include <numeric>
#include <algorithm>


void fcfs::initalize_msched() {}
void fcfs::msched_cycle_operate() {
  clearing_count++;
  if (clearing_count >= clearing_interval){
    for(long unsigned int i = 0; i < NUM_CPUS; i++)
      is_blacklisted[i] = false;
  }
}
void fcfs::add_packet(std::vector<PACKET>::iterator packet, DRAM_CHANNEL& channel, bool is_write){
  MEMORY_CONTROLLER::add_packet(packet, channel, is_write);
}
void fcfs::msched_channel_operate(std::array<DRAM_CHANNEL, DRAM_CHANNELS> ::iterator channel_it) {
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
  if (channel.write_mode)
    iter_next_schedule = std::min_element(std::begin(channel.WQ), std::end(channel.WQ), next_schedule());
  is_write = true;
  else
      iter_next_schedule = std::min_element(std::begin(channel.RQ), std::end(channel.RQ), next_schedule());
  is_write = false;

  MEMORY_CONTROLLER::add_packet(iter_next_schedule, channel, is_write);
}
bool fcfs_sorter(BANK_REQUEST const& rhs, BANK_REQUEST const& lhs) {
  return lhs.event_cycle < rhs.event_cycle;
}
BANK_REQUEST* fcfs::msched_get_request(std::array<DRAM_CHANNEL, DRAM_CHANNELS> ::iterator channel_it) {
  DRAM_CHANNEL& channel = *channel_it;
  auto new_req = std::max_element(std::begin(channel.bank_request),std::end(channel.bank_request), fcfs_sorter);

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