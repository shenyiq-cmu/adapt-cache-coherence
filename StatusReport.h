#pragma once

#include "Config.h"
#include "Request.h"
#include "Statistics.h"

#include <vector>

namespace ramulator {

struct StatusReport {
  bool hit = false;
  bool write_miss = false;
  bool read_miss = false;
  bool mshr_hit = false;
  bool mshr_unavailable = false;
  bool set_unavailable = false;
  bool mshr_allocated = false;
  int evictions = 0;
  std::vector<Request> requests;

  void update_send_stats(ScalarStat& cache_total_miss,
                         ScalarStat& cache_write_miss,
                         ScalarStat& cache_read_miss,
                         ScalarStat& cache_mshr_hit,
                         ScalarStat& cache_mshr_unavailable,
                         ScalarStat& cache_set_unavailable);
};
}