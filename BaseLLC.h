#pragma once

#include "Request.h"
#include "StatusReport.h"

#include <list>

namespace ramulator {

struct BaseLLC {
  size_t size;
  unsigned int assoc;
  unsigned int block_size;
  unsigned int num_mshr_entries;
  std::list<Request> retry_list;

  BaseLLC(int size, int assoc, int block_size, int num_mshr_entries)
      : size(size),
        assoc(assoc),
        block_size(block_size),
        num_mshr_entries(num_mshr_entries) {}

  virtual bool send(Request& req, StatusReport& report) { return false; }

  virtual void callback(Request& req) {}

  virtual ~BaseLLC() {}
};
}