#ifndef DRAM_H
#define DRAM_H

#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

#include "champsim_constants.h"
#include "memory_class.h"
#include "operable.h"
#include "util.h"
#include "dram_addr.h"



namespace detail
{
// https://stackoverflow.com/a/31962570
constexpr int32_t ceil(float num)
{
  return (static_cast<float>(static_cast<int32_t>(num)) == num) ? static_cast<int32_t>(num) : static_cast<int32_t>(num) + ((num > 0) ? 1 : 0);
}
} // namespace detail
struct is_unscheduled {
  bool operator()(const PACKET& lhs) { return !lhs.scheduled; }
};
struct next_schedule : public invalid_is_maximal<PACKET, min_event_cycle<PACKET>, PACKET, is_unscheduled, is_unscheduled> {};

struct BANK_REQUEST {
  bool valid = false, row_buffer_hit = false, is_write = false;

  std::size_t open_row = std::numeric_limits<uint32_t>::max();

  uint64_t event_cycle = 0;

  PACKET* pkt;
};
template <typename req>
class DRAM_CHANNEL {
public:
  static_assert(std::is_base_of<BANK_REQUEST, req>::value, "Invalid template type");

  // DRAM_IO_FREQ defined in champsim_constants.h
  const static uint64_t tRP = detail::ceil(1.0 * tRP_DRAM_NANOSECONDS * DRAM_IO_FREQ / 1000);
  const static uint64_t tRCD = detail::ceil(1.0 * tRCD_DRAM_NANOSECONDS * DRAM_IO_FREQ / 1000);
  const static uint64_t tCAS = detail::ceil(1.0 * tCAS_DRAM_NANOSECONDS * DRAM_IO_FREQ / 1000);
  const static uint64_t DRAM_DBUS_TURN_AROUND_TIME = detail::ceil(1.0 * DBUS_TURN_AROUND_NANOSECONDS * DRAM_IO_FREQ / 1000);
  const static uint64_t DRAM_DBUS_RETURN_TIME = detail::ceil(1.0 * BLOCK_SIZE / DRAM_CHANNEL_WIDTH);

  using req_it = typename std::array<req, DRAM_RANKS* DRAM_BANKS>::iterator;
  req_it active_request = std::end(bank_request);

  uint64_t dbus_cycle_available = 0, dbus_cycle_congested = 0, dbus_count_congested = 0;
  unsigned WQ_ROW_BUFFER_HIT = 0, WQ_ROW_BUFFER_MISS = 0, RQ_ROW_BUFFER_HIT = 0, RQ_ROW_BUFFER_MISS = 0, WQ_FULL = 0;
  bool write_mode = false;
  uint64_t current_cycle;


  // one request per bank (there are DRAM_BANKS banks in each rank)
  std::array<req, DRAM_RANKS* DRAM_BANKS> bank_request = {};
  req_it get_bank(uint32_t addr);

  virtual void initalize_channel() = 0;
  virtual void custom_operate() = 0;

  // only active requests can be completed
  // out of the current requests on the banks, which should be selected as the new active request?
  // this gets called only if the bus is available
  virtual req_it get_new_active_request() = 0;

  // select a packet to place on the bank
  // this gets called each cycle
  // will only be placed on the bank if that bank is clear, it's the implementation's responsibility to ensure this is available
  virtual PACKET& fill_bank_request() = 0;

  void clear_channel_bank_requests(uint32_t current_cycle);
  void print_generic_stats();
  void print_stats() {print_generic_stats();};
  void operate(uint64_t current_cycle);
};


template <typename chan, typename req>
class MEMORY_CONTROLLER : public champsim::operable, public MemoryRequestConsumer
{
public:

  static_assert(std::is_base_of<DRAM_CHANNEL<req>, chan>::value, "Invalid template type");
  std::array<chan, DRAM_CHANNELS> channels;

  explicit MEMORY_CONTROLLER(double freq_scale) : champsim::operable(freq_scale),
                                                  MemoryRequestConsumer(std::numeric_limits<unsigned>::max()){
    initalize_msched();
  }

  // queue related virtual methods to implement
  virtual int add_rq(PACKET* packet) override = 0;
  virtual int add_wq(PACKET* packet) override = 0;
  virtual int add_pq(PACKET* packet) override = 0;
  virtual uint32_t get_occupancy(uint8_t queue_type, uint64_t address) override = 0;
  virtual uint32_t get_size(uint8_t queue_type, uint64_t address) override = 0;

  // controller specific virtual methods
  virtual void initalize_msched() {};
  virtual void cycle_operate() {};
  virtual void print_stats() {};

  void operate() override;

  chan& get_channel_obj(uint64_t addr){
    return channels[dram_get_channel(addr)];
  };

  void clear_channel_bank_requests(chan& channel);
};

template <typename req>
class SPLIT_MEM_CHANNEL: public DRAM_CHANNEL<req>
{
public:
  // these values control when to send out a burst of writes
  std::size_t DRAM_WRITE_HIGH_WM = ((DRAM_WQ_SIZE * 7) >> 3);         // 7/8th
  std::size_t DRAM_WRITE_LOW_WM = ((DRAM_WQ_SIZE * 6) >> 3);          // 6/8th
  std::size_t MIN_DRAM_WRITES_PER_SWITCH = ((DRAM_WQ_SIZE * 1) >> 2); // 1/4

  std::vector<PACKET> WQ{DRAM_WQ_SIZE};
  std::vector<PACKET> RQ{DRAM_RQ_SIZE};

  void initalize_channel() override {};
  typename DRAM_CHANNEL<req>::req_it get_new_active_request() override {
    return std::min_element(std::begin(this->bank_request), std::end(this->bank_request), min_event_cycle<BANK_REQUEST>());
  };
  PACKET& fill_bank_request() override {
    std::vector<PACKET>::iterator iter_next_schedule;
    if (this->write_mode)
      iter_next_schedule = std::min_element(std::begin(WQ), std::end(WQ), next_schedule());
    else
      iter_next_schedule = std::min_element(std::begin(RQ), std::end(RQ), next_schedule());
    return *iter_next_schedule;
  };
  void custom_operate() override;
  virtual void business_operate() {};
};


template <typename chan, typename req>
class SPLIT_MEM_CONTROLLER: public MEMORY_CONTROLLER<chan, req>
{
public:
  explicit SPLIT_MEM_CONTROLLER(double freq_scale) : MEMORY_CONTROLLER<chan, req>(freq_scale) {};
  int add_rq(PACKET* packet) override;
  int add_wq(PACKET* packet) override;
  int add_pq(PACKET* packet) override {return add_rq(packet);};
  uint32_t get_occupancy(uint8_t queue_type, uint64_t address) override;
  uint32_t get_size(uint8_t queue_type, uint64_t address) override;
};

class GEN_MEM_CHANNEL: public SPLIT_MEM_CHANNEL<BANK_REQUEST> {};
class GEN_MEMORY_CONTROLLER: public SPLIT_MEM_CONTROLLER<GEN_MEM_CHANNEL, BANK_REQUEST> {};

struct GEN_PACKET{
  PACKET* packet;
  bool is_write;
};

template <typename req>
class SINGLE_BUF_CHANNEL: public DRAM_CHANNEL<req>
{
  SINGLE_BUF_CHANNEL() = default;
  using req_it = typename std::array<req, DRAM_RANKS* DRAM_BANKS>::iterator;
  std::vector<GEN_PACKET> QUEUE{DRAM_WQ_SIZE + DRAM_RQ_SIZE};
  void initalize_channel() override {};
  req_it get_new_active_request() override {return std::begin(this->bank_request);};
  PACKET& fill_bank_request() override {return *(QUEUE[0].packet);};
  void custom_operate() override {};
};
template<typename chan, typename req>
class SINGLE_BUF_CONTROLLER: public MEMORY_CONTROLLER<chan, req>
{
  static_assert(std::is_base_of<SINGLE_BUF_CHANNEL<req>, chan>::value, "Invalid template type");
  using channel_t = SINGLE_BUF_CHANNEL<BANK_REQUEST>;
  std::array<channel_t, DRAM_CHANNELS> channels;
  bool gen_pck_is_valid(GEN_PACKET i) {return i.packet->address != 0;};


  int add_rq(PACKET* packet) override;
  int add_wq(PACKET* packet) override;
  int add_pq(PACKET* packet) override {return add_rq(packet);};
  uint32_t get_occupancy(uint8_t queue_type, uint64_t address);
  uint32_t get_size(uint8_t queue_type, uint64_t address);
};

#include "dram_controller.tpp"


#endif
