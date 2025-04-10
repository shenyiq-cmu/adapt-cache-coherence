#ifndef __CACHE_H
#define __CACHE_H

// Ramulator headers
#include "Config.h"
#include "Request.h"
#include "Statistics.h"
#include "StatusReport.h"

// LLCs
#include "BaseLLC.h"
#include "SimpleLLC.h"
#include "WaypartLLC.h"
#include "CustomLLC.h"

// standard algorithms on C++ data structures
#include <algorithm>

// for printf
#include <cstdio>

// C-style asserts
#include <cassert>

// lambda function helpers
#include <functional>

// STL data strucutres
#include <list>
#include <map>
#include <queue>

// smart pointers (shared_ptr, unique_ptr etc.)
#include <memory>

namespace ramulator {

// forward definition to use in Cache class
// defined later on in this file
// stores cache configuration, memory requests etc.
class CacheSystem;

// Combined cache description
class Cache {

 public:
  // ---------internal types---------

  enum class Level {
    L1,
    L2,
    L3,
    MAX
  };

  // ---------ramulator stats reported by cache---------
  // cache accesses
  ScalarStat cache_read_access;
  ScalarStat cache_write_access;
  ScalarStat cache_total_access;

  // cache misses and evictions
  ScalarStat cache_read_miss;
  ScalarStat cache_write_miss;
  ScalarStat cache_total_miss;
  ScalarStat cache_eviction;

  // cache MSHR usage
  ScalarStat cache_mshr_hit;
  ScalarStat cache_mshr_unavailable;

  // cache contention
  ScalarStat cache_set_unavailable;

  // ---------cache data members---------
  // which level (L1, L2, etc.) is this cache at?
  Level level;
  std::string level_string;

  // handle to cache system to get configuration, fire requests, etc.
  std::shared_ptr<CacheSystem> cachesys;

  // LLC has multiple higher caches
  std::vector<Cache*> higher_cache;

  // non-LLC has a cache below it
  Cache* lower_cache;

  // LLC
  std::shared_ptr<BaseLLC> llc;

  // L1, L2, L3 accumulated latencies
  // these are fixed in the simulation model
  int latency[int(Level::MAX)] = {4, 4 + 12, 4 + 12 + 31};
  int latency_each[int(Level::MAX)] = {4, 12, 31};

  // other helpers
  bool is_first_level;
  bool is_last_level;

  // ---------member functions---------
  Cache(int size,
        int assoc,
        int block_size,
        int mshr_entry_num,
        Level level,
        std::shared_ptr<CacheSystem> cachesys);

  void tick();

  bool send(Request req);

  void concatlower(Cache* lower);

  void callback(Request& req);
};

class CacheSystem {
 public:
  CacheSystem(const Config& configs, std::function<bool(Request)> send_memory)
      : send_memory(send_memory) {
    if (configs.has_core_caches()) {
      first_level = Cache::Level::L1;
    } else if (configs.has_l3_cache()) {
      first_level = Cache::Level::L3;
    } else {
      last_level = Cache::Level::MAX;  // no cache
    }

    if (configs.has_l3_cache()) {
      last_level = Cache::Level::L3;
    } else if (configs.has_core_caches()) {
      last_level = Cache::Level::L2;
    } else {
      last_level = Cache::Level::MAX;  // no cache
    }

    // 18-740
    if (configs.is_way_partitioning()) {
      cache_qos = Cache_QoS::way_partitioning;
    } else if (configs.is_custom()) {
      cache_qos = Cache_QoS::custom;
    } else {
      cache_qos = Cache_QoS::basic;
    }
  }

  // 18-740
  enum class Cache_QoS {
    basic,
    way_partitioning,
    custom
  } cache_qos;

  // wait_list contains miss requests with their latencies in
  // cache. When this latency is met, the send_memory function
  // will be called to send the request to the memory system.
  std::list<std::pair<long, Request>> wait_list;

  // hit_list contains hit requests with their latencies in cache.
  // callback function will be called when this latency is met and
  // set the instruction status to ready in processor's window.
  std::list<std::pair<long, Request>> hit_list;

  std::function<bool(Request)> send_memory;

  long clk = 0;
  void tick();

  Cache::Level first_level;
  Cache::Level last_level;
};

}  // namespace ramulator

#endif /* __CACHE_H */
