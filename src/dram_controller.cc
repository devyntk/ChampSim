#include "dram_controller.h"

#include <algorithm>

#include "champsim_constants.h"
#include "util.h"

extern uint8_t all_warmup_complete;

void MEMORY_CONTROLLER::operate()
{
  msched_cycle_operate();
  for (auto channel_it = std::begin(channels); channel_it != std::end(channels); channel_it++) {
    DRAM_CHANNEL& channel = *channel_it;
    // Finish request
    if (channel.active_request != std::end(channel.bank_request) && channel.active_request->event_cycle <= current_cycle) {
      for (auto ret : channel.active_request->pkt->to_return)
        ret->return_data(&(*channel.active_request->pkt));

      channel.active_request->valid = false;

      *channel.active_request->pkt = {};
      channel.active_request = std::end(channel.bank_request);
    }

    // Look for requests to put on the bus
    BANK_REQUEST* iter_next_process = std::min_element(std::begin(channel.bank_request), std::end(channel.bank_request), min_event_cycle<BANK_REQUEST>());
    if (iter_next_process->valid && iter_next_process->event_cycle <= current_cycle) {
      if (channel.active_request == std::end(channel.bank_request) && channel.dbus_cycle_available <= current_cycle) {
        // Bus is available
        // Put this request on the data bus
        channel.active_request = iter_next_process;
        channel.active_request->event_cycle = current_cycle + DRAM_DBUS_RETURN_TIME;

        if (iter_next_process->row_buffer_hit)
          if (channel.write_mode)
            channel.WQ_ROW_BUFFER_HIT++;
          else
            channel.RQ_ROW_BUFFER_HIT++;
        else if (channel.write_mode)
          channel.WQ_ROW_BUFFER_MISS++;
        else
          channel.RQ_ROW_BUFFER_MISS++;
      } else {
        // Bus is congested
        if (channel.active_request != std::end(channel.bank_request))
          channel.dbus_cycle_congested += (channel.active_request->event_cycle - current_cycle);
        else
          channel.dbus_cycle_congested += (channel.dbus_cycle_available - current_cycle);
        channel.dbus_count_congested++;
      }
    }
    msched_channel_operate(channel_it);
  }
}

int MEMORY_CONTROLLER::add_rq(PACKET* packet)
{
  if (all_warmup_complete < NUM_CPUS) {
    for (auto ret : packet->to_return)
      ret->return_data(packet);

    return -1; // Fast-forward
  }

  auto& channel = channels[dram_get_channel(packet->address)];

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
  rq_it->event_cycle = current_cycle;

  return get_occupancy(1, packet->address);
}

int MEMORY_CONTROLLER::add_wq(PACKET* packet)
{
  if (all_warmup_complete < NUM_CPUS)
    return -1; // Fast-forward

  auto& channel = channels[dram_get_channel(packet->address)];

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
  wq_it->event_cycle = current_cycle;

  return get_occupancy(2, packet->address);
}

int MEMORY_CONTROLLER::add_pq(PACKET* packet) { return add_rq(packet); }

/*
 * | row address | rank index | column address | bank index | channel | block
 * offset |
 */

uint32_t MEMORY_CONTROLLER::dram_get_channel(uint64_t address)
{
  int shift = LOG2_BLOCK_SIZE;
  return (address >> shift) & bitmask(lg2(DRAM_CHANNELS));
}

uint32_t MEMORY_CONTROLLER::dram_get_bank(uint64_t address)
{
  int shift = lg2(DRAM_CHANNELS) + LOG2_BLOCK_SIZE;
  return (address >> shift) & bitmask(lg2(DRAM_BANKS));
}

uint32_t MEMORY_CONTROLLER::dram_get_column(uint64_t address)
{
  int shift = lg2(DRAM_BANKS) + lg2(DRAM_CHANNELS) + LOG2_BLOCK_SIZE;
  return (address >> shift) & bitmask(lg2(DRAM_COLUMNS));
}

uint32_t MEMORY_CONTROLLER::dram_get_rank(uint64_t address)
{
  int shift = lg2(DRAM_BANKS) + lg2(DRAM_COLUMNS) + lg2(DRAM_CHANNELS) + LOG2_BLOCK_SIZE;
  return (address >> shift) & bitmask(lg2(DRAM_RANKS));
}

uint32_t MEMORY_CONTROLLER::dram_get_row(uint64_t address)
{
  int shift = lg2(DRAM_RANKS) + lg2(DRAM_BANKS) + lg2(DRAM_COLUMNS) + lg2(DRAM_CHANNELS) + LOG2_BLOCK_SIZE;
  return (address >> shift) & bitmask(lg2(DRAM_ROWS));
}

uint32_t MEMORY_CONTROLLER::get_occupancy(uint8_t queue_type, uint64_t address)
{
  uint32_t channel = dram_get_channel(address);
  if (queue_type == 1)
    return std::count_if(std::begin(channels[channel].RQ), std::end(channels[channel].RQ), is_valid<PACKET>());
  else if (queue_type == 2)
    return std::count_if(std::begin(channels[channel].WQ), std::end(channels[channel].WQ), is_valid<PACKET>());
  else if (queue_type == 3)
    return get_occupancy(1, address);

  return 0;
}

uint32_t MEMORY_CONTROLLER::get_size(uint8_t queue_type, uint64_t address)
{
  uint32_t channel = dram_get_channel(address);
  if (queue_type == 1)
    return channels[channel].RQ.size();
  else if (queue_type == 2)
    return channels[channel].WQ.size();
  else if (queue_type == 3)
    return get_size(1, address);

  return 0;
}

void MEMORY_CONTROLLER::clear_channel_bank_requests(DRAM_CHANNEL& channel)
{
  for (auto it = std::begin(channel.bank_request); it != std::end(channel.bank_request); ++it) {
    // Leave active request on the data bus
    if (it != channel.active_request && it->valid) {
      // Leave rows charged
      if (it->event_cycle < (current_cycle + tCAS))
        it->open_row = UINT32_MAX;

      // This bank is ready for another DRAM request
      it->valid = false;
      it->pkt->scheduled = false;
      it->pkt->event_cycle = current_cycle;
    }
  }
}
void MEMORY_CONTROLLER::add_packet(std::vector<PACKET>::iterator packet, DRAM_CHANNEL& channel, bool is_write)
{
  if (is_valid<PACKET>()(*packet) && packet->event_cycle <= current_cycle) {
    uint32_t op_rank = dram_get_rank(packet->address), op_bank = dram_get_bank(packet->address), op_row = dram_get_row(packet->address);

    auto op_idx = op_rank * DRAM_BANKS + op_bank;

    if (!channel.bank_request[op_idx].valid) {
      bool row_buffer_hit = (channel.bank_request[op_idx].open_row == op_row);

      // this bank is now busy
      channel.bank_request[op_idx] = {true, row_buffer_hit, is_write, op_row, current_cycle + tCAS + (row_buffer_hit ? 0 : tRP + tRCD), packet};

      packet->scheduled = true;
      packet->event_cycle = std::numeric_limits<uint64_t>::max();
    }
  }
}
