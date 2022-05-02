#ifndef fcfs_cc
#define fcfs_cc

#include "fcfs.h"

struct next_schedule_ready : public invalid_is_maximal<PACKET, min_event_cycle<PACKET>, PACKET, is_unscheduled, is_unscheduled> {};

fcfs_channel::req_it fcfs_channel::get_new_active_request() {
  return std::min_element(std::begin(bank_request), std::end(bank_request), min_event_cycle<BANK_REQUEST>());
}
PACKET& fcfs_channel::fill_bank_request() {
  std::vector<PACKET>::iterator iter_next_schedule;
  if (write_mode)
    iter_next_schedule = std::min_element(std::begin(WQ), std::end(WQ), next_schedule_ready());
  else
    iter_next_schedule = std::min_element(std::begin(RQ), std::end(RQ), next_schedule_ready());
  return *iter_next_schedule;
}

#endif