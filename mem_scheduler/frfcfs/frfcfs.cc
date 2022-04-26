#include "dram_controller.h"

struct is_unscheduled {
  bool operator()(const PACKET& lhs) { return !lhs.scheduled; }
};
struct next_schedule : public invalid_is_maximal<PACKET, min_event_cycle<PACKET>, PACKET, is_unscheduled, is_unscheduled> {
};

void frfcfs::initalize_msched() {}
void frfcfs::msched_cycle_operate() {}
BANK_REQUEST* frfcfs::msched_get_request(DRAM_CHANNEL& channel) {
  return std::min_element(std::begin(channel.bank_request), std::end(channel.bank_request), min_event_cycle<BANK_REQUEST>());
}
void frfcfs::msched_channel_operate(DRAM_CHANNEL& channel) {
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
  if (channel.write_mode)
    iter_next_schedule = std::min_element(std::begin(channel.WQ), std::end(channel.WQ), next_schedule());
  else
    iter_next_schedule = std::min_element(std::begin(channel.RQ), std::end(channel.RQ), next_schedule());

  if (is_valid<PACKET>()(*iter_next_schedule) && iter_next_schedule->event_cycle <= current_cycle) {
    uint32_t op_rank = dram_get_rank(iter_next_schedule->address), op_bank = dram_get_bank(iter_next_schedule->address),
             op_row = dram_get_row(iter_next_schedule->address);

    auto op_idx = op_rank * DRAM_BANKS + op_bank;

    if (!channel.bank_request[op_idx].valid) {
      bool row_buffer_hit = (channel.bank_request[op_idx].open_row == op_row);

      // this bank is now busy
      channel.bank_request[op_idx] = {true, row_buffer_hit, op_row, current_cycle + tCAS + (row_buffer_hit ? 0 : tRP + tRCD), iter_next_schedule};

      iter_next_schedule->scheduled = true;
      iter_next_schedule->event_cycle = std::numeric_limits<uint64_t>::max();
    }
  }
}
