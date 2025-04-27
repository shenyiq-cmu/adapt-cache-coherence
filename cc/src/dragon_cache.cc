#include "src_740/dragon_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"
#include <iostream>
#define NOT_EXIST -1

namespace gem5 {

DragonCache::DragonCache(const DragonCacheParams& params) 
    : CoherentCacheBase(params),
    blockOffset(params.blockOffset),
    setBit(params.setBit),
    cacheSizeBit(params.cacheSizeBit) {
    std::cerr << "Dragon Cache " << cacheId << " created\n";
    DPRINTF(CCache, "Dragon[%d] cache created\n", cacheId);

    // set up cache data structure
    blockSize = 0x1 << blockOffset;
    numSets = 0x1 << setBit;
    cacheSize = 0x1 << cacheSizeBit;
    numLines =  cacheSize / numSets / blockSize;

    DPRINTF(CCache, "blocksize: %d, setsize: %d, cachsize: %d\n\n", blockSize, numLines, cacheSize);
    // init cache manager
    DragonCacheMgr.resize(numSets);
    
    // init cache set manager
    for(auto &setMgr : DragonCacheMgr){
        setMgr.clkPtr = 0;
        setMgr.cacheSet.resize(numLines);
        // init cache lines
        for(auto &cacheline : setMgr.cacheSet){
            cacheline.tag = 0;
            cacheline.clkFlag = 0;
            cacheline.dirty = 0;
            cacheline.valid = false;
            cacheline.cohState = DragonState::INVALID;
            cacheline.cacheBlock.resize(blockSize);
        }
    }

    dataToWrite.resize(blockSize);

    bus->cacheBlockSize = blockSize;
}


void DragonCache::printDataHex(uint8_t* data, int length){
    DPRINTF(CCache, "DATA: ");
    for(int i = 0; i < length; i++){
        printf("%02x", data[i]);
    }
    printf("\n");
}

uint64_t DragonCache::getTag(long addr){
    return ((uint64_t)addr >> ((blockOffset + setBit)));
}

uint64_t DragonCache::getSet(long addr){
    uint64_t mask = (0x1 << (blockOffset + setBit)) - 1;
    return ((uint64_t)addr & mask) >> blockOffset;
}

uint64_t DragonCache::getBlkAddr(long addr){
    return ((addr >> blockOffset) << blockOffset);
}

uint64_t DragonCache::constructAddr(uint64_t tag, uint64_t set, uint64_t blkOffset){
 
    return ((tag << (blockOffset+setBit)) | (set << blockOffset)) | blkOffset;

}

bool DragonCache::isHit(long addr, int &lineID) {
    // hit if tag matches and state is modified or shared
    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    bool exist = false;

    if(DragonCacheMgr[setID].tagMap.find(tag) != DragonCacheMgr[setID].tagMap.end()){
        // a cache line can be at invalid coherence state but still exists in cache
        exist = true;
        lineID = DragonCacheMgr[setID].tagMap[tag];
    }
    else{
        lineID = NOT_EXIST;
    }

    return (exist && (DragonCacheMgr[setID].cacheSet[lineID].cohState != DragonState::INVALID));
}

int DragonCache::allocate(long addr) {
    // assume clk_ptr now points to a empty line
    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    int lineID = NOT_EXIST;
    cacheSetMgr &setMgr = DragonCacheMgr[setID];

    assert(setMgr.cacheSet[setMgr.clkPtr].valid == false);

    cacheLine &cline = setMgr.cacheSet[setMgr.clkPtr];
    cline.dirty = false;
    cline.clkFlag = 1;
    cline.cohState = DragonState::INVALID;
    cline.valid = true;
    cline.tag = tag;
    memset(&cline.cacheBlock[0], 0, blockSize);

    // update clk pointer of the circular array
    setMgr.tagMap[tag] = setMgr.clkPtr;
    lineID = setMgr.clkPtr;

    setMgr.clkPtr = (setMgr.clkPtr + 1) % numLines;

    DPRINTF(CCache, "dragon[%d] allocate set: %d, way: %d for %#x\n\n", cacheId, setID, lineID, addr);

    return lineID;
}

void DragonCache::evict(long addr) {

    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    cacheSetMgr &setMgr = DragonCacheMgr[setID];

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
            DPRINTF(CCache, "dragon[%d] replaces set: %d, way: %d, block tag: %#x, for %#x\n\n", cacheId, setID, setMgr.clkPtr, cline.tag, addr);
            // write back if dirty
            if(cline.dirty){
                assert(cline.cohState == DragonState::MODIFIED || cline.cohState == DragonState::SHARED_MOD);
                uint64_t wbAddr = constructAddr(cline.tag, setID, 0);
                writeback(wbAddr, &cline.cacheBlock[0]);
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

void DragonCache::writeback(long addr, uint8_t* data){ 
    
    bus->sendBlkWriteback(cacheId, getBlkAddr(addr), data, blockSize);
    
    DPRINTF(CCache, "dragon[%d] writeback %#x with DATA\n\n", cacheId, addr);
    printDataHex(data, blockSize);
}

void DragonCache::handleCoherentCpuReq(PacketPtr pkt) {
    // std::cerr << "Dragon[" << cacheId << "] cpu req for addr " << std::hex << pkt->getAddr() << std::dec
    //           << " isRead=" << pkt->isRead() << " isWrite=" << pkt->isWrite() 
    //           << " needsResponse=" << pkt->needsResponse() << "\n";
    DPRINTF(CCache, "dragon[%d] cpu req: %s\n\n", cacheId, pkt->print());
    
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
        cacheLine &currCacheline = DragonCacheMgr[setID].cacheSet[lineID];
        
        assert(currCacheline.cohState != DragonState::INVALID);

        assert(pkt->needsResponse());

        localStats.hitCount++;
        DPRINTF(CCache, "dragon[%d] cache hit #%d\n", cacheId, localStats.hitCount);

        if (isRead) {
            // no state change or snoop needed, reply now
            pkt->makeResponse();
            DPRINTF(CCache, "dragon[%d] read hit %#x, set: %d, line: %d\n\n", cacheId, addr, setID, lineID);
            
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
            // std::cerr << "dragon[" << cacheId << "] write hit in state " << " (" << getStateName(currCacheline.cohState) << ")\n";
            DPRINTF(CCache, "dragon[%d] write hit in state %d\n", cacheId, (int)currCacheline.cohState);
            
            switch (currCacheline.cohState) {
                case DragonState::EXCLUSIVE:
                    // E → M on write (PrWr) - no changes needed
                    // std::cerr << "dragon[" << cacheId << "] E→M transition with PrWr\n";
                    DPRINTF(CCache, "STATE_PrWr: dragon[%d] upgrade from Exclusive to Modified for addr %#x\n", cacheId, addr);
                    currCacheline.cohState = DragonState::MODIFIED;
                    pkt->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
                    currCacheline.dirty = true;
                    currCacheline.clkFlag = 1;
    
                    pkt->makeResponse();
                                    
                    sendCpuResp(pkt);
    
                    blocked = false;

                    break;
                    
                case DragonState::MODIFIED:
                    // Update data

                    // std::cerr << "dragon[" << cacheId << "] M→M transition with PrWr\n";
                    DPRINTF(CCache, "STATE_PrWr: dragon[%d] stay in Modified for addr %#x\n", cacheId, addr);

                    pkt->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
                    assert(currCacheline.dirty == true);
                    currCacheline.clkFlag = 1;
    
                    pkt->makeResponse();
                                    
                    sendCpuResp(pkt);
    
                    blocked = false;

                    break;
                    
                // Other states (SHARED_CLEAN and SHARED_MOD) remain unchanged
                case DragonState::SHARED_CLEAN:
                    // Sc → Sm on write with PrWr(S') transaction
                    // std::cerr << "dragon[" << cacheId << "] Sc→Sm transition with PrWr(S')\n";
                    // DPRINTF(CCache, "dragon[%d] Sc→Sm for addr %#x\n", cacheId, addr);
                    DPRINTF(CCache, "Dragon[%d] Sc write may need update others %#x\n\n", cacheId, addr);
                    requestPacket = pkt;
                    pkt->writeDataToBlock(&dataToWrite[0], blockSize);
                    bus->request(cacheId);
                    break;
                    
                case DragonState::SHARED_MOD:
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

        // stats collection start
        localStats.missCount++;

       // std::cerr << "dragon[" << cacheId << "] " << (isRead ? "read" : "write") 
        //         << " miss for addr " << std::hex << addr << std::dec << "\n";
        DPRINTF(CCache, "dragon[%d] %s miss #%d for addr %#x\n", 
                cacheId, isRead ? "read" : "write", localStats.missCount, addr);
        
        // For both read and write misses, we need to get bus access



        requestPacket = pkt;

        if (isWrite) {
            pkt->writeDataToBlock(&dataToWrite[0], blockSize);
        }

        bus->request(cacheId);
    }
}

void DragonCache::handleCoherentBusGrant() {
    // std::cerr << "dragon[" << cacheId << "] bus granted\n";

    
    assert(requestPacket != nullptr);
    assert(cacheId == bus->currentGranted);

    // stats collect start
    bus->stats.transCount++;

    DPRINTF(CCache, "dragon[%d] bus granted, transaction #%d\n\n", cacheId, bus->stats.transCount);

    
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
        cacheLine &currCacheline = DragonCacheMgr[setID].cacheSet[lineID];
        assert(isWrite && (currCacheline.cohState == DragonState::SHARED_CLEAN || currCacheline.cohState == DragonState::SHARED_MOD));
        // We had a hit but needed the bus (e.g., for write to shared line)

        if (currCacheline.cohState == DragonState::SHARED_CLEAN) {
            // Sc → Sm transition via PrWr(S')
            // std::cerr << "dragon[" << cacheId << "] in Sc broadcast BudUpd on write\n";
            DPRINTF(CCache, "dragon[%d] in Sc broadcast BusUpd on write for addr %#x\n", 
                    cacheId, addr);
            
            // Send a BusUpd to notify other caches
            bus->sendMemReq(requestPacket, false, BusUpd);
            
        }
        else if (currCacheline.cohState == DragonState::SHARED_MOD) {
            // std::cerr << "dragon[" << cacheId << "] in Sm broadcast BudUpd on write\n";
            DPRINTF(CCache, "dragon[%d] in Sm broadcast BusUpd on write for addr %#x\n", 
                    cacheId, addr);
            
            // Send a BusUpd to notify other caches
            bus->sendMemReq(requestPacket, false, BusUpd);
        }

    } else {
        // Cache miss - need to fetch from memory
        if (isRead) {
            // PrRdMiss - read miss, may get data from memory or other cache
            // std::cerr << "dragon[" << cacheId << "] sending BusRd\n";
            DPRINTF(CCache, "dragon[%d] read miss broadcast BusRd for addr %#x\n", 
                    cacheId, addr);
            
            // This will be handled in handleCoherentMemResp
            bus->sendMemReq(requestPacket, true, BusRd);
            // will know shared after snooping
            // requestPacket = nullptr;
        } else if (isWrite) {
            // PrWrMiss + transition to M - read for ownership (RFO)
            // std::cerr << "dragon[" << cacheId << "] sending PrWrMiss for write (RFO)\n";
            DPRINTF(CCache, "dragon[%d] write miss broadcast BusRd for addr %#x\n", 
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

// // Track if we're already handling a memory response to prevent reentrant calls
// thread_local bool handling_memory_response = false;

void DragonCache::handleCoherentMemResp(PacketPtr respPacket) {
    // std::cerr << "dragon[" << cacheId << "] mem resp for addr " << std::hex << pkt->getAddr() << std::dec
    //           << " isResponse=" << pkt->isResponse() << " needsResponse=" << pkt->needsResponse() << "\n";
    DPRINTF(CCache, "dragon[%d] mem resp: %s\n", cacheId, respPacket->print());
    
    // // Check for reentrant call
    // if (handling_memory_response) {
    //     std::cerr << "dragon[" << cacheId << "] WARNING: Reentrant memory response handling detected\n";
    //     return;
    // }
    
    // // Set flag to prevent reentrance
    // handling_memory_response = true;
    
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
        cacheLine &currCacheline = DragonCacheMgr[setID].cacheSet[lineID];
        assert(currCacheline.cohState == DragonState::SHARED_CLEAN || 
            currCacheline.cohState == DragonState::SHARED_MOD);
        assert(!memoryFetch);

        if(currCacheline.cohState == DragonState::SHARED_CLEAN){
            // print trans info
            if(bus->sharedWire)
                DPRINTF(CCache, "STATE_PrWr: dragon[%d] storing DATA at addr %#x, Shared_Clean to Shared_Mod\n", cacheId, addr);
            else
                DPRINTF(CCache, "STATE_PrWr: dragon[%d] storing DATA at addr %#x, Shared_Clean to Modified\n", cacheId, addr);
        }
        else{
            if(bus->sharedWire)
                DPRINTF(CCache, "STATE_PrWr: dragon[%d] storing DATA at addr %#x, stay in Shared_Mod\n", cacheId, addr);
            else
                DPRINTF(CCache, "STATE_PrWr: dragon[%d] storing DATA at addr %#x, Shared_Mod to Modified\n", cacheId, addr);
        }
        
        // BusOperationType opType = bus->getOperationType(pkt);
        
        currCacheline.cohState = bus->sharedWire ? DragonState::SHARED_MOD : DragonState::MODIFIED;
        currCacheline.dirty = true;
        currCacheline.clkFlag = 1;
        // can only modify parts that requested
        requestPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);

        printDataHex(&currCacheline.cacheBlock[0], blockSize);
        
        // Send response to CPU
        sendCpuResp(respPacket);
        
        // Release the bus since we're done with memory operation
        if (cacheId == bus->currentGranted) {
            //std::cerr << "dragon[" << cacheId << "] releasing bus after memory response\n";
            bus->release(cacheId);
        }
        blocked = false;
        return;

    }

    // if no hit, must be invalid
    assert(lineID == NOT_EXIST);

    evict(addr);

    lineID = allocate(addr);
    cacheLine &currCacheline = DragonCacheMgr[setID].cacheSet[lineID];
    assert(currCacheline.cohState == DragonState::INVALID);
    assert(currCacheline.valid);

    if(isRead){
        // read miss
        assert(memoryFetch);
        // decide on exclusive or shared based on snoop result
        currCacheline.cohState = (bus->sharedWire)? DragonState::SHARED_CLEAN : DragonState::EXCLUSIVE;
        // reset shared wire
        bus->sharedWire = false;
        currCacheline.clkFlag = 1;
        respPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
        requestPacket->setDataFromBlock(&currCacheline.cacheBlock[0], blockSize);

        if(currCacheline.cohState == DragonState::EXCLUSIVE){
            DPRINTF(CCache, "STATE_PrRd Miss: Dragon[%d] got DATA from read and Invalid to Exclusive\n\n", cacheId);
        }
        else{
            DPRINTF(CCache, "STATE_PrRd Miss: Dragon[%d] got DATA from read and Invalid to Shared Clean\n\n", cacheId);
        }
        
        printDataHex(&currCacheline.cacheBlock[0], blockSize);

    }
    else{
        // DPRINTF(CCache, "dragon[%d] storing %d in cache\n\n", cacheId, dataToWrite[0]);
        currCacheline.cohState = (bus->sharedWire)? DragonState::SHARED_MOD : DragonState::MODIFIED;
        currCacheline.dirty = true;
        currCacheline.clkFlag = 1;
        bus->sharedWire = false;

        if(memoryFetch){
            respPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
        }
        requestPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);

        if(currCacheline.cohState == DragonState::MODIFIED){
            DPRINTF(CCache, "STATE_PrWr Miss: Dragon[%d] write DATA and Invalid to Modified\n\n", cacheId);
        }
        else{
            DPRINTF(CCache, "STATE_PrWr Miss: Dragon[%d] write DATA and Invalid to Shared_Mod\n\n", cacheId);
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
    
    // // Reset the handling flag
    // handling_memory_response = false;
}

void DragonCache::handleCoherentSnoopedReq(PacketPtr pkt) {
    int lineID;
    long addr = pkt->getAddr();
    // bool isRemoteRead = pkt->isRead();

    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    bool snoopHit = isHit(addr, lineID);
    BusOperationType opType = bus->getOperationType(pkt);
    DragonState currState;
    cacheLine *cachelinePtr;
    
    DPRINTF(CCache, "dragon[%d] received snoop for addr %#x opType=%d\n", 
            cacheId, addr, opType);
    
    // If we don't have the line, or it's not the same address, do nothing
    if (!snoopHit) {
        currState = DragonState::INVALID;
    }
    else{
        currState = DragonCacheMgr[setID].cacheSet[lineID].cohState;
        cachelinePtr = &DragonCacheMgr[setID].cacheSet[lineID];
        // one or more caches have shared copies
        bus->sharedWire = true;
    }
    
    switch(currState){

        case DragonState::MODIFIED:

            // flush
            assert(cachelinePtr->dirty);
            assert(bus->hasBusRd(opType));
            // TODO: writeback data
            writeback(addr, &cachelinePtr->cacheBlock[0]);
            cachelinePtr->dirty = false;
            
            DPRINTF(CCache, "dragon[%d] snoop hit! Flush modified data\n\n", cacheId);

            cachelinePtr->cohState = DragonState::SHARED_MOD;
            DPRINTF(CCache, "STATE_BusRd: dragon[%d] BusRd hit! set: %d, way: %d, tag: %d, Modified to Shared_Mod\n\n", cacheId, setID, lineID, tag);

            if(!bus->hasBusUpd(opType)){
                break;
            }

            // intentional fall through when update after rd

        case DragonState::SHARED_MOD:

            // may or may not be synced with memory
            // can be busrd, bsupd or together

            if(bus->hasBusRd(opType) && cachelinePtr->dirty){
                writeback(addr, &cachelinePtr->cacheBlock[0]);
                cachelinePtr->dirty = false;
                DPRINTF(CCache, "dragon[%d] snoop hit! Flush shared modified data\n\n", cacheId);
            }

            if(bus->hasBusUpd(opType)){
                assert(pkt->isWrite());
                pkt->writeDataToBlock(&cachelinePtr->cacheBlock[0], blockSize);
                cachelinePtr->cohState = DragonState::SHARED_CLEAN;
                cachelinePtr->dirty = false;
                DPRINTF(CCache, "STATE_BusUpd: dragon[%d] BusUpd hit! set: %d, way: %d, tag: %d, Shared_Mod to Shared_Clean\n\n", cacheId, setID, lineID, tag);
            }

            break;

        case DragonState::EXCLUSIVE:

            assert(!cachelinePtr->dirty);
            assert(bus->hasBusRd(opType));

            cachelinePtr->cohState = DragonState::SHARED_CLEAN;
            DPRINTF(CCache, "STATE_BusRd: dragon[%d] BusRd hit! set: %d, way: %d, tag: %d, Exclusive to Shared_Clean\n\n", cacheId, setID, lineID, tag);

            if(!bus->hasBusUpd(opType)){
                break;
            }

            // intentional fall through when update after rd

        case DragonState::SHARED_CLEAN:

            if(bus->hasBusUpd(opType)){
                assert(pkt->isWrite());
                pkt->writeDataToBlock(&cachelinePtr->cacheBlock[0], blockSize);
                DPRINTF(CCache, "STATE_BusUpd: dragon[%d] BusUpd hit! set: %d, way: %d, tag: %d, stay in Shared_Clean\n\n", cacheId, setID, lineID, tag);
            }

            break;


        case DragonState::INVALID:
            DPRINTF(CCache, "dragon[%d] snoop miss! nothing to do\n\n", cacheId);
            break;

        default:
            break;

    }

    // // erase busop map
    // bus->rmBusTrans(pkt);

}

} // namespace gem5
