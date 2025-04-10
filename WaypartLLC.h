#pragma once

#include "Request.h"
#include "StatusReport.h"
#include "BaseLLC.h"

namespace ramulator {

struct WaypartLLC : public BaseLLC {

  WaypartLLC(int size, int assoc, int block_size, int num_mshr_entries);
  // 18-740
  // your cache and MSHR data structures here 
  // functions to allocate/hit/evict etc. here
  // see SimpleLLC for reference.

  // send a request to the cache system (i.e. process a request)
  virtual bool send(Request& req, StatusReport& report) override;

  // this is called by the memory when it has a response
  // free corresponding MSHR entry and update cache data
  // use SimpleLLC::callback as reference
  virtual void callback(Request& req) override;
};
}