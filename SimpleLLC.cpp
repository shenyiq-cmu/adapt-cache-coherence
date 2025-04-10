#include "SimpleLLC.h"

namespace ramulator {

SimpleLLC::SimpleLLC(int size, int assoc, int block_size, int num_mshr_entries)
    : BaseLLC(size, assoc, block_size, num_mshr_entries) {

  // Check size, block size and assoc are 2^N
  assert((size & (size - 1)) == 0);
  assert((block_size & (block_size - 1)) == 0);
  assert((assoc & (assoc - 1)) == 0);
  assert(size >= block_size);

  // Initialize cache configuration
  block_num = size / (block_size * assoc);
  index_mask = block_num - 1;
  index_offset = calc_log2(block_size);
  tag_offset = calc_log2(block_num) + index_offset;
}

bool SimpleLLC::send(Request& req, StatusReport& report) {

  // If there isn't a set, create it.
  auto& set = get_set(req.addr);
  Set::iterator line;

  // cache hit?
  if (is_hit(set, req.addr, &line)) {
    // lines in set need to be kept in LRU order
    // move hit line to back by inserting a copy at the end
    // updating the dirty bit if needed
    set.push_back(Line(req.addr,
                       get_tag(req.addr),
                       false,
                       line->dirty || (req.type == Request::Type::WRITE)));

    // then deleting the original line object
    set.erase(line);

    // IMPORTANT: record the cache hit
    report.hit = true;

    // acknowledge that this request was handled
    return true;

  } else {
    // IMPORTANT: record the miss
    if (req.type == Request::Type::WRITE) {
      report.write_miss = true;
    } else {
      assert(req.type == Request::Type::READ);
      report.read_miss = true;
    }

    bool dirty = (req.type == Request::Type::WRITE);

    if (req.type == Request::Type::WRITE) {
      req.type = Request::Type::READ;
    }

    // Is request already in MSHR?
    assert(req.type == Request::Type::READ);
    auto mshr = hit_mshr(req.addr);

    // If request is already waiting in MSHR, update dirty bit if needed and
    // finish
    if (mshr != mshr_entries.end()) {
      // IMPORTANT: record the MSHR hit
      report.mshr_hit = true;

      // update MSHR entry dirty bit (e.g. if it's a write request)
      mshr->second->dirty = dirty || mshr->second->dirty;

      // this request was handled successfully
      return true;
    }

    // Request wasn't in MSHR, so allocate a MSHR entry for it
    // Is there space in MSHR?
    if (mshr_entries.size() == num_mshr_entries) {
      // IMPORTANT: record that MSHR was full
      report.mshr_unavailable = true;

      // MSHR is full, stall this request
      return false;
    }

    // Check whether there is a line available for MSHR to fill once it gets the
    // result from memory
    if (all_locked(set)) {
      // IMPORTANT: record that no lines are available
      report.set_unavailable = true;

      // No space left for MSHR to use as destination, stall
      return false;
    }

    // allocate a new line for MSHR to eventually fill
    auto newline = allocate_line(set, req.addr, report);
    if (newline == set.end()) {
      // allocation failed, stall
      return false;
    }

    newline->dirty = dirty;

    // Add to MSHR entries
    mshr_entries.push_back(make_pair(req.addr, newline));

    // IMPORTANT: record that request was handled by allocating in MSHR
    report.mshr_allocated = true;
    return true;
  }
}

SimpleLLC::Set::iterator SimpleLLC::allocate_line(Set& set,
                                                  long addr,
                                                  StatusReport& report) {
  // See if an eviction is needed
  if (need_eviction(set, addr)) {
    // Get victim
    auto victim = find_if(
        set.begin(), set.end(), [this](Line line) { return !line.lock; });

    if (victim == set.end()) {
      return victim;  // doesn't exist a line that's already unlocked
                      // in each level
    }
    assert(victim != set.end());
    evict(&set, victim, report);
  }

  // Allocate newline, with lock bit on and dirty bit off
  set.push_back(Line(addr, get_tag(addr)));
  auto last_element = set.end();
  --last_element;
  return last_element;
}

void SimpleLLC::evict(Set* set, Set::iterator victim, StatusReport& report) {
  // IMPORTANT: record this eviction
  report.evictions++;

  long addr = victim->addr;
  bool dirty = victim->dirty;

  if (dirty) {
    // IMPORTANT: record the request to memory
    report.requests.emplace_back(addr, Request::Type::WRITE);
  }

  set->erase(victim);
}

bool SimpleLLC::is_hit(Set& set, long addr, Set::iterator* pos_ptr) {
  auto pos = find_if(set.begin(), set.end(), [addr, this](Line l) {
    return (l.tag == get_tag(addr));
  });
  *pos_ptr = pos;
  if (pos == set.end()) {
    return false;
  }
  return !pos->lock;
}

bool SimpleLLC::need_eviction(const Set& set, long addr) {
  if (find_if(set.begin(), set.end(), [addr, this](Line l) {
        return (get_tag(addr) == l.tag);
      }) != set.end()) {
    // Due to MSHR, the program can't reach here. Just for checking
    assert(false);
  } else {
    if (set.size() < assoc) {
      return false;
    } else {
      return true;
    }
  }
}

void SimpleLLC::callback(Request& req) {
  auto it = find_if(mshr_entries.begin(),
                    mshr_entries.end(),
                    [&req, this](std::pair<long, Set::iterator> mshr_entry) {
    return (align(mshr_entry.first) == align(req.addr));
  });

  if (it != mshr_entries.end()) {
    it->second->lock = false;
    mshr_entries.erase(it);
  }
}

int SimpleLLC::calc_log2(int val) {
  int n = 0;
  while ((val >>= 1)) n++;
  return n;
}

int SimpleLLC::get_index(long addr) {
  return (addr >> index_offset) & index_mask;
};

long SimpleLLC::get_tag(long addr) { return (addr >> tag_offset); }

// Align the address to cache line size
long SimpleLLC::align(long addr) { return (addr & ~(block_size - 1l)); }

SimpleLLC::Set& SimpleLLC::get_set(long addr) {
  if (sets.find(get_index(addr)) == sets.end()) {
    sets.insert(make_pair(get_index(addr), Set()));
  }
  return sets[get_index(addr)];
}

std::vector<std::pair<long, SimpleLLC::Set::iterator>>::iterator
SimpleLLC::hit_mshr(long addr) {
  auto mshr_it =
      find_if(mshr_entries.begin(),
              mshr_entries.end(),
              [addr, this](std::pair<long, Set::iterator> mshr_entry) {
        return (align(mshr_entry.first) == align(addr));
      });
  return mshr_it;
}

bool SimpleLLC::all_locked(const Set& set) {
  if (set.size() < assoc) {
    return false;
  }
  for (const auto& line : set) {
    if (!line.lock) {
      return false;
    }
  }
  return true;
}
}