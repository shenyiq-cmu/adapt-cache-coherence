#include "WaypartLLC.h"

namespace ramulator {

WaypartLLC::WaypartLLC(int size,
                       int assoc,
                       int block_size,
                       int num_mshr_entries)
    : BaseLLC(size, assoc, block_size, num_mshr_entries) {}

// 18-740: Way Partitioning - implement send request - refer SimpleLLC.cpp 
bool WaypartLLC::send(Request& req, StatusReport& report) { return false; }

// 18-740 - consult SimpleLLC for callback implementation
void WaypartLLC::callback(Request& req) {}

// 18-740 other methods here
}
