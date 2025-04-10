#pragma once

#include "mem/port.hh"
#include "params/MsiCache.hh"
#include "sim/sim_object.hh"

#include "coherent_cache_base.hh"
#include "src_740/serializing_bus.hh"

#include <list>

namespace gem5 {

class MsiCache : public CoherentCacheBase {
   public:
    MsiCache(const MsiCacheParams &params);

    // your cache storage, state machine etc here. See MiCache for reference.
    enum class MsiState {
        Invalid,
        Modified,
        Shared,
        Error
    } state = MsiState::Invalid;

    // single entry cache = all bits are used for tag
    unsigned char data = 0;
    long tag = 0;
    bool dirty = false;

    unsigned char dataToWrite = 0;

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
