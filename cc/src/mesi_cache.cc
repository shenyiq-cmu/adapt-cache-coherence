#include "src_740/mesi_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"
#include <iostream>
#define NOT_EXIST -1

namespace gem5 {

MesiCache::MesiCache(const MesiCacheParams& params) 
    : CoherentCacheBase(params) {
    std::cerr << "Dragon Cache " << cacheId << " created\n";
    DPRINTF(CCache, "Dragon[%d] cache created\n", cacheId);

    // set up cache data structure
    blockSize = 0x1 << blockOffset;
    numSets = 0x1 << setBit;
    cacheSize = 0x1 << cacheSizeBit;
    numLines =  cacheSize / numSets / blockSize;

    DPRINTF(CCache, "blocksize: %d, setsize: %d, cachsize: %d\n\n", blockSize, numLines, cacheSize);
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
            cacheline.cohState = MesiState::INVALID;
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

    return (exist && (MesiCacheMgr[setID].cacheSet[lineID].cohState != MesiState::INVALID));
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
    cline.cohState = MesiState::INVALID;
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
                assert(cline.cohState == MesiState::MODIFIED || cline.cohState == MesiState::SHARED_MOD);
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

}

void MesiCache::writeback(long addr, uint8_t* data){ 
    
    bus->sendBlkWriteback(cacheId, getBlkAddr(addr), data, blockSize);
    
    DPRINTF(CCache, "Mesi[%d] writeback %#x with DATA\n\n", cacheId, addr);
    printDataHex(data, blockSize);
}

void MesiCache::handleCoherentCpuReq(PacketPtr pkt) {
    // std::cerr << "Dragon[" << cacheId << "] cpu req for addr " << std::hex << pkt->getAddr() << std::dec
    //           << " isRead=" << pkt->isRead() << " isWrite=" << pkt->isWrite() 
    //           << " needsResponse=" << pkt->needsResponse() << "\n";
    DPRINTF(CCache, "Mesi[%d] cpu req: %s\n\n", cacheId, pkt->print());
    
    blocked = true; // stop accepting new reqs from CPU until this one is done
    int lineID;
    long addr = pkt->getAddr();
    bool isRead = pkt->isRead();
    bool isWrite = pkt->isWrite();

    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    bool cacheHit = isHit(addr, lineID);
    
    
    if (cacheHit) {
        // Cache hit
        cacheLine &currCacheline = MesiCacheMgr[setID].cacheSet[lineID];
        
        assert(currCacheline.cohState != MesiState::INVALID);

        assert(pkt->needsResponse());

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

        } else if (isWrite) {
            // Write hit
            std::cerr << "MESI[" << cacheId << "] write hit in state " << " (" << getStateName(currCacheline.cohState) << ")\n";
            DPRINTF(CCache, "Mesi[%d] write hit in state %d\n", cacheId, (int)currCacheline.cohState);
            
            switch (currCacheline.cohState) {
                case MesiState::EXCLUSIVE:
                    // E → M on write (PrWr) - no changes needed
                    std::cerr << "MESI[" << cacheId << "] E→M transition with PrWr\n";
                    DPRINTF(CCache, "Mesi[%d] E→M transition for addr %#x\n", cacheId, addr);
                    currCacheline.cohState = MesiState::MODIFIED;
                    pkt->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
                    currCacheline.dirty = true;
                    currCacheline.clkFlag = 1;
    
                    pkt->makeResponse();
                                    
                    sendCpuResp(pkt);
    
                    blocked = false;

                    break;
                    
                case MesiState::MODIFIED:
                    // Update data

                    std::cerr << "MESI[" << cacheId << "] M→M transition with PrWr\n";
                    DPRINTF(CCache, "Mesi[%d] M→M transition for addr %#x\n", cacheId, addr);

                    pkt->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
                    assert(currCacheline.dirty == true);
                    currCacheline.clkFlag = 1;
    
                    pkt->makeResponse();
                                    
                    sendCpuResp(pkt);
    
                    blocked = false;

                    break;
                    
                // Other states (SHARED_CLEAN and SHARED_MOD) remain unchanged
                case MesiState::SHARED_CLEAN:
                    // Sc → Sm on write with PrWr(S') transaction
                    // std::cerr << "MESI[" << cacheId << "] Sc→Sm transition with PrWr(S')\n";
                    // DPRINTF(CCache, "Mesi[%d] Sc→Sm for addr %#x\n", cacheId, addr);
                    DPRINTF(CCache, "Dragon[%d] Sc write may need update others %#x\n\n", cacheId, addr);
                    requestPacket = pkt;
                    pkt->writeDataToBlock(&dataToWrite[0], blockSize);
                    bus->request(cacheId);
                    break;
                    
                case MesiState::SHARED_MOD:
                    // Sm → Sm on write with PrWr(S) transaction
                    DPRINTF(CCache, "Dragon[%d] Sm write may need update others %#x\n\n", cacheId, addr);
                    requestPacket = pkt;
                    pkt->writeDataToBlock(&dataToWrite[0], blockSize);
                    bus->request(cacheId);
                    break;
                    
                default:
                    panic("Invalid state");
            }
        }
    } else {
        // Cache miss handling 
        std::cerr << "MESI[" << cacheId << "] " << (isRead ? "read" : "write") 
                 << " miss for addr " << std::hex << addr << std::dec << "\n";
        DPRINTF(CCache, "Mesi[%d] %s miss for addr %#x\n", 
                cacheId, isRead ? "read" : "write", addr);
        
        // For both read and write misses, we need to get bus access

        requestPacket = pkt;

        if (isWrite) {
            pkt->writeDataToBlock(&dataToWrite[0], blockSize);
        }

        bus->request(cacheId);
    }
}

void MesiCache::handleCoherentBusGrant() {
    std::cerr << "MESI[" << cacheId << "] bus granted\n";
    DPRINTF(CCache, "Mesi[%d] bus granted\n\n", cacheId);
    
    assert(requestPacket != nullptr);
    assert(cacheId == bus->currentGranted);
    
    uint64_t addr = requestPacket->getAddr();
    uint64_t blk_addr = requestPacket->getBlockAddr(blockSize);
    uint64_t size = requestPacket->getSize();

    int lineID;

    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    bool cacheHit = isHit(addr, lineID);

    bool isRead = requestPacket->isRead() && !requestPacket->isWrite();
    bool isWrite = requestPacket->isWrite();
    
    bus->sharedWire = false;

    if (cacheHit) {
        cacheLine &currCacheline = MesiCacheMgr[setID].cacheSet[lineID];
        assert(isWrite && (currCacheline.cohState == MesiState::SHARED_CLEAN || currCacheline.cohState == MesiState::SHARED_MOD));
        // We had a hit but needed the bus (e.g., for write to shared line)

        if (currCacheline.cohState == MesiState::SHARED_CLEAN) {
            // Sc → Sm transition via PrWr(S')
            std::cerr << "MESI[" << cacheId << "] in Sc broadcast BudUpd on write\n";
            DPRINTF(CCache, "Mesi[%d] in Sc broadcast BusUpd on write for addr %#x\n", 
                    cacheId, addr);
            
            // Send a BusUpd to notify other caches
            bus->sendMemReq(requestPacket, false, BusUpd);
            
        }
        else if (currCacheline.cohState == MesiState::SHARED_MOD) {
            std::cerr << "MESI[" << cacheId << "] in Sm broadcast BudUpd on write\n";
            DPRINTF(CCache, "Mesi[%d] in Sm broadcast BusUpd on write for addr %#x\n", 
                    cacheId, addr);
            
            // Send a BusUpd to notify other caches
            bus->sendMemReq(requestPacket, false, BusUpd);
        }

    } else {
        // Cache miss - need to fetch from memory
        if (isRead) {
            // PrRdMiss - read miss, may get data from memory or other cache
            std::cerr << "MESI[" << cacheId << "] sending BusRd\n";
            DPRINTF(CCache, "Mesi[%d] sending BusRd for addr %#x\n", 
                    cacheId, addr);
            
            // This will be handled in handleCoherentMemResp
            bus->sendMemReq(requestPacket, true, BusRd);
            // will know shared after snooping
            // requestPacket = nullptr;
        } else if (isWrite) {
            // PrWrMiss + transition to M - read for ownership (RFO)
            std::cerr << "MESI[" << cacheId << "] sending PrWrMiss for write (RFO)\n";
            DPRINTF(CCache, "Mesi[%d] sending PrWrMiss for write (RFO) addr %#x\n", 
                    cacheId, addr);
            
            // This will be handled in handleCoherentMemResp
            if(addr == blk_addr && size == blockSize){
                // overwrite whole block
                bus->sendMemReq(requestPacket, false, BusRdUpd);
            }
            else{
                bus->sendMemReq(requestPacket, true, BusRdUpd);
            }
            
            // requestPacket = nullptr;
        }
    }
}

// Track if we're already handling a memory response to prevent reentrant calls
thread_local bool handling_memory_response = false;

void MesiCache::handleCoherentMemResp(PacketPtr respPacket) {
    // std::cerr << "MESI[" << cacheId << "] mem resp for addr " << std::hex << pkt->getAddr() << std::dec
    //           << " isResponse=" << pkt->isResponse() << " needsResponse=" << pkt->needsResponse() << "\n";
    DPRINTF(CCache, "Mesi[%d] mem resp: %s\n", cacheId, respPacket->print());
    
    // // Check for reentrant call
    // if (handling_memory_response) {
    //     std::cerr << "MESI[" << cacheId << "] WARNING: Reentrant memory response handling detected\n";
    //     return;
    // }
    
    // Set flag to prevent reentrance
    handling_memory_response = true;
    
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
    
    // Handle the response from memory
    if (cacheHit) {
        assert(lineID != NOT_EXIST);
        cacheLine &currCacheline = MesiCacheMgr[setID].cacheSet[lineID];
        assert(currCacheline.cohState == MesiState::SHARED_CLEAN || 
            currCacheline.cohState == MesiState::SHARED_MOD);
        assert(!memoryFetch);

        if(currCacheline.cohState == MesiState::SHARED_CLEAN){
            // print trans info
            if(bus->sharedWire)
                DPRINTF(CCache, "Mesi[%d] Sc→Sm transition for addr %#x\n", cacheId, addr);
            else
                DPRINTF(CCache, "Mesi[%d] Sc→M transition for addr %#x\n", cacheId, addr);
        }
        else{
            if(bus->sharedWire)
                DPRINTF(CCache, "Mesi[%d] Sm→Sm transition for addr %#x\n", cacheId, addr);
            else
                DPRINTF(CCache, "Mesi[%d] Sm→M transition for addr %#x\n", cacheId, addr);
        }
        
        // BusOperationType opType = bus->getOperationType(pkt);
        
        currCacheline.cohState = bus->sharedWire ? MesiState::SHARED_MOD : MesiState::MODIFIED;
        currCacheline.dirty = true;
        currCacheline.clkFlag = 1;
        // can only modify parts that requested
        requestPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
        
        // Send response to CPU
        sendCpuResp(respPacket);
        
        // Release the bus since we're done with memory operation
        if (cacheId == bus->currentGranted) {
            std::cerr << "MESI[" << cacheId << "] releasing bus after memory response\n";
            bus->release(cacheId);
        }
        blocked = false;
        return;

    }

    // if no hit, must be invalid
    assert(lineID == NOT_EXIST);

    evict(addr);

    lineID = allocate(addr);
    cacheLine &currCacheline = MesiCacheMgr[setID].cacheSet[lineID];
    assert(currCacheline.cohState == MesiState::INVALID);
    assert(currCacheline.valid);

    if(isRead){
        // read miss
        assert(memoryFetch);
        // decide on exclusive or shared based on snoop result
        currCacheline.cohState = (bus->sharedWire)? MesiState::SHARED_CLEAN : MesiState::EXCLUSIVE;
        // reset shared wire
        bus->sharedWire = false;
        currCacheline.clkFlag = 1;
        respPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);

        if(currCacheline.cohState == MesiState::EXCLUSIVE){
            DPRINTF(CCache, "STATE_PrRd Miss: Dragon[%d] got DATA from read and Invalid to Exclusive\n\n", cacheId);
        }
        else{
            DPRINTF(CCache, "STATE_PrRd Miss: Dragon[%d] got DATA from read and Invalid to Shared Clean\n\n", cacheId);
        }
        
        printDataHex(&currCacheline.cacheBlock[0], blockSize);

    }
    else{
        // DPRINTF(CCache, "Mesi[%d] storing %d in cache\n\n", cacheId, dataToWrite[0]);
        currCacheline.cohState = (bus->sharedWire)? MesiState::SHARED_MOD : MesiState::MODIFIED;
        currCacheline.dirty = true;
        currCacheline.clkFlag = 1;
        bus->sharedWire = false;

        if(memoryFetch){
            respPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
        }
        requestPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);

        if(currCacheline.cohState == MesiState::MODIFIED){
            DPRINTF(CCache, "STATE_PrWr Miss: Dragon[%d] write DATA and Invalid to Modified\n\n", cacheId);
        }
        else{
            DPRINTF(CCache, "STATE_PrWr Miss: Dragon[%d] write DATA and Invalid to Shared Mod\n\n", cacheId);
        }
        printDataHex(&currCacheline.cacheBlock[0], blockSize);

    }

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
    
    // Reset the handling flag
    handling_memory_response = false;
}

void MesiCache::handleCoherentSnoopedReq(PacketPtr pkt) {
    int lineID;
    long addr = pkt->getAddr();
    // bool isRemoteRead = pkt->isRead();

    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    bool snoopHit = isHit(addr, lineID);
    BusOperationType opType = bus->getOperationType(pkt);
    MesiState currState;
    cacheLine *cachelinePtr;
    
    DPRINTF(CCache, "Mesi[%d] received snoop for addr %#x opType=%d\n", 
            cacheId, addr, opType);
    
    // If we don't have the line, or it's not the same address, do nothing
    if (!snoopHit) {
        currState = MesiState::INVALID;
    }
    else{
        currState = MesiCacheMgr[setID].cacheSet[lineID].cohState;
        cachelinePtr = &MesiCacheMgr[setID].cacheSet[lineID];
        // one or more caches have shared copies
        bus->sharedWire = true;
    }
    
    switch(currState){

        case MesiState::MODIFIED:

            // flush
            assert(cachelinePtr->dirty);
            assert(bus->hasBusRd(opType));
            // TODO: writeback data
            writeback(addr, &cachelinePtr->cacheBlock[0]);
            cachelinePtr->dirty = false;
            
            DPRINTF(CCache, "Mesi[%d] snoop hit! Flush modified data\n\n", cacheId);

            cachelinePtr->cohState = MesiState::SHARED_MOD;
            DPRINTF(CCache, "STATE_BusRd: Mesi[%d] BusRd hit! set: %d, way: %d, tag: %d, Modified to Sm\n\n", cacheId, setID, lineID, tag);

            if(!bus->hasBusUpd(opType)){
                break;
            }

            // intentional fall through when update after rd

        case MesiState::SHARED_MOD:

            // may or may not be synced with memory
            // can be busrd, bsupd or together

            if(bus->hasBusRd(opType) && cachelinePtr->dirty){
                writeback(addr, &cachelinePtr->cacheBlock[0]);
                cachelinePtr->dirty = false;
                DPRINTF(CCache, "Mesi[%d] snoop hit! Flush shared modified data\n\n", cacheId);
            }

            if(bus->hasBusUpd(opType)){
                assert(pkt->isWrite());
                pkt->writeDataToBlock(&cachelinePtr->cacheBlock[0], blockSize);
                cachelinePtr->cohState = MesiState::SHARED_CLEAN;
                DPRINTF(CCache, "STATE_BusUpd: Mesi[%d] BusUpd hit! set: %d, way: %d, tag: %d, Sm to Sc\n\n", cacheId, setID, lineID, tag);
            }

            break;

        case MesiState::EXCLUSIVE:

            assert(!cachelinePtr->dirty);
            assert(bus->hasBusRd(opType));

            cachelinePtr->cohState = MesiState::SHARED_CLEAN;
            DPRINTF(CCache, "STATE_BusRd: Mesi[%d] BusRd hit! set: %d, way: %d, tag: %d, Exclusive to Sc\n\n", cacheId, setID, lineID, tag);

            if(!bus->hasBusUpd(opType)){
                break;
            }

            // intentional fall through when update after rd

        case MesiState::SHARED_CLEAN:

            if(bus->hasBusUpd(opType)){
                assert(pkt->isWrite());
                pkt->writeDataToBlock(&cachelinePtr->cacheBlock[0], blockSize);
                DPRINTF(CCache, "STATE_BusUpd: Mesi[%d] BusUpd hit! set: %d, way: %d, tag: %d, Sc to Sc\n\n", cacheId, setID, lineID, tag);
            }

            break;


        case MesiState::INVALID:
            DPRINTF(CCache, "Mesi[%d] snoop miss! nothing to do\n\n", cacheId);
            break;

        default:
            break;

    }

    // erase busop map
    bus->rmBusTrans(pkt);

}

} // namespace gem5
