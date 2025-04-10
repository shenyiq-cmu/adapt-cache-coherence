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
    
    void handleCoherentCpuReq(PacketPtr pkt) override;
    void handleCoherentBusGrant() override;
    void handleCoherentMemResp(PacketPtr pkt) override;
    void handleCoherentSnoopedReq(PacketPtr pkt) override;
};
}