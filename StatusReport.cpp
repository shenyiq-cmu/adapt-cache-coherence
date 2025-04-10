#include "StatusReport.h"

namespace ramulator {

void StatusReport::update_send_stats(ScalarStat& cache_total_miss,
                                     ScalarStat& cache_write_miss,
                                     ScalarStat& cache_read_miss,
                                     ScalarStat& cache_mshr_hit,
                                     ScalarStat& cache_mshr_unavailable,
                                     ScalarStat& cache_set_unavailable) {
  // tally misses
  if (write_miss || read_miss) {
    cache_total_miss++;
    if (write_miss) {
      cache_write_miss++;
    } else {
      cache_read_miss++;
    }
  }

  // tally mshr
  if (mshr_hit) {
    cache_mshr_hit++;
  }

  if (mshr_unavailable) {
    cache_mshr_unavailable++;
  }

  // tally set
  if (set_unavailable) {
    cache_set_unavailable++;
  }
}
}