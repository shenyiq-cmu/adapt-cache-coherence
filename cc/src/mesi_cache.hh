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
};

}