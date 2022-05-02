#ifndef frfcfs_cc
#define frfcfs_cc

#include "frfcfs.h"
template <typename T = BANK_REQUEST, typename U = BANK_REQUEST>
struct cmp_event_cycle_ready {
  bool operator()(const T& lhs, const U& rhs) {
    if (lhs.row_buffer_hit != rhs.row_buffer_hit) {
      return lhs.row_buffer_hit < rhs.row_buffer_hit;
    }
    return lhs.event_cycle < rhs.event_cycle;
  }
};
template <typename T>
struct min_event_cycle_ready : invalid_is_maximal<T, cmp_event_cycle_ready<T>> {
};
frfcfs_channel::req_it frfcfs_channel::get_new_active_request() {
  return std::min_element(std::begin(bank_request), std::end(bank_request), min_event_cycle_ready<BANK_REQUEST>());
}
PACKET& frfcfs_channel::fill_bank_request() {
  std::vector<PACKET>::iterator iter_next_schedule;
  if (write_mode)
    iter_next_schedule = std::min_element(std::begin(WQ), std::end(WQ), invalid_is_maximal<PACKET, min_event_cycle<PACKET>, PACKET, is_unscheduled, is_unscheduled>());
  else
    iter_next_schedule = std::min_element(std::begin(RQ), std::end(RQ), invalid_is_maximal<PACKET, min_event_cycle<PACKET>, PACKET, is_unscheduled, is_unscheduled>());
  return *iter_next_schedule;
}
bool frfcfs_channel::operator()(const BANK_REQUEST& lhs, const BANK_REQUEST& rhs) {

}

#endif