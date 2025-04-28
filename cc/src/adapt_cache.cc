#include "src_740/adapt_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"
#include <iostream>
#define NOT_EXIST -1
#define SHARED_MEM_START 0x8000
#define SHARED_MEM_END 0xa000

namespace gem5 {

AdaptCache::AdaptCache(const AdaptCacheParams& params) 
    : CoherentCacheBase(params),
    blockOffset(params.blockOffset),
    setBit(params.setBit),
    cacheSizeBit(params.cacheSizeBit),
    invalidThreshold(params.invalidThreshold),
    invalidationRatio(params.invalidationRatio) {
    std::cerr << "Adapt Cache " << cacheId << " created\n";
    DPRINTF(CCache, "Adapt[%d] cache created\n", cacheId);

    // set up cache data structure
    blockSize = 0x1 << blockOffset;
    numSets = 0x1 << setBit;
    cacheSize = 0x1 << cacheSizeBit;
    numLines =  cacheSize / numSets / blockSize;

    DPRINTF(CCache, "blocksize: %d, setsize: %d, cachsize: %d\n\n", blockSize, numLines, cacheSize);
    // init cache manager
    AdaptCacheMgr.resize(numSets);
    
    // init cache set manager
    for(auto &setMgr : AdaptCacheMgr){
        setMgr.clkPtr = 0;
        setMgr.cacheSet.resize(numLines);
        // init cache lines
        for(auto &cacheline : setMgr.cacheSet){
            cacheline.tag = 0;
            cacheline.clkFlag = 0;
            cacheline.dirty = 0;
            cacheline.valid = false;
            cacheline.cohState = AdaptState::INVALID;
            cacheline.cacheBlock.resize(blockSize);
            cacheline.invalidCounter = invalidThreshold;
            cacheline.writeRunCounter = 0;
        }
    }

    dataToWrite.resize(blockSize);

    bus->cacheBlockSize = blockSize;

    if(bus->invalidationThs.empty()){
        bus->invalidationThs.resize((SHARED_MEM_END - SHARED_MEM_START)/blockSize);
        for(auto& T : bus->invalidationThs){
            T = invalidThreshold;
        }
    }
}


void AdaptCache::printDataHex(uint8_t* data, int length){
    DPRINTF(CCache, "DATA: ");
    for(int i = 0; i < length; i++){
        printf("%02x", data[i]);
    }
    printf("\n");
}

uint64_t AdaptCache::getTag(long addr){
    return ((uint64_t)addr >> ((blockOffset + setBit)));
}

uint64_t AdaptCache::getSet(long addr){
    uint64_t mask = (0x1 << (blockOffset + setBit)) - 1;
    return ((uint64_t)addr & mask) >> blockOffset;
}

uint64_t AdaptCache::getBlkAddr(long addr){
    return ((addr >> blockOffset) << blockOffset);
}

uint64_t AdaptCache::constructAddr(uint64_t tag, uint64_t set, uint64_t blkOffset){
 
    return ((tag << (blockOffset+setBit)) | (set << blockOffset)) | blkOffset;

}

uint64_t AdaptCache::getBlkNumber(long addr){
    return (addr >> blockOffset) - (SHARED_MEM_START >> blockOffset);
}

void AdaptCache::endWriteRun(long addr, int& currWriteRun){
    if(currWriteRun < invalidationRatio){
        bus->invalidationThs[getBlkNumber(addr)]++;
        DPRINTF(CCache, "adapt[%d] Write Run ends at %d for %#x, inv threshold increase\n\n", cacheId, currWriteRun, addr);
    }
    else{
        bus->invalidationThs[getBlkNumber(addr)]--;
        DPRINTF(CCache, "adapt[%d] Write Run ends at %d for %#x, inv threshold increase\n\n", cacheId, currWriteRun, addr);
    }
    currWriteRun = 0;
}

bool AdaptCache::isHit(long addr, int &lineID) {
    // hit if tag matches and state is modified or shared
    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    bool exist = false;

    if(AdaptCacheMgr[setID].tagMap.find(tag) != AdaptCacheMgr[setID].tagMap.end()){
        // a cache line can be at invalid coherence state but still exists in cache
        exist = true;
        lineID = AdaptCacheMgr[setID].tagMap[tag];
    }
    else{
        lineID = NOT_EXIST;
    }

    return (exist && (AdaptCacheMgr[setID].cacheSet[lineID].cohState != AdaptState::INVALID));
}

int AdaptCache::allocate(long addr) {
    // assume clk_ptr now points to a empty line
    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    int lineID = NOT_EXIST;
    cacheSetMgr &setMgr = AdaptCacheMgr[setID];

    assert(setMgr.cacheSet[setMgr.clkPtr].valid == false);

    cacheLine &cline = setMgr.cacheSet[setMgr.clkPtr];
    cline.dirty = false;
    cline.clkFlag = 1;
    cline.cohState = AdaptState::INVALID;
    cline.valid = true;
    cline.tag = tag;
    cline.invalidCounter = bus->invalidationThs[getBlkNumber(addr)];
    cline.writeRunCounter = 0;
    memset(&cline.cacheBlock[0], 0, blockSize);

    // update clk pointer of the circular array
    setMgr.tagMap[tag] = setMgr.clkPtr;
    lineID = setMgr.clkPtr;

    setMgr.clkPtr = (setMgr.clkPtr + 1) % numLines;

    DPRINTF(CCache, "adapt[%d] allocate set: %d, way: %d for %#x\n\n", cacheId, setID, lineID, addr);

    return lineID;
}

void AdaptCache::evict(long addr) {

    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    cacheSetMgr &setMgr = AdaptCacheMgr[setID];

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
            DPRINTF(CCache, "adapt[%d] replaces set: %d, way: %d, block tag: %#x, for %#x\n\n", cacheId, setID, setMgr.clkPtr, cline.tag, addr);
            // write back if dirty
            if(cline.dirty){
                assert(cline.cohState == AdaptState::MODIFIED || cline.cohState == AdaptState::SHARED_MOD);
                uint64_t wbAddr = constructAddr(cline.tag, setID, 0);
                writeback(wbAddr, &cline.cacheBlock[0]);
            }

            // need to erase from map
            setMgr.tagMap.erase(cline.tag);

            // record write run and update Ths
            endWriteRun(addr, cline.writeRunCounter);
            // allocate will reset writeRunCounter


            cline.valid = false;
            // other fields reset by allocate

            break;
        }

        setMgr.clkPtr = (setMgr.clkPtr+1)%numLines;
    }

}

void AdaptCache::writeback(long addr, uint8_t* data){ 
    
    bus->sendBlkWriteback(cacheId, getBlkAddr(addr), data, blockSize);
    
    DPRINTF(CCache, "adapt[%d] writeback %#x with DATA\n\n", cacheId, addr);
    printDataHex(data, blockSize);
}

void AdaptCache::handleCoherentCpuReq(PacketPtr pkt) {
    // std::cerr << "Adapt[" << cacheId << "] cpu req for addr " << std::hex << pkt->getAddr() << std::dec
    //           << " isRead=" << pkt->isRead() << " isWrite=" << pkt->isWrite() 
    //           << " needsResponse=" << pkt->needsResponse() << "\n";
    DPRINTF(CCache, "adapt[%d] cpu req: %s\n\n", cacheId, pkt->print());
    
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
        cacheLine &currCacheline = AdaptCacheMgr[setID].cacheSet[lineID];
        
        assert(currCacheline.cohState != AdaptState::INVALID);

        assert(pkt->needsResponse());

        localStats.hitCount++;
        DPRINTF(CCache, "adapt[%d] cache hit #%d\n", cacheId, localStats.hitCount);

        if (isRead) {
            // no state change or snoop needed, reply now
            pkt->makeResponse();
            DPRINTF(CCache, "adapt[%d] read hit %#x, set: %d, line: %d\n\n", cacheId, addr, setID, lineID);
            
            // if hit, the other cpu can be only in S or I, no need snoop
            pkt->setDataFromBlock(&currCacheline.cacheBlock[0], blockSize);

            // cache line update
            currCacheline.clkFlag = 1;

            currCacheline.accessSinceUpd = true;

            // return the response packet to CPU
            sendCpuResp(pkt);

            // start accepting new requests
            blocked = false;

        } else if (isWrite) {
            // Write hit
            // std::cerr << "adapt[" << cacheId << "] write hit in state " << " (" << getStateName(currCacheline.cohState) << ")\n";
            DPRINTF(CCache, "adapt[%d] write hit in state %d\n", cacheId, (int)currCacheline.cohState);
            
            switch (currCacheline.cohState) {
                case AdaptState::EXCLUSIVE:
                    // E → M on write (PrWr) - no changes needed
                    // std::cerr << "adapt[" << cacheId << "] E→M transition with PrWr\n";
                    DPRINTF(CCache, "STATE_PrWr: adapt[%d] upgrade from Exclusive to Modified for addr %#x\n", cacheId, addr);
                    currCacheline.cohState = AdaptState::MODIFIED;
                    pkt->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
                    currCacheline.dirty = true;
                    currCacheline.clkFlag = 1;
                    // update write run when write exclusively
                    currCacheline.writeRunCounter++;
    
                    pkt->makeResponse();
                                    
                    sendCpuResp(pkt);
    
                    blocked = false;

                    break;
                    
                case AdaptState::MODIFIED:
                    // Update data

                    // std::cerr << "adapt[" << cacheId << "] M→M transition with PrWr\n";
                    DPRINTF(CCache, "STATE_PrWr: adapt[%d] stay in Modified for addr %#x\n", cacheId, addr);

                    pkt->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
                    assert(currCacheline.dirty == true);
                    currCacheline.clkFlag = 1;
                    // update write run when write exclusively
                    currCacheline.writeRunCounter++;
    
                    pkt->makeResponse();
                                    
                    sendCpuResp(pkt);
    
                    blocked = false;

                    break;
                    
                // Other states (SHARED_CLEAN and SHARED_MOD) remain unchanged
                case AdaptState::SHARED_CLEAN:
                    // Sc → Sm on write with PrWr(S') transaction
                    // std::cerr << "adapt[" << cacheId << "] Sc→Sm transition with PrWr(S')\n";
                    // DPRINTF(CCache, "adapt[%d] Sc→Sm for addr %#x\n", cacheId, addr);
                    DPRINTF(CCache, "Adapt[%d] Sc write may need update others %#x\n\n", cacheId, addr);
                    requestPacket = pkt;
                    pkt->writeDataToBlock(&dataToWrite[0], blockSize);
                    bus->request(cacheId);
                    break;
                    
                case AdaptState::SHARED_MOD:
                    // Sm → Sm on write with PrWr(S) transaction
                    DPRINTF(CCache, "Adapt[%d] Sm write may need update others %#x\n\n", cacheId, addr);
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

       // std::cerr << "adapt[" << cacheId << "] " << (isRead ? "read" : "write") 
        //         << " miss for addr " << std::hex << addr << std::dec << "\n";
        DPRINTF(CCache, "adapt[%d] %s miss #%d for addr %#x\n", 
                cacheId, isRead ? "read" : "write", localStats.missCount, addr);
        
        // For both read and write misses, we need to get bus access



        requestPacket = pkt;

        if (isWrite) {
            pkt->writeDataToBlock(&dataToWrite[0], blockSize);
        }

        bus->request(cacheId);
    }
}

void AdaptCache::handleCoherentBusGrant() {
    // std::cerr << "adapt[" << cacheId << "] bus granted\n";

    
    assert(requestPacket != nullptr);
    assert(cacheId == bus->currentGranted);

    // stats collect start
    // bus->stats.transCount++;

    // DPRINTF(CCache, "adapt[%d] bus granted, transaction #%d\n\n", cacheId, bus->stats.transCount);

    DPRINTF(CCache, "adapt[%d] bus granted\n\n", cacheId);

    uint64_t addr = requestPacket->getAddr();
    uint64_t blk_addr = requestPacket->getBlockAddr(blockSize);
    uint64_t size = requestPacket->getSize();

    int lineID;

    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    bool cacheHit = isHit(addr, lineID);

    bool isRead = requestPacket->isRead() && !requestPacket->isWrite();
    bool isWrite = requestPacket->isWrite();

    BusOperationType busOp;
    
    bus->sharedWire = false;
    bus->remoteAccessWire = false;

    if (cacheHit) {
        cacheLine &currCacheline = AdaptCacheMgr[setID].cacheSet[lineID];
        assert(isWrite && (currCacheline.cohState == AdaptState::SHARED_CLEAN || currCacheline.cohState == AdaptState::SHARED_MOD));
        // We had a hit but needed the bus (e.g., for write to shared line)

        busOp = (currCacheline.invalidCounter>0)? BusUpd : BusRdX;

        if (currCacheline.cohState == AdaptState::SHARED_CLEAN) {
            // Sc → Sm transition via PrWr(S')
            // std::cerr << "adapt[" << cacheId << "] in Sc broadcast BudUpd on write\n";
            DPRINTF(CCache, "adapt[%d] in Sc broadcast %s on write for addr %#x\n", cacheId, 
                (currCacheline.invalidCounter>0)? "BusUpd" : "BusRdX", addr);
            
            // Send a BusUpd to notify other caches if counter not 0, otherwise invalidate others
            bus->sendMemReq(requestPacket, false, (currCacheline.invalidCounter>0)? BusUpd : BusRdX);
            
        }
        else if (currCacheline.cohState == AdaptState::SHARED_MOD) {
            // std::cerr << "adapt[" << cacheId << "] in Sm broadcast BudUpd on write\n";
            DPRINTF(CCache, "adapt[%d] in Sm broadcast %s on write for addr %#x\n", cacheId, 
                (currCacheline.invalidCounter>0)? "BusUpd" : "BusRdX", addr);
            
            // Send a BusUpd to notify other caches if counter not 0, otherwise invalidate others
            bus->sendMemReq(requestPacket, false, (currCacheline.invalidCounter>0)? BusUpd : BusRdX);
        }

    } else {
        // Cache miss - need to fetch from memory
        if (isRead) {
            // PrRdMiss - read miss, may get data from memory or other cache
            // std::cerr << "adapt[" << cacheId << "] sending BusRd\n";
            DPRINTF(CCache, "adapt[%d] read miss broadcast BusRd for addr %#x\n", 
                    cacheId, addr);
            
            busOp = BusRd;

            // This will be handled in handleCoherentMemResp
            bus->sendMemReq(requestPacket, true, BusRd);
            // will know shared after snooping
            // requestPacket = nullptr;
        } else if (isWrite) {
            // fixed invalid threshold for now
            DPRINTF(CCache, "adapt[%d] write miss broadcast %s for addr %#x\n",cacheId, 
                (bus->invalidationThs[getBlkNumber(addr)]>0)? "BusRdUpd" : "BusRdX", addr);

            busOp = (bus->invalidationThs[getBlkNumber(addr)]>0)? BusRdUpd : BusRdX;
            
            // This will be handled in handleCoherentMemResp
            if(addr == blk_addr && size == blockSize){
                // overwrite whole block
                bus->sendMemReq(requestPacket, false, (bus->invalidationThs[getBlkNumber(addr)]>0)? BusRdUpd : BusRdX);
            }
            else{
                bus->sendMemReq(requestPacket, true, (bus->invalidationThs[getBlkNumber(addr)]>0)? BusRdUpd : BusRdX);
            }
            
            // requestPacket = nullptr;
        }
    }

    busStatsUpdate(busOp, requestPacket->getSize());

}

// // Track if we're already handling a memory response to prevent reentrant calls
// thread_local bool handling_memory_response = false;

void AdaptCache::handleCoherentMemResp(PacketPtr respPacket) {
    // std::cerr << "adapt[" << cacheId << "] mem resp for addr " << std::hex << pkt->getAddr() << std::dec
    //           << " isResponse=" << pkt->isResponse() << " needsResponse=" << pkt->needsResponse() << "\n";
    DPRINTF(CCache, "adapt[%d] mem resp: %s\n", cacheId, respPacket->print());
    
    
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
        cacheLine &currCacheline = AdaptCacheMgr[setID].cacheSet[lineID];
        assert(currCacheline.cohState == AdaptState::SHARED_CLEAN || 
            currCacheline.cohState == AdaptState::SHARED_MOD);
        assert(!memoryFetch);

        if(currCacheline.cohState == AdaptState::SHARED_CLEAN){
            // print trans info
            // starting a write run
            assert(currCacheline.writeRunCounter == 0);
            currCacheline.writeRunCounter++;

            if(bus->sharedWire){
                DPRINTF(CCache, "STATE_PrWr: adapt[%d] storing DATA at addr %#x, Shared_Clean to Shared_Mod\n", cacheId, addr);
                // switch from other states to shared_mod -> have sent busupd
                // first update, does exist interrupt
                currCacheline.invalidCounter--;
            }
            else{
                DPRINTF(CCache, "STATE_PrWr: adapt[%d] storing DATA at addr %#x, Shared_Clean to Modified\n", cacheId, addr);

            }
        }
        else{
            if(bus->sharedWire){
                DPRINTF(CCache, "STATE_PrWr: adapt[%d] storing DATA at addr %#x, stay in Shared_Mod\n", cacheId, addr);
                // have sent busupd
                // it's not the first update, if there's remote access during two updates, reset counterR
                if(bus->remoteAccessWire){
                    // remote access interrupt write run
                    endWriteRun(addr, currCacheline.writeRunCounter);
                    // currCacheline.writeRunCounter = 0;
                    
                    // update invalid th
                    currCacheline.invalidCounter = bus->invalidationThs[getBlkNumber(addr)];
                } 
                currCacheline.invalidCounter--;
                currCacheline.writeRunCounter++;
            }
            else{
                DPRINTF(CCache, "STATE_PrWr: adapt[%d] storing DATA at addr %#x, Shared_Mod to Modified\n", cacheId, addr);
                // switch out from Sm
                // reset to corresponding one
                currCacheline.invalidCounter = bus->invalidationThs[getBlkNumber(addr)];
                // write run continues to Modified
                currCacheline.writeRunCounter++;
            }
        }
       
        currCacheline.cohState = bus->sharedWire ? AdaptState::SHARED_MOD : AdaptState::MODIFIED;
        currCacheline.dirty = true;
        currCacheline.clkFlag = 1;
        // can only modify parts that requested
        requestPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);

        printDataHex(&currCacheline.cacheBlock[0], blockSize);
        
        // Send response to CPU
        sendCpuResp(respPacket);
        
        // Release the bus since we're done with memory operation
        if (cacheId == bus->currentGranted) {
            //std::cerr << "adapt[" << cacheId << "] releasing bus after memory response\n";
            bus->release(cacheId);
        }
        blocked = false;
        return;

    }

    // if no hit, must be invalid
    // assert(lineID == NOT_EXIST);

    if(lineID == NOT_EXIST){
        evict(addr);

        lineID = allocate(addr);
    }

    cacheLine &currCacheline = AdaptCacheMgr[setID].cacheSet[lineID];
    assert(currCacheline.cohState == AdaptState::INVALID);
    assert(currCacheline.valid);

    if(isRead){
        // read miss
        assert(memoryFetch);
        // decide on exclusive or shared based on snoop result
        currCacheline.cohState = (bus->sharedWire)? AdaptState::SHARED_CLEAN : AdaptState::EXCLUSIVE;
        // reset shared wire
        bus->sharedWire = false;
        currCacheline.clkFlag = 1;
        respPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
        requestPacket->setDataFromBlock(&currCacheline.cacheBlock[0], blockSize);

        if(currCacheline.cohState == AdaptState::EXCLUSIVE){
            DPRINTF(CCache, "STATE_PrRd Miss: Adapt[%d] got DATA from read and Invalid to Exclusive\n\n", cacheId);
        }
        else{
            DPRINTF(CCache, "STATE_PrRd Miss: Adapt[%d] got DATA from read and Invalid to Shared Clean\n\n", cacheId);
        }
        
        printDataHex(&currCacheline.cacheBlock[0], blockSize);

    }
    else{
        // DPRINTF(CCache, "adapt[%d] storing %d in cache\n\n", cacheId, dataToWrite[0]);
        currCacheline.cohState = (bus->sharedWire)? AdaptState::SHARED_MOD : AdaptState::MODIFIED;
        currCacheline.dirty = true;
        currCacheline.clkFlag = 1;
        bus->sharedWire = false;
        assert(currCacheline.writeRunCounter == 0);
        // starting a write run
        currCacheline.writeRunCounter++;

        if(memoryFetch){
            respPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);
        }
        requestPacket->writeDataToBlock(&currCacheline.cacheBlock[0], blockSize);

        if(currCacheline.cohState == AdaptState::MODIFIED){
            DPRINTF(CCache, "STATE_PrWr Miss: Adapt[%d] write DATA and Invalid to Modified\n\n", cacheId);
        }
        else{
            DPRINTF(CCache, "STATE_PrWr Miss: Adapt[%d] write DATA and Invalid to Shared_Mod\n\n", cacheId);
            // BusUpd has been sent on write Miss with shared
            currCacheline.invalidCounter--;
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

void AdaptCache::handleCoherentSnoopedReq(PacketPtr pkt) {
    int lineID;
    long addr = pkt->getAddr();
    // bool isRemoteRead = pkt->isRead();

    uint64_t setID = getSet(addr);
    uint64_t tag = getTag(addr);
    bool snoopHit = isHit(addr, lineID);
    BusOperationType opType = bus->getOperationType(pkt);
    AdaptState currState;
    cacheLine *cachelinePtr;
    
    DPRINTF(CCache, "adapt[%d] received snoop for addr %#x opType=%d\n", 
            cacheId, addr, opType);
    
    // If we don't have the line, or it's not the same address, do nothing
    if (!snoopHit) {
        currState = AdaptState::INVALID;
    }
    else{
        currState = AdaptCacheMgr[setID].cacheSet[lineID].cohState;
        cachelinePtr = &AdaptCacheMgr[setID].cacheSet[lineID];
        // one or more caches have shared copies
        // if bus command is invalidate, keep the share line low to get
        // writer exclusive copy
        bus->sharedWire = (opType != BusRdX);
        bus->remoteAccessWire = cachelinePtr->accessSinceUpd;
    }
    
    switch(currState){

        case AdaptState::MODIFIED:

            // flush
            assert(cachelinePtr->dirty);
            assert(bus->hasBusRd(opType) || opType == BusRdX);
            // writeback data
            writeback(addr, &cachelinePtr->cacheBlock[0]);
            // bus stats record flush data
            bus->stats.rdBytes += blockSize;
            cachelinePtr->dirty = false;
            
            DPRINTF(CCache, "adapt[%d] snoop hit! Flush modified data\n\n", cacheId);

            // Update write run
            endWriteRun(addr, cachelinePtr->writeRunCounter);

            if(opType != BusRdX){
                cachelinePtr->cohState = AdaptState::SHARED_MOD;
                DPRINTF(CCache, "STATE_BusRd: adapt[%d] BusRd hit! set: %d, way: %d, tag: %d, Modified to Shared_Mod\n\n", cacheId, setID, lineID, tag);
    
                if(!bus->hasBusUpd(opType)){
                    break;
                }
                // intentional fall through when update after rd
            }
            else{
                cachelinePtr->cohState = AdaptState::INVALID;
                DPRINTF(CCache, "STATE_BusRdX: adapt[%d] BusRd hit! set: %d, way: %d, tag: %d, Modified to Invalid\n\n", cacheId, setID, lineID, tag);
                break;
            }



        case AdaptState::SHARED_MOD:

            // may or may not be synced with memory
            // can be busrd, bsupd or together
            if(opType != BusRdX){
                if(bus->hasBusRd(opType) && cachelinePtr->dirty){
                    writeback(addr, &cachelinePtr->cacheBlock[0]);
                    // bus stats record flush data
                    bus->stats.rdBytes += blockSize;
                    cachelinePtr->dirty = false;
                    DPRINTF(CCache, "adapt[%d] snoop hit! Flush shared modified data\n\n", cacheId);
                }
    
                if(bus->hasBusUpd(opType)){
                    assert(pkt->isWrite());
                    pkt->writeDataToBlock(&cachelinePtr->cacheBlock[0], blockSize);
                    cachelinePtr->cohState = AdaptState::SHARED_CLEAN;
                    cachelinePtr->dirty = false;
                    cachelinePtr->accessSinceUpd = false;
                    DPRINTF(CCache, "STATE_BusUpd: adapt[%d] BusUpd hit! set: %d, way: %d, tag: %d, Shared_Mod to Shared_Clean\n\n", cacheId, setID, lineID, tag);
                }
    
            }
            else{
                if(cachelinePtr->dirty){
                    writeback(addr, &cachelinePtr->cacheBlock[0]);
                    // bus stats record flush data
                    bus->stats.rdBytes += blockSize;
                    cachelinePtr->dirty = false;
                    DPRINTF(CCache, "adapt[%d] snoop hit! Flush shared modified data\n\n", cacheId);
                }

                cachelinePtr->cohState = AdaptState::INVALID;
                    DPRINTF(CCache, "STATE_BusUpd: adapt[%d] BusRdX hit! set: %d, way: %d, tag: %d, Shared_Mod to Invalid\n\n", cacheId, setID, lineID, tag);
                
            }

            // no matter what bus operation it is, an bus signal interrupt restores the original writer's counter
            if(cachelinePtr->writeRunCounter > 0){
                // prevent double update from the fall off
                endWriteRun(addr, cachelinePtr->writeRunCounter);
            }
            
            // currCacheline.writeRunCounter = 0;
            // update invalidTH
            cachelinePtr->invalidCounter = bus->invalidationThs[getBlkNumber(addr)];


            break;

        case AdaptState::EXCLUSIVE:

            assert(!cachelinePtr->dirty);
            assert(bus->hasBusRd(opType) || opType == BusRdX);
            assert(cachelinePtr->writeRunCounter == 0);

            if(opType != BusRdX){
                cachelinePtr->cohState = AdaptState::SHARED_CLEAN;
                DPRINTF(CCache, "STATE_BusRd: adapt[%d] BusRd hit! set: %d, way: %d, tag: %d, Exclusive to Shared_Clean\n\n", cacheId, setID, lineID, tag);

                if(!bus->hasBusUpd(opType)){
                    break;
                }
            }
            else{
                cachelinePtr->cohState = AdaptState::INVALID;
                DPRINTF(CCache, "STATE_BusRd: adapt[%d] BusRdX hit! set: %d, way: %d, tag: %d, Exclusive to Invalid\n\n", cacheId, setID, lineID, tag);
            }



            // intentional fall through when update after rd

        case AdaptState::SHARED_CLEAN:

            assert(cachelinePtr->writeRunCounter == 0);

            if(opType != BusRdX){
                if(bus->hasBusUpd(opType)){
                    assert(pkt->isWrite());
                    pkt->writeDataToBlock(&cachelinePtr->cacheBlock[0], blockSize);
                    cachelinePtr->accessSinceUpd = false;
                    DPRINTF(CCache, "STATE_BusUpd: adapt[%d] BusUpd hit! set: %d, way: %d, tag: %d, stay in Shared_Clean\n\n", cacheId, setID, lineID, tag);
                }
            }
            else{
                cachelinePtr->cohState = AdaptState::INVALID;
                DPRINTF(CCache, "STATE_BusRd: adapt[%d] BusRdX hit! set: %d, way: %d, tag: %d, Shared_Clean to Invalid\n\n", cacheId, setID, lineID, tag);
            }


            break;


        case AdaptState::INVALID:
            DPRINTF(CCache, "adapt[%d] snoop miss! nothing to do\n\n", cacheId);
            break;

        default:
            break;

    }

    // // erase busop map
    // bus->rmBusTrans(pkt);

}

} // namespace gem5
