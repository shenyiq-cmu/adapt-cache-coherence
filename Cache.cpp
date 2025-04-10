#include "Cache.h"

#ifndef DEBUG_CACHE
#define debug(...)
#else
#define debug(...)                               \
  do {                                           \
    printf("\033[36m[DEBUG] %s ", __FUNCTION__); \
    printf(__VA_ARGS__);                         \
    printf("\033[0m\n");                         \
  } while (0)
#endif

namespace ramulator {

Cache::Cache(int size,
             int assoc,
             int block_size,
             int mshr_entry_num,
             Level level,
             std::shared_ptr<CacheSystem> cachesys)
    : level(level), cachesys(cachesys), higher_cache(0), lower_cache(nullptr) {

  if (cachesys->cache_qos == CacheSystem::Cache_QoS::basic) {
    llc = std::make_shared<SimpleLLC>(size, assoc, block_size, mshr_entry_num);
  } else if (cachesys->cache_qos == CacheSystem::Cache_QoS::way_partitioning) {
    llc = std::make_shared<WaypartLLC>(size, assoc, block_size, mshr_entry_num);
  } else if (cachesys->cache_qos == CacheSystem::Cache_QoS::custom) {
    llc = std::make_shared<CustomLLC>(size, assoc, block_size, mshr_entry_num);
  } else {
    std::terminate();
  }

  debug("level %d size %d assoc %d block_size %d\n",
        int(level),
        llc->size,
        llc->assoc,
        llc->block_size);

  if (level == Level::L1) {
    level_string = "L1";
  } else if (level == Level::L2) {
    level_string = "L2";
  } else if (level == Level::L3) {
    level_string = "L3";
  }

  is_first_level = (level == cachesys->first_level);
  is_last_level = (level == cachesys->last_level);

  // regStats
  cache_read_miss.name(level_string + string("_cache_read_miss"))
      .desc("cache read miss count")
      .precision(0);

  cache_write_miss.name(level_string + string("_cache_write_miss"))
      .desc("cache write miss count")
      .precision(0);

  cache_total_miss.name(level_string + string("_cache_total_miss"))
      .desc("cache total miss count")
      .precision(0);

  cache_eviction.name(level_string + string("_cache_eviction"))
      .desc("number of evict from this level to lower level")
      .precision(0);

  cache_read_access.name(level_string + string("_cache_read_access"))
      .desc("cache read access count")
      .precision(0);

  cache_write_access.name(level_string + string("_cache_write_access"))
      .desc("cache write access count")
      .precision(0);

  cache_total_access.name(level_string + string("_cache_total_access"))
      .desc("cache total access count")
      .precision(0);

  cache_mshr_hit.name(level_string + string("_cache_mshr_hit"))
      .desc("cache mshr hit count")
      .precision(0);
  cache_mshr_unavailable.name(level_string + string("_cache_mshr_unavailable"))
      .desc("cache mshr not available count")
      .precision(0);
  cache_set_unavailable.name(level_string + string("_cache_set_unavailable"))
      .desc("cache set not available")
      .precision(0);
}

bool Cache::send(Request req) {
  debug("level %d req.addr %lx req.type %d, index %d, tag %ld",
        int(level),
        req.addr,
        int(req.type),
        get_index(req.addr),
        get_tag(req.addr));

  cache_total_access++;
  if (req.type == Request::Type::WRITE) {
    cache_write_access++;
  } else {
    assert(req.type == Request::Type::READ);
    cache_read_access++;
  }

  StatusReport report;
  bool handled = llc->send(req, report);
  report.update_send_stats(cache_total_miss,
                           cache_write_miss,
                           cache_read_miss,
                           cache_mshr_hit,
                           cache_mshr_unavailable,
                           cache_set_unavailable);

  // basic correctness checks

  if (report.read_miss || report.write_miss) {
    debug("miss @level %d", int(level));
  }

  if (report.hit) {
    cachesys->hit_list.push_back(
        make_pair(cachesys->clk + latency[int(level)], req));

    debug("hit, update timestamp %ld", cachesys->clk);
    debug("hit finish time %ld", cachesys->clk + latency[int(level)]);
  }

  if (report.mshr_hit) {
    debug("hit mshr");
  }

  if (report.mshr_unavailable) {
    debug("no mshr entry available");
  }

  if (report.mshr_allocated) {
    if (!is_last_level) {
      if (!lower_cache->send(req)) {
        llc->retry_list.push_back(req);
      }
    } else {
      cachesys->wait_list.push_back(
          make_pair(cachesys->clk + latency[int(level)], req));
    }
  }

  cache_eviction += report.evictions;

  // fire requests to memory
  for (auto& write_req : report.requests) {
    cachesys->wait_list.push_back(
        make_pair(cachesys->clk + latency[int(level)], write_req));

    debug(
        "inject one write request to memory system "
        "addr %lx, invalidate time %ld, issue time %ld",
        write_req.addr,
        0,
        cachesys->clk + latency[int(level)]);
  }

  return handled;
}

void Cache::concatlower(Cache* lower) {
  lower_cache = lower;
  assert(lower != nullptr);
  lower->higher_cache.push_back(this);
};

void Cache::callback(Request& req) {
  debug("level %d", int(level));

  llc->callback(req);

  if (higher_cache.size()) {
    for (auto hc : higher_cache) {
      hc->callback(req);
    }
  }
}

void Cache::tick() {

  if (!lower_cache->is_last_level) lower_cache->tick();

  for (auto it = llc->retry_list.begin(); it != llc->retry_list.end(); it++) {
    if (lower_cache->send(*it)) it = llc->retry_list.erase(it);
  }
}

void CacheSystem::tick() {
  debug("clk %ld", clk);

  ++clk;

  // Sends ready waiting request to memory
  auto it = wait_list.begin();
  while (it != wait_list.end() && clk >= it->first) {
    if (!send_memory(it->second)) {
      ++it;
    } else {

      debug("complete req: addr %lx", (it->second).addr);

      it = wait_list.erase(it);
    }
  }

  // hit request callback
  it = hit_list.begin();
  while (it != hit_list.end()) {
    if (clk >= it->first) {
      it->second.callback(it->second);

      debug("finish hit: addr %lx", (it->second).addr);

      it = hit_list.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace ramulator
