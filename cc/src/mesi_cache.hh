#pragma once

#include "mem/port.hh"
#include "params/MesiCache.hh"
#include "sim/sim_object.hh"

#include "src_740/coherent_cache_base.hh"
#include "src_740/serializing_bus.hh"

#include <list>
#include <vector>
#include <unordered_map>

namespace gem5 {

// Define the cache states
enum MesiState {
    INVALID = 0,
    EXCLUSIVE = 1,    // E: Exclusive clean
    MODIFIED = 2,     // M: Modified (dirty)
    SHARED_CLEAN = 3, // Sc: Shared clean
    SHARED_MOD = 4    // Sm: Shared modified
};

class MesiCache : public CoherentCacheBase {
private:
    // Single address cache
    bool valid;             // Whether the cache line is valid
    Addr cachedAddr;        // The address currently in cache
    int cacheState;         // State of the cache line (using enum MesiState)
    unsigned char cacheData; // The actual data stored

public:
    MesiCache(const MesiCacheParams &params);

    // coherence state machine implementation
    void handleCoherentCpuReq(PacketPtr pkt) override;
    void handleCoherentBusGrant() override;
    void handleCoherentMemResp(PacketPtr pkt) override;
    void handleCoherentSnoopedReq(PacketPtr pkt) override;
    
    // Helper method to get state name for logging
    const char* getStateName(int state) {
        switch (state) {
            case INVALID: return "INVALID";
            case EXCLUSIVE: return "EXCLUSIVE";
            case MODIFIED: return "MODIFIED";
            case SHARED_CLEAN: return "SHARED_CLEAN";
            case SHARED_MOD: return "SHARED_MOD";
            default: return "UNKNOWN";
        }
    }
};

}