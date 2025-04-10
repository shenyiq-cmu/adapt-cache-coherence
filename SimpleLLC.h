#pragma once

#include "BaseLLC.h"

#include <list>
#include <map>
#include <algorithm>

namespace ramulator {

struct SimpleLLC : public BaseLLC {
  //------ Internal Types ------
  // definition of one cache line
  struct Line {
    long addr;
    long tag;

    // When the lock is on, the line is waiting for data from memory
    bool lock;

    bool dirty;

    Line(long addr, long tag)
        : addr(addr), tag(tag), lock(true), dirty(false) {}
    Line(long addr, long tag, bool lock, bool dirty)
        : addr(addr), tag(tag), lock(lock), dirty(dirty) {}
  };

  // a cache set is a list of cache lines
  using Set = std::list<Line>;

  //------ Member Variables ------
  // helpers derived from configuration
  unsigned int block_num;
  unsigned int index_mask;
  unsigned int index_offset;
  unsigned int tag_offset;

  // cache data storage
  std::map<int, Set> sets;

  // MSHR
  std::vector<std::pair<long, Set::iterator>> mshr_entries;

  // ------ Core Methods ------
  SimpleLLC(int size, int assoc, int block_size, int num_mshr_entries);

  virtual bool send(Request& req, StatusReport& report) override;

  virtual void callback(Request& req) override;

  // ------ Helpers ------
  bool is_hit(Set& set, long addr, Set::iterator* pos_ptr);

  Set::iterator allocate_line(Set& set, long addr, StatusReport& report);

  void evict(Set* set, Set::iterator victim, StatusReport& report);

  bool need_eviction(const Set& set, long addr);

  // ------ Misc Helpers ------
  int calc_log2(int val);

  int get_index(long addr);

  long get_tag(long addr);

  long align(long addr);

  Set& get_set(long addr);

  std::vector<std::pair<long, Set::iterator>>::iterator hit_mshr(long addr);

  bool all_locked(const Set& set);
};
}