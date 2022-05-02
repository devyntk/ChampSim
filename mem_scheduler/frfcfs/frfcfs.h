#ifndef CHAMPSIM_FRFCFS_H
#define CHAMPSIM_FRFCFS_H

#include "dram_controller.h"
#include <array>
#include <numeric>
#include <algorithm>

class frfcfs_channel: public SPLIT_MEM_CHANNEL<BANK_REQUEST>
{
  req_it get_new_active_request() override;
  PACKET & fill_bank_request() override;
  bool operator()(const BANK_REQUEST& lhs, const BANK_REQUEST& rhs);
};

class frfcfs: public SPLIT_MEM_CONTROLLER<frfcfs_channel, BANK_REQUEST>
{
public:
  explicit frfcfs(double freq_scale) : SPLIT_MEM_CONTROLLER<frfcfs_channel, BANK_REQUEST>(freq_scale) {};

};
#endif // CHAMPSIM_FRFCFS_H
