//
// Created by devyn on 5/1/22.
//
#include "bliss.h"

bliss_channel::req_it bliss_channel::get_new_active_request() {
  return std::min_element(std::begin(bank_request), std::end(bank_request), min_event_cycle<BANK_REQUEST>());
//  if(req->valid) {
//    if (req->pkt->cpu == last_cpu) {
//      requests_served += 1;
//      if (requests_served > blacklist_threshold) {
//        is_blacklisted[last_cpu] = true;
//      }
//    } else {
//      requests_served = 0;
//    }
//  } else {
//    return std::end(bank_request);
//  }
//
//  return req;
}
struct next_schedule_ready : public invalid_is_maximal<PACKET, min_event_cycle<PACKET>, PACKET, is_unscheduled, is_unscheduled> {};
PACKET& bliss_channel::fill_bank_request() {
  std::vector<PACKET>::iterator iter_next_schedule;
  if (write_mode)
    iter_next_schedule = std::min_element(std::begin(WQ), std::end(WQ), next_schedule_ready());
  else
    iter_next_schedule = std::min_element(std::begin(RQ), std::end(RQ), next_schedule_ready());
  return *iter_next_schedule;
}
void bliss_channel::business_operate() {
//  clearing_count++;
//  if (clearing_count >= clearing_interval){
//    for(long unsigned int i = 0; i < NUM_CPUS; i++)
//      is_blacklisted[i] = false;
//  }
}
bool bliss_channel::operator()(const BANK_REQUEST& lhs, const BANK_REQUEST& rhs) {
//  if (lhs.valid && rhs.valid){
//    bool lhs_blacklist = is_blacklisted[lhs.pkt->cpu];
//    bool rhs_blacklist = is_blacklisted[rhs.pkt->cpu];
//    if (lhs_blacklist != rhs_blacklist)
//      return lhs_blacklist < rhs_blacklist;
//  } else {
//    return lhs.valid > rhs.valid;
//  }
  if (lhs.row_buffer_hit != rhs.row_buffer_hit) {
    return lhs.row_buffer_hit < rhs.row_buffer_hit;
  }
  return lhs.event_cycle < rhs.event_cycle;
}
