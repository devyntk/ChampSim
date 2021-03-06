#include "dram_controller.h"

bool frfcfs_sorter(BANK_REQUEST const& rhs, BANK_REQUEST const& lhs) {
  if (lhs.row_buffer_hit != rhs.row_buffer_hit)
    // swapped since we want to sort the buffer hits at the bottom
    return lhs.row_buffer_hit > rhs.row_buffer_hit;
  return lhs.event_cycle < rhs.event_cycle;
}

void frfcfs::initalize_msched() {}
void frfcfs::msched_cycle_operate() {}
BANK_REQUEST* frfcfs::msched_get_request(std::array<DRAM_CHANNEL, DRAM_CHANNELS>::iterator channel_it) {
  DRAM_CHANNEL& channel = *channel_it;
  return std::min_element(std::begin(channel.bank_request), std::end(channel.bank_request), frfcfs_sorter);
}
void frfcfs::msched_channel_operate(std::array<DRAM_CHANNEL, DRAM_CHANNELS> ::iterator channel_it) {
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
