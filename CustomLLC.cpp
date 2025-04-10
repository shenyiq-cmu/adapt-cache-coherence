#include "CustomLLC.h"

namespace ramulator {

CustomLLC::CustomLLC(int size, int assoc, int block_size, int num_mshr_entries)
    : BaseLLC(size, assoc, block_size, num_mshr_entries) {}

bool CustomLLC::send(Request& req, StatusReport& report) { return false; }

void CustomLLC::callback(Request& req) {}
}