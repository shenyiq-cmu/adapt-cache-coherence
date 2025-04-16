#include "src_740/mesi_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"
#include <vector>
#define CACHE_START 0x8000
#define NOT_EXIST -1

// cache coherence protocol and cache replacement protocol orthogonol
// a cache line can still exist in the cache, but invalidated

namespace gem5 {

MesiCache::MesiCache(const MesiCacheParams& params) 
: CoherentCacheBase(params),
  blockOffset(params.blockOffset),
  setBit(params.setBit),
  cacheSizeBit(params.cacheSizeBit) {
    // for(int i = 0; i < 4096; i++){
    //     share[i] = 0;
    // }
    
    // set up cache data structure
    blockSize = 0x1 << blockOffset;
    numSets = 0x1 << setBit;
    cacheSize = 0x1 << cacheSizeBit;
    numLines =  cacheSize / numSets / blockSize;

    DPRINTF(CCache, "blocksize: %d, setsize: %d, cachsize: %d\n\n", blockSize, numLines, cacheSize);
    // std::cerr<<"print here"<<std::endl;
    // std::cout<<"print here"<<std::endl;
    // init cache manager
    MesiCacheMgr.resize(numSets);
    
    // init cache set manager
    for(auto &setMgr : MesiCacheMgr){
        setMgr.clkPtr = 0;
        setMgr.cacheSet.resize(numLines);
        // init cache lines
        for(auto &cacheline : setMgr.cacheSet){
            cacheline.tag = 0;
            cacheline.clkFlag = 0;
            cacheline.dirty = 0;
            cacheline.valid = false;
            cacheline.cohState = MesiState::Invalid;
            cacheline.cacheBlock.resize(blockSize);
        }
    }

    dataToWrite.resize(blockSize);

    bus->cacheBlockSize = blockSize;

    
}

void MesiCache::printDataHex(uint8_t* data, int length){
    DPRINTF(CCache, "DATA: ");
    for(int i = 0; i < length; i++){
        printf("%02x", data[i]);
    }
    printf("\n");
}

uint64_t MesiCache::getTag(long addr){
    return ((uint64_t)addr >> ((blockOffset + setBit)));
}

uint64_t MesiCache::getSet(long addr){
    uint64_t mask = (0x1 << (blockOffset + setBit)) - 1;
    return ((uint64_t)addr & mask) >> blockOffset;
}

uint64_t MesiCache::getBlkAddr(long addr){
    return ((addr >> blockOffset) << blockOffset);
}

bool MesiCache::isHit(long addr, int &lineID) {
    // hit if tag matches and state is modified or shared
    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    bool exist = false;

    if(MesiCacheMgr[setID].tagMap.find(tag) != MesiCacheMgr[setID].tagMap.end()){
        // a cache line can be at invalid coherence state but still exists in cache
        exist = true;
        lineID = MesiCacheMgr[setID].tagMap[tag];
    }
    else{
        lineID = NOT_EXIST;
    }

    return (exist && (MesiCacheMgr[setID].cacheSet[lineID].cohState != MesiState::Invalid));
}

int MesiCache::allocate(long addr) {
    // assume clk_ptr now points to a empty line
    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    int lineID = NOT_EXIST;
    cacheSetMgr &setMgr = MesiCacheMgr[setID];

    assert(setMgr.cacheSet[setMgr.clkPtr].valid == false);

    cacheLine &cline = setMgr.cacheSet[setMgr.clkPtr];
    cline.dirty = false;
    cline.clkFlag = 1;
    cline.cohState = MesiState::Invalid;
    cline.valid = true;
    cline.tag = tag;
    memset(&cline.cacheBlock[0], 0, blockSize);

    // update clk pointer of the circular array
    setMgr.tagMap[tag] = setMgr.clkPtr;
    lineID = setMgr.clkPtr;

    setMgr.clkPtr = (setMgr.clkPtr + 1) % numLines;

    DPRINTF(CCache, "Mesi[%d] allocate set: %d, way: %d for %#x\n\n", cacheId, setID, lineID, addr);

    return lineID;
}

void MesiCache::evict(long addr) {

    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    cacheSetMgr &setMgr = MesiCacheMgr[setID];

    if(setMgr.tagMap.size() < numLines){
        // still have unallocated lines
        return;
    }

    while(1){
        if(setMgr.cacheSet[setMgr.clkPtr].clkFlag == 1){
            setMgr.cacheSet[setMgr.clkPtr].clkFlag = 0;
        }
        else{
            // evict block
            cacheLine &cline = setMgr.cacheSet[setMgr.clkPtr];
            DPRINTF(CCache, "Mesi[%d] replaces set: %d, way: %d, block tag: %#x, for %#x\n\n", cacheId, setID, setMgr.clkPtr, cline.tag, addr);
            // write back if dirty
            if(cline.dirty){
                assert(cline.cohState == MesiState::Modified);
                writeback(addr, &cline.cacheBlock[0]);
            }

            // need to erase from map
            setMgr.tagMap.erase(cline.tag);

            cline.valid = false;
            // other fields reset by allocate

            

            break;
        }

        setMgr.clkPtr = (setMgr.clkPtr+1)%numLines;
    }

    // // write back only when modified
    // if ((state == MesiState::Modified) && dirty) {
    //     state = MesiState::Invalid;
    //     writeback();     
    // }
    // else if(state == MesiState::Shared || state == MesiState::Exclusive){
    //     state = MesiState::Invalid;
    // }
}

void MesiCache::writeback(long addr, uint8_t* data){ 
    
    bus->sendBlkWriteback(cacheId, getBlkAddr(addr), data, blockSize);
    
    DPRINTF(CCache, "Mesi[%d] writeback %#x with DATA\n\n", cacheId, addr);
    printDataHex(data, blockSize);
}


void MesiCache::handleCoherentCpuReq(PacketPtr pkt) {
    DPRINTF(CCache, "Mesi[%d] cpu req: %s\n\n", cacheId, pkt->print());
    std::cerr<< "cache: "<<cacheId<<"handleCPUreq"<<std::endl;
    // your implementation here. See MiCache/MsiCache for reference.
    blocked = true; // stop accepting new reqs from CPU until this one is done
    int lineID;
    long addr = pkt->getAddr();
    bool isRead = pkt->isRead();

    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    bool cacheHit = isHit(addr, lineID);

    if (cacheHit) {

        cacheLine &currCacheline = MesiCacheMgr[setID].cacheSet[lineID];
        
        assert(currCacheline.cohState != MesiState::Invalid);
        

        if (isRead) {
            // no state change or snoop needed, reply now
            pkt->makeResponse();
            DPRINTF(CCache, "Mesi[%d] read hit %#x, set: %d, line: %d\n\n", cacheId, addr, setID, lineID);
            
            // if hit, the other cpu can be only in S or I, no need snoop
            pkt->setDataFromBlock(&currCacheline.cacheBlock[0], blockSize);

            // cache line update
            currCacheline.clkFlag = 1;

            // return the response packet to CPU
            sendCpuResp(pkt);

            // start accepting new requests
            blocked = false;
        }
        else {
            

            // pkt->writeDataToBlock(&MesiCacheMgr[setID].cacheSet[lineID].cacheBlock[0], blockSize);
            
            if(currCacheline.cohState == MesiState::Shared){
                // if shared, need to invalidate the other cpu's cache
                // assert(share[pkt->getAddr() - CACHE_START] == 1);
                DPRINTF(CCache, "Mesi[%d] write need invalidate others %#x\n\n", cacheId, addr);
                requestPacket = pkt;
                pkt->writeDataToBlock(&dataToWrite[0], blockSize);
                bus->request(cacheId);
            }
            else{
                // if modified or exclusive nothing to do, reply now
                // assert(share[pkt->getAddr() - CACHE_START] == 0);
                DPRINTF(CCache, "Mesi[%d] write hit %#x, set: %d, line: %d\n\n", cacheId, addr, setID, lineID);

                if(currCacheline.cohState == MesiState::Exclusive){
                    DPRINTF(CCache, "STATE_PrWr: Mesi[%d] current line upgrade from Exclusive to Modified\n\n", cacheId);                   
                }

                // cache line update
                currCacheline.cohState = MesiState::Modified;
                pkt->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
                currCacheline.dirty = true;
                currCacheline.clkFlag = 1;

                pkt->makeResponse();
                                
                sendCpuResp(pkt);

                blocked = false;
            }
            
        }

    }
    else {
        // miss
        DPRINTF(CCache, "Mesi[%d] cache miss %#x\n\n", cacheId, addr);
        // if invalidate, need to "read" from memory, may change other cache state
        requestPacket = pkt;
        if (pkt->isWrite()) {
            pkt->writeDataToBlock(&dataToWrite[0], blockSize);
        }

        // request bus access
        // this will lead to handleCoherentBusGrant() being called eventually
        bus->request(cacheId);
    }
}


void MesiCache::handleCoherentBusGrant() {
    DPRINTF(CCache, "Mesi[%d] bus granted\n\n", cacheId);
    std::cerr<< "cache: "<<cacheId<<"handleCOhbusgrant"<<std::endl;
    // your implementation here. See MiCache/MsiCache for reference.
    assert(requestPacket != nullptr);
    assert(cacheId == bus->currentGranted);
    // need to align memory access on block size
    uint64_t addr = requestPacket->getAddr();
    uint64_t blk_addr = requestPacket->getBlockAddr(blockSize);
    uint64_t size = requestPacket->getSize();
    std::cerr<< "addr: " << addr << "blk_addr" << blk_addr << "size: "<<size<<std::endl;
    // panic_if(addr - blk_addr + size > blockSize, "access span multiple lines");
    // requestPacket->setAddr(blk_addr);
    // requestPacket->setSize(blockSize);

    int lineID;

    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    bool cacheHit = isHit(addr, lineID);
    // cacheHit means a transition from shared to modified

    if(requestPacket->isRead()){
        DPRINTF(CCache, "Mesi[%d] broadcast BusRd for block address %#x\n\n", cacheId, blk_addr);
    }
    else{
        DPRINTF(CCache, "Mesi[%d] broadcast BusRdX for block address %#x\n\n", cacheId, blk_addr);
    }

    bus->sharedWire = false;

    if((addr == blk_addr && size == blockSize) || cacheHit){
        bool isRead = requestPacket->isRead();
        if (isRead) {
            // see second argument as whether I need data read from memory now
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
    else{

        bus->sendMemReq(requestPacket, true);
    
    }


}

void MesiCache::handleCoherentMemResp(PacketPtr respPacket) {
    DPRINTF(CCache, "Mesi[%d] mem resp: %s\n", cacheId, respPacket->print());
    std::cerr<< "cache: "<<cacheId<<"handleCOhMemreq"<<std::endl;
    // your implementation here. See MiCache/MsiCache for reference.
    int lineID;
    assert(requestPacket != nullptr);
    // for read requests, the response will only be read resp, just store to the corresponding cache line

    // for write requests, if the response is write resp, it means no memory read is performed, just write to the
    // current cache struct; if the response is read resp, it means we need to write memory data first then modify it
    long addr = requestPacket->getAddr();
    bool isRead = requestPacket->isRead();

    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    bool cacheHit = isHit(addr, lineID);

    bool memoryFetch = respPacket->isRead();

    // if it's a hit, a write request to shared state, update cache line state and return
    if(cacheHit){
        assert(lineID != NOT_EXIST);
        assert(MesiCacheMgr[setID].cacheSet[lineID].cohState == MesiState::Shared);
        assert(!memoryFetch);

        
        MesiCacheMgr[setID].cacheSet[lineID].cohState = MesiState::Modified;
        MesiCacheMgr[setID].cacheSet[lineID].dirty = true;
        MesiCacheMgr[setID].cacheSet[lineID].clkFlag = 1;
        // can only modify parts that requested
        requestPacket->writeDataToBlock(&MesiCacheMgr[setID].cacheSet[lineID].cacheBlock[0], blockSize);
        DPRINTF(CCache, "STATE_PrWr: Mesi[%d] storing DATA in cache and upgrade from Shared to Modified\n\n", cacheId);
        printDataHex(&MesiCacheMgr[setID].cacheSet[lineID].cacheBlock[0], blockSize);
        // memcpy(&MesiCacheMgr[setID].cacheSet[lineID].cacheBlock[0], &dataToWrite[0], blockSize);

        // the CPU has been waiting for a response. Send it this one.
        sendCpuResp(respPacket);

        // release the bus so other caches can use it
        bus->release(cacheId);

        // start accepting new requests
        blocked = false;

        return;
    }


    // if it's not a hit,
    // if cache line exists, update the original cache line state and data based on read/write
    if(lineID != NOT_EXIST){
        cacheLine &currCacheline = MesiCacheMgr[setID].cacheSet[lineID];
        assert(currCacheline.cohState == MesiState::Invalid);
        if(isRead){
            
            assert(memoryFetch);
            // decide on exclusive or shared based on snoop result
            currCacheline.cohState = (bus->sharedWire)? MesiState::Shared : MesiState::Exclusive;
            // reset shared wire
            bus->sharedWire = false;
            currCacheline.clkFlag = 1;
            // write data block from memory to cache structure
            respPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);

            if(currCacheline.cohState == MesiState::Exclusive){
                DPRINTF(CCache, "STATE_PrRd: Mesi[%d] got DATA from read and Invalid to Exclusive\n\n", cacheId);
            }
            else{
                DPRINTF(CCache, "STATE_PrRd: Mesi[%d] got DATA from read and Invalid to Shared\n\n", cacheId);
            }
            
            printDataHex(&currCacheline.cacheBlock[0], blockSize);
        }
        else{
            
            currCacheline.cohState = MesiState::Modified;
            currCacheline.dirty = true;
            currCacheline.clkFlag = 1;
            // memcpy(&currCacheline.cacheBlock[0], &dataToWrite[0], blockSize);
            // write data block from memory to cache structure
            if(memoryFetch){
                respPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
            }
            // modify
            requestPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);

            DPRINTF(CCache, "STATE_PrWr: Mesi[%d] storing DATA in cache, Invalid to Modified\n\n", cacheId);
            printDataHex(&currCacheline.cacheBlock[0], blockSize);

        }
    }
    else{

        // if cache line does not exist, evict and allocate, update cache line state and data
        evict(addr);

        lineID = allocate(addr);
        cacheLine &currCacheline = MesiCacheMgr[setID].cacheSet[lineID];
        assert(currCacheline.cohState == MesiState::Invalid);
        assert(currCacheline.valid);

        if(isRead){
            // DPRINTF(CCache, "Mesi[%d] got data %d from read\n\n", cacheId, currCacheline.cacheBlock[0]);
            assert(memoryFetch);
            // decide on exclusive or shared based on snoop result
            currCacheline.cohState = (bus->sharedWire)? MesiState::Shared : MesiState::Exclusive;
            // reset shared wire
            bus->sharedWire = false;
            currCacheline.clkFlag = 1;
            respPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);

            if(currCacheline.cohState == MesiState::Exclusive){
                DPRINTF(CCache, "STATE_PrRd: Mesi[%d] got DATA from read and Invalid to Exclusive\n\n", cacheId);
            }
            else{
                DPRINTF(CCache, "STATE_PrRd: Mesi[%d] got DATA from read and Invalid to Shared\n\n", cacheId);
            }
            
            printDataHex(&currCacheline.cacheBlock[0], blockSize);

        }
        else{
            // DPRINTF(CCache, "Mesi[%d] storing %d in cache\n\n", cacheId, dataToWrite[0]);
            currCacheline.cohState = MesiState::Modified;
            currCacheline.dirty = true;
            currCacheline.clkFlag = 1;
            if(memoryFetch){
                respPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
            }
            requestPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);

            DPRINTF(CCache, "STATE_PrWr: Mesi[%d] storing DATA in cache, Invalid to Modified\n\n", cacheId);
            printDataHex(&currCacheline.cacheBlock[0], blockSize);

        }

    }

    
    // // Potentially sends a writeback to memory.
    // if(!isHit(pkt->getAddr())){
    //     // evict when miss
    //     evict();

    //     // allocate new
    //     allocate(pkt->getAddr());
    // }

    // bool isRead = pkt->isRead();
    // if (isRead) {
    //     data = *pkt->getPtr<unsigned char>();
    //     DPRINTF(CCache, "Mesi[%d] got data %d from read\n\n", cacheId, data);
    //     // read, now in shared state
    //     int share_entry = pkt->getAddr() - CACHE_START;
    //     if(share[share_entry]){
    //         // already shared with others
    //         state = MesiState::Shared;
    //         DPRINTF(CCache, "Mesi[%d] cache line shared\n\n", cacheId);
    //     }
    //     else{
    //         // owned by myself
    //         state = MesiState::Exclusive;
    //         DPRINTF(CCache, "Mesi[%d] cache line exclusive\n\n", cacheId);
    //     }
        
    // }
    // else {
    //     // do not read data from a write response packet. Use stored value.
    //     DPRINTF(CCache, "Mesi[%d] storing %d in cache\n\n", cacheId, dataToWrite);
    //     data = dataToWrite;

    //     // when write, this cpu know that the other CPU will evict, so I have the only copy
    //     share[pkt->getAddr() - CACHE_START] = 0;

    //     // update dirty bit
    //     dirty = true;
    //     state = MesiState::Modified;
    // }

    if(memoryFetch){
        // if there's a memory fetch, an aligned new packet was allocated
        // when sending to memory, need to make response using the original request to
        // replace the aligned request
        requestPacket->makeResponse();
        delete respPacket;
        respPacket = requestPacket;
        requestPacket = nullptr;
    }

    // the CPU has been waiting for a response. Send it this one.
    sendCpuResp(respPacket);

    // release the bus so other caches can use it
    bus->release(cacheId);

    // start accepting new requests
    blocked = false;
}

void MesiCache::handleCoherentSnoopedReq(PacketPtr pkt) {
    DPRINTF(CCache, "Mesi[%d] snoop: %s\n", cacheId, pkt->print());
    // your implementation here. See MiCache/MsiCache for reference.
    int lineID;
    long addr = pkt->getAddr();
    bool isRemoteRead = pkt->isRead();

    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    bool snoopHit = isHit(addr, lineID);
    MesiState currState;
    cacheLine *cachelinePtr;

    if(!snoopHit){
        currState = MesiState::Invalid;
    }
    else{
        currState = MesiCacheMgr[setID].cacheSet[lineID].cohState;
        cachelinePtr = &MesiCacheMgr[setID].cacheSet[lineID];
        // one or more caches have shared copies
        bus->sharedWire = true;
    }

    switch(currState){

        case MesiState::Modified:

            // flush
            assert(cachelinePtr->dirty);
            // TODO: writeback data
            writeback(addr, &cachelinePtr->cacheBlock[0]);
            cachelinePtr->dirty = false;
            DPRINTF(CCache, "Mesi[%d] snoop hit! Flush modified data\n\n", cacheId);

            if(isRemoteRead){
                DPRINTF(CCache, "STATE_BusRd: Mesi[%d] BusRd hit! set: %d, way: %d, tag: %d, Modified to Shared\n\n", cacheId, setID, lineID, tag);
                cachelinePtr->cohState = MesiState::Shared;
            }
            else{
                DPRINTF(CCache, "STATE_BusRdX: Mesi[%d] BusRdx hit! set: %d, way: %d, tag: %d, Modified to Invalid\n\n", cacheId, setID, lineID, tag);
                cachelinePtr->cohState = MesiState::Invalid;
            }

            break;

        case MesiState::Exclusive:

            if(isRemoteRead){
                DPRINTF(CCache, "STATE_BusRd: Mesi[%d] BusRd hit! set: %d, way: %d, tag: %d, Exclusive to Shared\n\n", cacheId, setID, lineID, tag);
                cachelinePtr->cohState = MesiState::Shared;
            }
            else{
                DPRINTF(CCache, "STATE_BusRdX: Mesi[%d] BusRdx hit! set: %d, way: %d, tag: %d, Exclusive to Invalid\n\n", cacheId, setID, lineID, tag);
                cachelinePtr->cohState = MesiState::Invalid;
            }

            break;

        case MesiState::Shared:

            if(isRemoteRead){
                DPRINTF(CCache, "STATE_BusRd: Mesi[%d] BusRd hit! set: %d, way: %d, tag: %d, Shared keeps\n\n", cacheId, setID, lineID, tag);
                cachelinePtr->cohState = MesiState::Shared;
            }
            else{
                DPRINTF(CCache, "STATE_BusRdX: Mesi[%d] BusRdx hit! set: %d, way: %d, tag: %d, Shared to Invalid\n\n", cacheId, setID, lineID, tag);
                cachelinePtr->cohState = MesiState::Invalid;
            }

            break;

        case MesiState::Invalid:
            DPRINTF(CCache, "Mesi[%d] snoop miss! nothing to do\n\n", cacheId);
            break;

        default:
            break;

    }

    // // cache snooped a request on the shared bus. Update internal state if needed.
    // // only need to care about snoop hit on M
    // if (snoopHit) {
    //     // the other cpu either read or write on this address, it has the shared copy
    //     share[pkt->getAddr() - CACHE_START] = 1;

    //     if(isRemoteRead){
    //         //remote read
    //         if(state == MesiState::Modified){
    //             writeback(); // not evict, actually a write back
    //             state = MesiState::Shared;
    //         }
    //         else if(state == MesiState::Exclusive){
    //             state = MesiState::Shared;
    //         }
    //         DPRINTF(CCache, "Mesi[%d] snoop hit! Modified to shared\n\n", cacheId);

    //     }
    //     else{
    //         // write
    //         // will write back only when modified
    //         writeback();
    //         state = MesiState::Invalid;
    //         DPRINTF(CCache, "Mesi[%d] snoop hit! Invalidate\n\n", cacheId);
    //     }
    // }
    // else {
    //     DPRINTF(CCache, "Mesi[%d] snoop miss! nothing to do\n\n", cacheId);
    // }
}



}