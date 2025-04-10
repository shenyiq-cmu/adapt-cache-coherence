#include "src_740/msi_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"

namespace gem5 {

MsiCache::MsiCache(const MsiCacheParams& params) 
: CoherentCacheBase(params) {}


bool MsiCache::isHit(long addr) {
    // hit if tag matches and state is modified or shared
    return state != MsiState::Invalid && tag == addr;
}

void MsiCache::allocate(long addr) {
    tag = addr;
    dirty = false;
}

void MsiCache::evict() {
    // write back only when modified
    if ((state == MsiState::Modified) && dirty) {
        state = MsiState::Invalid;
        writeback();     
    }
    else if(state == MsiState::Shared){
        state = MsiState::Invalid;
    }
}

void MsiCache::writeback(){
    if ((state == MsiState::Modified) && dirty) {
        dirty = false;
        DPRINTF(CCache, "Msi[%d] writeback %#x, %d\n\n", cacheId, tag, data);
        bus->sendWriteback(cacheId, tag, data);        
    }
}


void MsiCache::handleCoherentCpuReq(PacketPtr pkt) {
    DPRINTF(CCache, "Msi[%d] cpu req: %s\n\n", cacheId, pkt->print());
       
    blocked = true; // stop accepting new reqs from CPU until this one is done

    long addr = pkt->getAddr();
    bool isRead = pkt->isRead();
    bool cacheHit = isHit(addr);

    if (cacheHit) {
        
        assert(state != MsiState::Invalid);
        

        if (isRead) {
            // no state change or snoop needed, reply now
            pkt->makeResponse();
            DPRINTF(CCache, "Msi[%d] M read hit %#x\n\n", cacheId, addr);
            
            // if hit, the other cpu can be only in S or I, no need snoop
            pkt->setData(&data);

            // return the response packet to CPU
            sendCpuResp(pkt);

            // start accepting new requests
            blocked = false;
        }
        else {
            DPRINTF(CCache, "Msi[%d] M write hit %#x\n\n", cacheId, addr);
            dirty = true;
            data = *pkt->getPtr<unsigned char>();
            
            if(state == MsiState::Shared){
                // if shared, need to invalidate the other cpu's cache
                requestPacket = pkt;
                bus->request(cacheId);
            }
            else{
                // if modified, nothing to do, reply now
                pkt->makeResponse();
                                
                sendCpuResp(pkt);

                blocked = false;
            }
            

        }


    }
    else {
        DPRINTF(CCache, "Msi[%d] cache miss %#x\n\n", cacheId, addr);
        // if invalidate, need to "read" from memory, may change other cache state
        requestPacket = pkt;
        if (pkt->isWrite()) {
            dataToWrite = *pkt->getPtr<unsigned char>();
        }

        // request bus access
        // this will lead to handleCoherentBusGrant() being called eventually
        bus->request(cacheId);
    }
}


void MsiCache::handleCoherentBusGrant() {
    DPRINTF(CCache, "Msi[%d] bus granted\n\n", cacheId);
    assert(requestPacket != nullptr);
    assert(cacheId == bus->currentGranted);

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

void MsiCache::handleCoherentMemResp(PacketPtr pkt) {
    DPRINTF(CCache, "Msi[%d] mem resp: %s\n", cacheId, pkt->print());

    if(state != MsiState::Shared || pkt->isRead()){
        assert(!isHit(pkt->getAddr()));
    }
    
    // Potentially sends a writeback to memory.
    if(!isHit(pkt->getAddr())){
        // evict when miss
        evict();

        // allocate new
        allocate(pkt->getAddr());
    }

    bool isRead = pkt->isRead();
    if (isRead) {
        data = *pkt->getPtr<unsigned char>();
        DPRINTF(CCache, "Msi[%d] got data %d from read\n\n", cacheId, data);
        // read, now in shared state
        state = MsiState::Shared;
    }
    else {
        // do not read data from a write response packet. Use stored value.
        DPRINTF(CCache, "Msi[%d] storing %d in cache\n\n", cacheId, dataToWrite);
        data = dataToWrite;

        // update dirty bit
        dirty = true;
        state = MsiState::Modified;
    }

    // the CPU has been waiting for a response. Send it this one.
    sendCpuResp(pkt);
    
    // release the bus so other caches can use it
    bus->release(cacheId);

    // start accepting new requests
    blocked = false;
}

void MsiCache::handleCoherentSnoopedReq(PacketPtr pkt) {
    DPRINTF(CCache, "Msi[%d] snoop: %s\n", cacheId, pkt->print());

    bool snoopHit = isHit(pkt->getAddr());
    bool isRemoteRead = pkt->isRead();

    // cache snooped a request on the shared bus. Update internal state if needed.
    // only need to care about snoop hit on M
    if (snoopHit) {

        if(isRemoteRead){
            //remote read
            if(state == MsiState::Modified){
                writeback(); // not evict, actually a write back
                state = MsiState::Shared;
            }
            DPRINTF(CCache, "Msi[%d] snoop hit! Modified to shared\n\n", cacheId);

        }
        else{
            // write
            // will write back only when modified
            writeback();
            state = MsiState::Invalid;
            DPRINTF(CCache, "Msi[%d] snoop hit! Invalidate\n\n", cacheId);
        }
    }
    else {
        DPRINTF(CCache, "Msi[%d] snoop miss! nothing to do\n\n", cacheId);
    }
}



}