#include "src_740/mi_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"

namespace gem5 {

MiCache::MiCache(const MiCacheParams& params) 
: CoherentCacheBase(params) {}

// here, Modified is the only valid state, so we don't need a explicit Valid bit
bool MiCache::isHit(long addr) {
    return state == MiState::Modified && tag == addr;
}

void MiCache::allocate(long addr) {
    tag = addr;
    dirty = false;
}

void MiCache::evict() {
    // TODO: if line is Modified and dirty, write back dirty data.
    // The bus includes special handling of writebacks since only one of the snoopers can potentially
    // have a line in M state. So, since there is no contention for writebacks, we don't need to request bus access.
    // Just call sendWriteback directly.
    if ((state == MiState::Modified) && dirty) {
        dirty = false;
        state = MiState::Invalid;
        DPRINTF(CCache, "Mi[%d] writeback %#x, %d\n\n", cacheId, tag, data);
        bus->sendWriteback(cacheId, tag, data);        
    }
}

void MiCache::handleCoherentCpuReq(PacketPtr pkt) {
    DPRINTF(CCache, "Mi[%d] cpu req: %s\n\n", cacheId, pkt->print());
    blocked = true; // stop accepting new reqs from CPU until this one is done

    long addr = pkt->getAddr();
    bool isRead = pkt->isRead();
    bool cacheHit = isHit(addr);

    if (cacheHit) {
        // M is the only valid state, must be M to hit
        assert(state == MiState::Modified);
        // cache was hit, cache can respond without memory.
        // Turn this gem5 Request packet in-place into a Response packet.
        // ReadReq -> ReadResp, WriteReq -> WriteResp
        // Need to return a response to CPU for BOTH read and write, otherwise it'll stall.
        pkt->makeResponse();

        if (isRead) {
            DPRINTF(CCache, "Mi[%d] M read hit %#x\n\n", cacheId, addr);
            // set response data to cached value. This will be returned to CPU.
            pkt->setData(&data);
        }
        else {
            DPRINTF(CCache, "Mi[%d] M write hit %#x\n\n", cacheId, addr);
            dirty = true;
            // this cache already has the line in M, so must be exclusive, no need to send to snoop bus.
            // writeback cache: no need to send to memory, just update cache data using packet data.
            data = *pkt->getPtr<unsigned char>();
        }

        // return the response packet to CPU
        sendCpuResp(pkt);

        // start accepting new requests
        blocked = false;
    }
    else {
        DPRINTF(CCache, "Mi[%d] cache miss %#x\n\n", cacheId, addr);
        // cache needs to do a memory request for both read and write
        // so that other caches can snoop it since it is allocating a new block.

        // Only evict/allocate new block AFTER bus is granted and BEFORE bus is released,
        // since a snoop for this addr could come in the middle.

        // In this implementation, the cache only evicts/allocates once memory response is received.

        // store the packet, the data to write and request bus access
        // store data to write since getting data from a DRAM write response packet may not work.
        requestPacket = pkt;
        if (pkt->isWrite()) {
            dataToWrite = *pkt->getPtr<unsigned char>();
        }

        // request bus access
        // this will lead to handleCoherentBusGrant() being called eventually
        bus->request(cacheId);
    }
}


void MiCache::handleCoherentBusGrant() {
    DPRINTF(CCache, "Mi[%d] bus granted\n\n", cacheId);
    assert(requestPacket != nullptr);
    assert(cacheId == bus->currentGranted);

    // bus was granted, send the req to memory and cause other caches to snoop this req.
    // this send is guaranteed to succeed since the bus 
    // belongs to this cache for now.

    bool isRead = requestPacket->isRead();
    if (isRead) {
        bus->sendMemReq(requestPacket, true);
    }
    else {
        // optimization: write request doesn't actually need to go to memory, only needs to cause snoops.
        // so use False as second arg to stop bus from sending to memory.
        // It will still cause other caches to snoop, forcing M->I for other caches.
        // This is correct since this is a writeback cache, so will update memory when ->I

        // Also correct to send the write to memory anyway, but it's an unneeded write.
        bus->sendMemReq(requestPacket, false);

    }
}

void MiCache::handleCoherentMemResp(PacketPtr pkt) {
    DPRINTF(CCache, "Mi[%d] mem resp: %s\n", cacheId, pkt->print());

    // In MI, mem req only happens on cache miss
    assert(!isHit(pkt->getAddr()));

    // since this happened on miss, evict old block
    // Potentially sends a writeback to memory.
    evict();

    // allocate new
    allocate(pkt->getAddr());

    // now in M state
    state = MiState::Modified;

    bool isRead = pkt->isRead();
    if (isRead) {
        data = *pkt->getPtr<unsigned char>();
        DPRINTF(CCache, "Mi[%d] got data %d from read\n\n", cacheId, data);
    }
    else {
        // do not read data from a write response packet. Use stored value.
        DPRINTF(CCache, "Mi[%d] storing %d in cache\n\n", cacheId, dataToWrite);
        data = dataToWrite;

        // update dirty bit
        dirty = true;
    }

    // the CPU has been waiting for a response. Send it this one.
    sendCpuResp(pkt);
    
    // release the bus so other caches can use it
    bus->release(cacheId);

    // start accepting new requests
    blocked = false;
}

void MiCache::handleCoherentSnoopedReq(PacketPtr pkt) {
    DPRINTF(CCache, "Mi[%d] snoop: %s\n", cacheId, pkt->print());

    bool snoopHit = isHit(pkt->getAddr());

    // cache snooped a request on the shared bus. Update internal state if needed.
    // only need to care about snoop hit on M
    if (snoopHit) {
        // must be M to hit
        assert(state == MiState::Modified);
        DPRINTF(CCache, "Mi[%d] snoop hit! invalidate\n\n", cacheId);

        // evict block, cause writeback if dirty
        evict();

        // invalidate
        state = MiState::Invalid;
    }
    else {
        DPRINTF(CCache, "Mi[%d] snoop miss! nothing to do\n\n", cacheId);
    }
}



}
