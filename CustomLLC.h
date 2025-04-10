#pragma once

#include "Request.h"
#include "StatusReport.h"
#include "BaseLLC.h"

namespace ramulator {

struct CustomLLC : public BaseLLC {

  CustomLLC(int size, int assoc, int block_size, int num_mshr_entries);

  virtual bool send(Request& req, StatusReport& report) override;

  virtual void callback(Request& req) override;
};
}