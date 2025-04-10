#pragma once

#include "mem/port.hh"
#include "params/MiCache.hh"
#include "sim/sim_object.hh"

#include "coherent_cache_base.hh"
#include "src_740/serializing_bus.hh"

#include <list>

namespace gem5 {

class MiCache : public CoherentCacheBase {
   public:
    MiCache(const MiCacheParams &params);

    // only one entry in cache, only one state variable needed
    enum class MiState {
        Invalid,
        Modified,
        Error
    } state = MiState::Invalid;

    // single entry cache = all bits are used for tag
    unsigned char data = 0;
    long tag = 0;
    bool dirty = false;

    unsigned char dataToWrite = 0;

    bool isHit(long addr);
    void allocate(long addr);
    void evict();
    
    // executed when the CPU sends a read/write request packet to this cache
    // @param pkt: the request packet
    void handleCoherentCpuReq(PacketPtr pkt) override;

    // executed when the bus grants access to this cache
    void handleCoherentBusGrant() override;

    // executed when the cache receives a response from the memory
    // @param pkt: the response packet
    void handleCoherentMemResp(PacketPtr pkt) override;

    // executed when the cache snoops a request on the shared bus
    // @param pkt: the snooped packet
    void handleCoherentSnoopedReq(PacketPtr pkt) override;
};
}