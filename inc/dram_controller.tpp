#ifndef DRAM_TPP
#define DRAM_TPP

#include "dram_controller.h"

#include <algorithm>
#include <iomanip>
#include <type_traits>

#include "champsim_constants.h"
#include "util.h"

extern uint8_t all_warmup_complete;

template <typename chan, typename req>
void MEMORY_CONTROLLER<chan, req>::operate() {
  cycle_operate();
  for (auto channel_it = std::begin(channels); channel_it != std::end(channels); channel_it++){
    chan& channel = *channel_it;
    channel.operate(current_cycle);
  }
}

template <typename req>
typename DRAM_CHANNEL<req>::req_it DRAM_CHANNEL<req>::get_bank(uint32_t addr)
{
  auto it = std::begin(bank_request);
  std::advance(it, get_bank_addr(addr));
  return it;
}

template <typename req>
void DRAM_CHANNEL<req>::operate(uint64_t current_cycle_new)
{
  this->current_cycle = current_cycle_new;
  // Finish request
  if (active_request != std::end(bank_request) && active_request->event_cycle <= this->current_cycle) {
    for (auto ret : active_request->pkt->to_return)
      ret->return_data(&(*active_request->pkt));

    active_request->valid = false;

    *active_request->pkt = {};
    active_request = std::end(bank_request);
  }

  // Look for requests to put on the bus
  if (active_request == std::end(bank_request) && dbus_cycle_available <= this->current_cycle) {
    req_it iter_next_process = get_new_active_request();
//    std::cout << "Valid: " << iter_next_process->valid << " Cycle: " <<  iter_next_process->event_cycle << std::endl;
    if (iter_next_process != std::end(bank_request) && iter_next_process->valid && iter_next_process->event_cycle <= this->current_cycle) {
//      std::cout << "Putting new req on bus" << std::endl;
      // Bus is available
      // Put this request on the data bus
      active_request = iter_next_process;
      active_request->event_cycle = current_cycle + DRAM_DBUS_RETURN_TIME;

      if (iter_next_process->row_buffer_hit)
        if (write_mode)
          WQ_ROW_BUFFER_HIT++;
        else
          RQ_ROW_BUFFER_HIT++;
      else if (write_mode)
        WQ_ROW_BUFFER_MISS++;
      else
        RQ_ROW_BUFFER_MISS++;
    } else {
      // Bus is congested
      if (active_request != std::end(bank_request))
        dbus_cycle_congested += (active_request->event_cycle - current_cycle);
      else
        dbus_cycle_congested += (dbus_cycle_available - current_cycle);
      dbus_count_congested++;
    }
  }
  PACKET& packet = fill_bank_request();
  if (is_valid<PACKET>()(packet) && packet.event_cycle <= this->current_cycle) {
    uint32_t op_rank = dram_get_rank(packet.address), op_bank = dram_get_bank(packet.address), op_row = dram_get_row(packet.address);

    auto op_idx = op_rank * DRAM_BANKS + op_bank;

    if (!bank_request[op_idx].valid) {
      bool row_buffer_hit = (bank_request[op_idx].open_row == op_row);

      // this bank is now busy
      bank_request[op_idx] = {true, row_buffer_hit, write_mode, op_row, current_cycle + tCAS + (row_buffer_hit ? 0 : tRP + tRCD), (PACKET*)&packet};

      packet.scheduled = true;
      packet.event_cycle = std::numeric_limits<uint64_t>::max();
    }
  }

  this->custom_operate();
}

template <typename chan>
void DRAM_CHANNEL<chan>::print_generic_stats() {
  std::cout << " DBUS AVG_CONGESTED_CYCLE: ";
  if (dbus_count_congested)
    std::cout << std::setw(10) << ((double)dbus_cycle_congested / dbus_count_congested);
  else
    std::cout << "-";
  std::cout << std::endl;
}

template <typename chan, typename req>
int SPLIT_MEM_CONTROLLER<chan, req>::add_rq(PACKET* packet)
{

  if (all_warmup_complete < NUM_CPUS) {
    for (auto ret : packet->to_return) {
      ret->return_data(packet);
    }
    return -1; // Fast-forward
  }

  auto& channel = this->get_channel_obj(packet->address);

  // Check for forwarding
  auto wq_it = std::find_if(std::begin(channel.WQ), std::end(channel.WQ), eq_addr<PACKET>(packet->address, LOG2_BLOCK_SIZE));
  if (wq_it != std::end(channel.WQ)) {
    packet->data = wq_it->data;
    for (auto ret : packet->to_return)
      ret->return_data(packet);

    return -1; // merged index
  }

  // Check for duplicates
  auto rq_it = std::find_if(std::begin(channel.RQ), std::end(channel.RQ), eq_addr<PACKET>(packet->address, LOG2_BLOCK_SIZE));
  if (rq_it != std::end(channel.RQ)) {
    packet_dep_merge(rq_it->lq_index_depend_on_me, packet->lq_index_depend_on_me);
    packet_dep_merge(rq_it->sq_index_depend_on_me, packet->sq_index_depend_on_me);
    packet_dep_merge(rq_it->instr_depend_on_me, packet->instr_depend_on_me);
    packet_dep_merge(rq_it->to_return, packet->to_return);

    return std::distance(std::begin(channel.RQ), rq_it); // merged index
  }

  // Find empty slot
  rq_it = std::find_if_not(std::begin(channel.RQ), std::end(channel.RQ), is_valid<PACKET>());
  if (rq_it == std::end(channel.RQ)) {
    return 0;
  }

  *rq_it = *packet;
  rq_it->event_cycle = this->current_cycle;

  return get_occupancy(1, packet->address);
}

template <typename chan, typename req>
int SPLIT_MEM_CONTROLLER<chan, req>::add_wq(PACKET* packet)
{
//  std::cout << "Add to wq" << std::endl;
  if (all_warmup_complete < NUM_CPUS)
    return -1; // Fast-forward

  auto& channel = this->channels[dram_get_channel(packet->address)];

  // Check for duplicates
  auto wq_it = std::find_if(std::begin(channel.WQ), std::end(channel.WQ), eq_addr<PACKET>(packet->address, LOG2_BLOCK_SIZE));
  if (wq_it != std::end(channel.WQ))
    return 0;

  // search for the empty index
  wq_it = std::find_if_not(std::begin(channel.WQ), std::end(channel.WQ), is_valid<PACKET>());
  if (wq_it == std::end(channel.WQ)) {
    channel.WQ_FULL++;
    return -2;
  }

  *wq_it = *packet;
  wq_it->event_cycle = this->current_cycle;

  return get_occupancy(2, packet->address);
}

template <typename chan, typename req>
uint32_t SPLIT_MEM_CONTROLLER<chan, req>::get_occupancy(uint8_t queue_type, uint64_t address)
{
  uint32_t channel = dram_get_channel(address);
  if (queue_type == 1)
    return std::count_if(std::begin(this->channels[channel].RQ), std::end(this->channels[channel].RQ), is_valid<PACKET>());
  else if (queue_type == 2)
    return std::count_if(std::begin(this->channels[channel].WQ), std::end(this->channels[channel].WQ), is_valid<PACKET>());
  else if (queue_type == 3)
    return get_occupancy(1, address);

  return 0;
}

template <typename chan, typename req>
uint32_t SPLIT_MEM_CONTROLLER<chan, req>::get_size(uint8_t queue_type, uint64_t address)
{
  auto channel = this->get_channel_obj(address);
  if (queue_type == 1)
    return channel.RQ.size();
  else if (queue_type == 2)
    return channel.WQ.size();
  else if (queue_type == 3)
    return get_size(1, address);

  return 0;
}
template <typename req>
void SPLIT_MEM_CHANNEL<req>::custom_operate()
{
  // Check queue occupancy
  std::size_t wq_occu = std::count_if(std::begin(WQ), std::end(WQ), is_valid<PACKET>());
  std::size_t rq_occu = std::count_if(std::begin(RQ), std::end(RQ), is_valid<PACKET>());

  // Change modes if the queues are unbalanced
  if ((!this->write_mode && (wq_occu >= DRAM_WRITE_HIGH_WM || (rq_occu == 0 && wq_occu > 0)))
      || (this->write_mode && (wq_occu == 0 || (rq_occu > 0 && wq_occu < DRAM_WRITE_LOW_WM)))) {
    // Reset scheduled requests
    for (auto it = std::begin(this->bank_request); it != std::end(this->bank_request); ++it) {
      // Leave active request on the data bus
      if (it != this->active_request && it->valid) {
        // Leave rows charged
        if (it->event_cycle < (this->current_cycle + this->tCAS))
          it->open_row = UINT32_MAX;

        // This bank is ready for another DRAM request
        it->valid = false;
        it->pkt->scheduled = false;
        it->pkt->event_cycle = this->current_cycle;
      }
    }

    // Add data bus turn-around time
    if (this->active_request != std::end(this->bank_request))
      this->dbus_cycle_available = this->active_request->event_cycle + this->DRAM_DBUS_TURN_AROUND_TIME; // After ongoing finish
    else
      this->dbus_cycle_available = this->current_cycle + this->DRAM_DBUS_TURN_AROUND_TIME;

    // Invert the mode
    this->write_mode = !this->write_mode;
  }
  this->business_operate();
}


template <typename chan, typename req>
int SINGLE_BUF_CONTROLLER<chan, req>::add_wq(PACKET* packet)
{
  if (all_warmup_complete < NUM_CPUS)
    return -1; // Fast-forward

  auto& channel = this->channels[dram_get_channel(packet->address)];

  // Check for duplicates
  auto wq_it = std::find_if(std::begin(channel.QUEUE), std::end(channel.QUEUE), eq_addr<PACKET>(packet->address, LOG2_BLOCK_SIZE));
  if (wq_it != std::end(channel.QUEUE))
    return 0;

  // search for the empty index
  wq_it = std::find_if_not(std::begin(channel.QUEUE), std::end(channel.QUEUE), this->gen_pck_is_valid);
  if (wq_it == std::end(channel.QUEUE)) {
    channel.WQ_FULL++;
    return -2;
  }

  wq_it->packet = *packet;
  wq_it->is_write = true;
  wq_it->packet->event_cycle = this->current_cycle;

  return get_occupancy(2, packet->address);
}

template <typename chan, typename req>
int SINGLE_BUF_CONTROLLER<chan, req>::add_rq(PACKET* packet)
{
  if (all_warmup_complete < NUM_CPUS) {
    for (auto ret : packet->to_return)
      ret->return_data(packet);

    return -1; // Fast-forward
  }

  auto& channel = this->get_channel_obj(packet->address);

  // Check for forwarding
  auto wq_it = std::find_if(std::begin(channel.QUEUE), std::end(channel.QUEUE), eq_addr<PACKET>(packet->address, LOG2_BLOCK_SIZE));
  if (wq_it != std::end(channel.QUEUE)) {
    packet->data = wq_it->packet->data;
    for (auto ret : packet->to_return)
      ret->return_data(packet);

    return -1; // merged index
  }

  // Check for duplicates
  auto rq_it = std::find_if(std::begin(channel.QUEUE), std::end(channel.QUEUE), eq_addr<PACKET>(packet->address, LOG2_BLOCK_SIZE));
  if (rq_it != std::end(channel.QUEUE)) {
    packet_dep_merge(rq_it->lq_index_depend_on_me, packet->lq_index_depend_on_me);
    packet_dep_merge(rq_it->sq_index_depend_on_me, packet->sq_index_depend_on_me);
    packet_dep_merge(rq_it->instr_depend_on_me, packet->instr_depend_on_me);
    packet_dep_merge(rq_it->to_return, packet->to_return);

    return std::distance(std::begin(channel.RQ), rq_it); // merged index
  }

  // Find empty slot
  rq_it = std::find_if_not(std::begin(channel.QUEUE), std::end(channel.QUEUE), this->gen_pck_is_valid);
  if (rq_it == std::end(channel.QUEUE)) {
    return 0;
  }

  wq_it->packet = *packet;
  wq_it->is_write = false;
  wq_it->packet->event_cycle = this->current_cycle;

  return get_occupancy(1, packet->address);
}
template <typename chan, typename req>
uint32_t SINGLE_BUF_CONTROLLER<chan, req>::get_occupancy(uint8_t queue_type, uint64_t address)
{
  uint32_t channel = dram_get_channel(address);
  return std::count_if(std::begin(this->channels[channel].QUEUE), std::end(this->channels[channel].QUEUE), this->gen_pck_is_valid);
}

template <typename chan, typename req>
uint32_t SINGLE_BUF_CONTROLLER<chan, req>::get_size(uint8_t queue_type, uint64_t address)
{
  auto channel = this->get_channel_obj(address);
  return channel.QUEUE.size();
}
#endif