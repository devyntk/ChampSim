#ifndef CHAMPSIM_ATLAS_H
#define CHAMPSIM_ATLAS_H
#define MSCHED
#include "dram_controller.h"
#include <vector>
class atlas : public MEMORY_CONTROLLER
{
public:
  void initalize_msched() override;
  void msched_cycle_operate() override;
  void msched_channel_operate(std::array<DRAM_CHANNEL, DRAM_CHANNELS> ::iterator channel_it) override;
  BANK_REQUEST* msched_get_request(std::array<DRAM_CHANNEL, DRAM_CHANNELS> ::iterator channel_it) override;
  atlas(double freqScale) : MEMORY_CONTROLLER(freqScale){};
private:
  void add_packet(std::vector<PACKET>::iterator packet, DRAM_CHANNEL& channel, bool is_write);
};


#endif // CHAMPSIM_ATLAS_H
