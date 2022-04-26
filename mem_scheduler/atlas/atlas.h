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
  void msched_channel_operate(DRAM_CHANNEL& channel) override;
  BANK_REQUEST* msched_get_request(DRAM_CHANNEL& channel) override;
  atlas(double freqScale) : MEMORY_CONTROLLER(freqScale){};
private:
  void add_packet(std::vector<PACKET>::iterator packet, DRAM_CHANNEL& channel, bool is_write);
};


#endif // CHAMPSIM_ATLAS_H
