#include "src_740/mesi_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"
#define CACHE_START 0x8000

namespace gem5 {

MesiCache::MesiCache(const MesiCacheParams& params) 
: CoherentCacheBase(params) {
    for(int i = 0; i < 4096; i++){
        share[i] = 0;
    }
}

bool MesiCache::isHit(long addr) {
    // hit if tag matches and state is modified or shared
    return state != MesiState::Invalid && tag == addr;
}

void MesiCache::allocate(long addr) {
    tag = addr;
    dirty = false;
}

void MesiCache::evict() {
    // write back only when modified
    if ((state == MesiState::Modified) && dirty) {
        state = MesiState::Invalid;
        writeback();     
    }
    else if(state == MesiState::Shared || state == MesiState::Exclusive){
        state = MesiState::Invalid;
    }
}

void MesiCache::writeback(){
    if ((state == MesiState::Modified) && dirty) {
        dirty = false;
        DPRINTF(CCache, "Msi[%d] writeback %#x, %d\n\n", cacheId, tag, data);
        bus->sendWriteback(cacheId, tag, data);        
    }
}


void MesiCache::handleCoherentCpuReq(PacketPtr pkt) {
    DPRINTF(CCache, "Mesi[%d] cpu req: %s\n\n", cacheId, pkt->print());
    // your implementation here. See MiCache/MsiCache for reference.
        blocked = true; // stop accepting new reqs from CPU until this one is done

    long addr = pkt->getAddr();
    bool isRead = pkt->isRead();
    bool cacheHit = isHit(addr);

    if (cacheHit) {
        
        assert(state != MesiState::Invalid);
        

        if (isRead) {
            // no state change or snoop needed, reply now
            pkt->makeResponse();
            DPRINTF(CCache, "Mesi[%d] M read hit %#x\n\n", cacheId, addr);
            
            // if hit, the other cpu can be only in S or I, no need snoop
            pkt->setData(&data);

            // return the response packet to CPU
            sendCpuResp(pkt);

            // start accepting new requests
            blocked = false;
        }
        else {
            DPRINTF(CCache, "Mesi[%d] M write hit %#x\n\n", cacheId, addr);
            dirty = true;
            data = *pkt->getPtr<unsigned char>();
            
            if(state == MesiState::Shared){
                // if shared, need to invalidate the other cpu's cache
                assert(share[pkt->getAddr() - CACHE_START] == 1);
                requestPacket = pkt;
                bus->request(cacheId);
            }
            else{
                // if modified or exclusive nothing to do, reply now
                assert(share[pkt->getAddr() - CACHE_START] == 0);
                state = MesiState::Modified;

                pkt->makeResponse();
                                
                sendCpuResp(pkt);

                blocked = false;
            }
            
        }

    }
    else {
        DPRINTF(CCache, "Mesi[%d] cache miss %#x\n\n", cacheId, addr);
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


void MesiCache::handleCoherentBusGrant() {
    DPRINTF(CCache, "Mesi[%d] bus granted\n\n", cacheId);
    // your implementation here. See MiCache/MsiCache for reference.
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

void MesiCache::handleCoherentMemResp(PacketPtr pkt) {
    DPRINTF(CCache, "Mesi[%d] mem resp: %s\n", cacheId, pkt->print());
    // your implementation here. See MiCache/MsiCache for reference.
    if(state != MesiState::Shared || pkt->isRead()){
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
        DPRINTF(CCache, "Mesi[%d] got data %d from read\n\n", cacheId, data);
        // read, now in shared state
        int share_entry = pkt->getAddr() - CACHE_START;
        if(share[share_entry]){
            // already shared with others
            state = MesiState::Shared;
        }
        else{
            // owned by myself
            state = MesiState::Exclusive;
        }
        
    }
    else {
        // do not read data from a write response packet. Use stored value.
        DPRINTF(CCache, "Mesi[%d] storing %d in cache\n\n", cacheId, dataToWrite);
        data = dataToWrite;

        // when write, this cpu know that the other CPU will evict, so I have the only copy
        share[pkt->getAddr() - CACHE_START] = 0;

        // update dirty bit
        dirty = true;
        state = MesiState::Modified;
    }

    // the CPU has been waiting for a response. Send it this one.
    sendCpuResp(pkt);

    // release the bus so other caches can use it
    bus->release(cacheId);

    // start accepting new requests
    blocked = false;
}

void MesiCache::handleCoherentSnoopedReq(PacketPtr pkt) {
    DPRINTF(CCache, "Mesi[%d] snoop: %s\n", cacheId, pkt->print());
    // your implementation here. See MiCache/MsiCache for reference.
    bool snoopHit = isHit(pkt->getAddr());
    bool isRemoteRead = pkt->isRead();

    // cache snooped a request on the shared bus. Update internal state if needed.
    // only need to care about snoop hit on M
    if (snoopHit) {
        // the other cpu either read or write on this address, it has the shared copy
        share[pkt->getAddr() - CACHE_START] = 1;

        if(isRemoteRead){
            //remote read
            if(state == MesiState::Modified){
                writeback(); // not evict, actually a write back
                state = MesiState::Shared;
            }
            else if(state == MesiState::Exclusive){
                state = MesiState::Shared;
            }
            DPRINTF(CCache, "Mesi[%d] snoop hit! Modified to shared\n\n", cacheId);

        }
        else{
            // write
            // will write back only when modified
            writeback();
            state = MesiState::Invalid;
            DPRINTF(CCache, "Mesi[%d] snoop hit! Invalidate\n\n", cacheId);
        }
    }
    else {
        DPRINTF(CCache, "Mesi[%d] snoop miss! nothing to do\n\n", cacheId);
    }
}



}