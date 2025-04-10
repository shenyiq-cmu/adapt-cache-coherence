#include "src_740/mesi_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"

namespace gem5 {

MesiCache::MesiCache(const MesiCacheParams& params) 
: CoherentCacheBase(params) {}


void MesiCache::handleCoherentCpuReq(PacketPtr pkt) {
    DPRINTF(CCache, "Mesi[%d] cpu req: %s\n\n", cacheId, pkt->print());
    // your implementation here. See MiCache/MsiCache for reference.
}


void MesiCache::handleCoherentBusGrant() {
    DPRINTF(CCache, "Mesi[%d] bus granted\n\n", cacheId);
    // your implementation here. See MiCache/MsiCache for reference.
}

void MesiCache::handleCoherentMemResp(PacketPtr pkt) {
    DPRINTF(CCache, "Mesi[%d] mem resp: %s\n", cacheId, pkt->print());
    // your implementation here. See MiCache/MsiCache for reference.
}

void MesiCache::handleCoherentSnoopedReq(PacketPtr pkt) {
    DPRINTF(CCache, "Mesi[%d] snoop: %s\n", cacheId, pkt->print());
    // your implementation here. See MiCache/MsiCache for reference.
}



}