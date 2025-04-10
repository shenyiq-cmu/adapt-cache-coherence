#pragma once

#include "mem/port.hh"
#include "params/MesiCache.hh"
#include "sim/sim_object.hh"

#include "coherent_cache_base.hh"
#include "src_740/serializing_bus.hh"

#include <list>

namespace gem5 {

class MesiCache : public CoherentCacheBase {
   public:
    MesiCache(const MesiCacheParams &params);

    // coherence state machine, data storage etc. here
    enum class MesiState {
        Invalid,
        Modified,
        Shared,
        Exclusive,
        Error
    } state = MesiState::Invalid;

    // single entry cache = all bits are used for tag
    unsigned char data = 0;
    long tag = 0;
    bool dirty = false;

    unsigned char dataToWrite = 0;

    bool share[4096];

    bool isHit(long addr);
    void allocate(long addr);
    void evict();
    void writeback();
    
    void handleCoherentCpuReq(PacketPtr pkt) override;
    void handleCoherentBusGrant() override;
    void handleCoherentMemResp(PacketPtr pkt) override;
    void handleCoherentSnoopedReq(PacketPtr pkt) override;
};
}