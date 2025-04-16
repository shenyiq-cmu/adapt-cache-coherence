#include "src_740/mesi_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"
#include <iostream>

namespace gem5 {

MesiCache::MesiCache(const MesiCacheParams& params) 
    : CoherentCacheBase(params),
      valid(false),
      cachedAddr(0),
      cacheState(INVALID),
      cacheData(0) {
    std::cerr << "MESI Cache " << cacheId << " created\n";
    DPRINTF(CCache, "Mesi[%d] cache created\n", cacheId);
}

void MesiCache::handleCoherentCpuReq(PacketPtr pkt) {
    std::cerr << "MESI[" << cacheId << "] cpu req for addr " << std::hex << pkt->getAddr() << std::dec
              << " isRead=" << pkt->isRead() << " isWrite=" << pkt->isWrite() 
              << " needsResponse=" << pkt->needsResponse() << "\n";
    DPRINTF(CCache, "Mesi[%d] cpu req: %s\n\n", cacheId, pkt->print());
    
    Addr addr = pkt->getAddr();
    bool read = pkt->isRead() && !pkt->isWrite();
    bool write = pkt->isWrite();
    
    // Check if we have the address in cache
    bool hit = valid && (cachedAddr == addr) && (cacheState != INVALID);
    
    if (hit) {
        // Cache hit
        if (read) {
            // Read hit - just return data
            std::cerr << "MESI[" << cacheId << "] read hit in state " << cacheState 
                     << " (" << getStateName(cacheState) << ")\n";
            DPRINTF(CCache, "Mesi[%d] read hit in state %d\n", cacheId, cacheState);
            
            // For all states, we can serve a read hit locally
            unsigned char* dataPtr = new unsigned char[1];
            *dataPtr = cacheData;
            
            if (pkt->needsResponse()) {
                std::cerr << "MESI[" << cacheId << "] making response for read hit\n";
                pkt->makeResponse();
                pkt->setData(dataPtr);
                sendCpuResp(pkt);
            } else {
                std::cerr << "MESI[" << cacheId << "] packet doesn't need response for read hit\n";
                delete[] dataPtr;
            }
            return;
        } else if (write) {
            // Write hit
            std::cerr << "MESI[" << cacheId << "] write hit in state " << cacheState 
                     << " (" << getStateName(cacheState) << ")\n";
            DPRINTF(CCache, "Mesi[%d] write hit in state %d\n", cacheId, cacheState);
            
            switch (cacheState) {
                case EXCLUSIVE:
                    // E → M on write (PrWr)
                    std::cerr << "MESI[" << cacheId << "] E→M transition with PrWr\n";
                    DPRINTF(CCache, "Mesi[%d] E→M transition for addr %#x\n", cacheId, addr);
                    cacheState = MODIFIED;
                    cacheData = *(pkt->getPtr<unsigned char>());
                    if (pkt->needsResponse()) {
                        std::cerr << "MESI[" << cacheId << "] making response for E→M\n";
                        pkt->makeResponse();
                        sendCpuResp(pkt);
                    } else {
                        std::cerr << "MESI[" << cacheId << "] packet doesn't need response for E→M\n";
                    }
                    return;
                    
                case MODIFIED:
                    // Check if this is a normal write or PrWr(S') transaction
                    // PrWr(S') would indicate we're intentionally transitioning to Sm
                    if (bus->hasShared(addr)) {
                        // M → Sm transition for PrWr(S')
                        std::cerr << "MESI[" << cacheId << "] M→Sm transition with PrWr(S')\n";
                        DPRINTF(CCache, "Mesi[%d] M→Sm transition for addr %#x\n", cacheId, addr);
                        cacheState = SHARED_MOD;
                        cacheData = *(pkt->getPtr<unsigned char>());
                        
                        // We need bus access for this transition
                        blocked = true;
                        requestPacket = pkt;
                        std::cerr << "MESI[" << cacheId << "] requesting bus for M→Sm transition\n";
                        bus->request(cacheId);
                        return;
                    } else {
                        // M → M on write (PrWr)
                        std::cerr << "MESI[" << cacheId << "] M→M (silent) with PrWr\n";
                        DPRINTF(CCache, "Mesi[%d] M→M (silent) for addr %#x\n", cacheId, addr);
                        cacheData = *(pkt->getPtr<unsigned char>());
                        if (pkt->needsResponse()) {
                            std::cerr << "MESI[" << cacheId << "] making response for M→M\n";
                            pkt->makeResponse();
                            sendCpuResp(pkt);
                        } else {
                            std::cerr << "MESI[" << cacheId << "] packet doesn't need response for M→M\n";
                        }
                        return;
                    }
                    
                case SHARED_CLEAN:
                    // Sc → Sm on write with PrWr(S') transaction
                    std::cerr << "MESI[" << cacheId << "] Sc→Sm transition with PrWr(S')\n";
                    DPRINTF(CCache, "Mesi[%d] Sc→Sm for addr %#x\n", cacheId, addr);
                    cacheState = SHARED_MOD;
                    cacheData = *(pkt->getPtr<unsigned char>());
                    
                    // We need bus access for this
                    blocked = true;
                    requestPacket = pkt;
                    std::cerr << "MESI[" << cacheId << "] requesting bus for Sc→Sm transition\n";
                    bus->request(cacheId);
                    return;
                    
                case SHARED_MOD:
                    // Sm → Sm on write with PrWr(S) transaction
                    std::cerr << "MESI[" << cacheId << "] Sm→Sm transition with PrWr(S)\n";
                    DPRINTF(CCache, "Mesi[%d] Sm→Sm for addr %#x\n", cacheId, addr);
                    // State remains SHARED_MOD
                    cacheData = *(pkt->getPtr<unsigned char>());
                    
                    // We need bus access for this to notify other caches
                    blocked = true;
                    requestPacket = pkt;
                    std::cerr << "MESI[" << cacheId << "] requesting bus for Sm→Sm transition\n";
                    bus->request(cacheId);
                    return;
                    
                default:
                    panic("Invalid state");
            }
        }
    } else {
        // Cache miss - need to get data from memory or other caches
        std::cerr << "MESI[" << cacheId << "] " << (read ? "read" : "write") 
                 << " miss for addr " << std::hex << addr << std::dec << "\n";
        DPRINTF(CCache, "Mesi[%d] %s miss for addr %#x\n", 
                cacheId, read ? "read" : "write", addr);
        
        // Handle replacement if we have a valid entry
        if (valid && cachedAddr != addr) {
            if (cacheState == MODIFIED || cacheState == SHARED_MOD) {
                // Need to write back first for both M and Sm states
                std::cerr << "MESI[" << cacheId << "] writing back modified line " 
                         << std::hex << cachedAddr << std::dec 
                         << " from state " << getStateName(cacheState) << "\n";
                DPRINTF(CCache, "Mesi[%d] writing back modified line %#x\n", 
                        cacheId, cachedAddr);
                bus->sendWriteback(cacheId, cachedAddr, cacheData);
            }
        }
        
        // For both read and write misses, we need to get bus access
        blocked = true;
        requestPacket = pkt;
        std::cerr << "MESI[" << cacheId << "] requesting bus for " 
                 << (read ? "read" : "write") << " miss\n";
        bus->request(cacheId);
    }
}

void MesiCache::handleCoherentBusGrant() {
    std::cerr << "MESI[" << cacheId << "] bus granted\n";
    DPRINTF(CCache, "Mesi[%d] bus granted\n\n", cacheId);
    
    assert(requestPacket != nullptr);
    assert(cacheId == bus->currentGranted);
    
    Addr addr = requestPacket->getAddr();
    bool read = requestPacket->isRead() && !requestPacket->isWrite();
    bool write = requestPacket->isWrite();
    
    // Check cache state after getting bus grant
    bool hit = valid && (cachedAddr == addr) && (cacheState != INVALID);
    
    if (hit) {
        // We had a hit but needed the bus (e.g., for write to shared line)
        if (write) {
            if (cacheState == SHARED_CLEAN) {
                // Sc → Sm transition via PrWr(S')
                std::cerr << "MESI[" << cacheId << "] Sc→Sm transition via PrWr(S')\n";
                DPRINTF(CCache, "Mesi[%d] Sc→Sm via PrWr(S') for addr %#x\n", 
                        cacheId, addr);
                
                // Update data and state before sending BusUpd
                cacheData = *(requestPacket->getPtr<unsigned char>());
                cacheState = SHARED_MOD;
                
                // Create a new write packet for BusUpd
                RequestPtr req = std::make_shared<Request>(addr, 1, 0, 0);
                PacketPtr updPkt = new Packet(req, MemCmd::WriteReq, 1);
                uint8_t* dataBlock = new uint8_t[1];
                *dataBlock = cacheData;
                updPkt->dataDynamic(dataBlock);
                
                // Send a BusUpd to notify other caches
                bus->sendMemReq(updPkt, false, BUS_UPDATE);
                
                // Make response to CPU before releasing bus
                if (requestPacket->needsResponse()) {
                    std::cerr << "MESI[" << cacheId << "] making response after Sc→Sm\n";
                    requestPacket->makeResponse();
                    sendCpuResp(requestPacket);
                }
                
                // Clear the request packet
                PacketPtr oldPkt = requestPacket;
                requestPacket = nullptr;
                
                // Release the bus
                std::cerr << "MESI[" << cacheId << "] releasing bus after Sc→Sm\n";
                bus->release(cacheId);
                
                // Unblock the cache
                blocked = false;
            }
            else if (cacheState == SHARED_MOD) {
                // Sm → Sm via PrWr(S)
                std::cerr << "MESI[" << cacheId << "] Sm→Sm via PrWr(S)\n";
                DPRINTF(CCache, "Mesi[%d] Sm→Sm via PrWr(S) for addr %#x\n", 
                        cacheId, addr);
                
                // Update the data
                cacheData = *(requestPacket->getPtr<unsigned char>());
                
                // Create a new write packet for BusUpd
                RequestPtr req = std::make_shared<Request>(addr, 1, 0, 0);
                PacketPtr updPkt = new Packet(req, MemCmd::WriteReq, 1);
                uint8_t* dataBlock = new uint8_t[1];
                *dataBlock = cacheData;
                updPkt->dataDynamic(dataBlock);
                
                // Send a BusUpd to update other caches
                bus->sendMemReq(updPkt, false, BUS_UPDATE);
                
                // Make response to CPU before releasing bus
                if (requestPacket->needsResponse()) {
                    std::cerr << "MESI[" << cacheId << "] making response after Sm→Sm\n";
                    requestPacket->makeResponse();
                    sendCpuResp(requestPacket);
                }
                
                // Clear the request packet
                PacketPtr oldPkt = requestPacket;
                requestPacket = nullptr;
                
                // Release the bus
                std::cerr << "MESI[" << cacheId << "] releasing bus after Sm→Sm\n";
                bus->release(cacheId);
                
                // Unblock the cache
                blocked = false;
            }
            else if (cacheState == MODIFIED) {
                // M → Sm transition with PrWr(S')
                std::cerr << "MESI[" << cacheId << "] M→Sm transition via PrWr(S')\n";
                DPRINTF(CCache, "Mesi[%d] M→Sm via PrWr(S') for addr %#x\n", 
                        cacheId, addr);
                
                // Update the data
                cacheData = *(requestPacket->getPtr<unsigned char>());
                
                // Transition to shared modified state
                cacheState = SHARED_MOD;
                
                // Create a new write packet for BusUpd
                RequestPtr req = std::make_shared<Request>(addr, 1, 0, 0);
                PacketPtr updPkt = new Packet(req, MemCmd::WriteReq, 1);
                uint8_t* dataBlock = new uint8_t[1];
                *dataBlock = cacheData;
                updPkt->dataDynamic(dataBlock);
                
                // Send BusUpd to notify other caches (no invalidation)
                bus->sendMemReq(updPkt, false, BUS_UPDATE);
                
                // Make response to CPU before releasing bus
                if (requestPacket->needsResponse()) {
                    std::cerr << "MESI[" << cacheId << "] making response after M→Sm\n";
                    requestPacket->makeResponse();
                    sendCpuResp(requestPacket);
                }
                
                // Clear the request packet
                PacketPtr oldPkt = requestPacket;
                requestPacket = nullptr;
                
                // Release the bus
                std::cerr << "MESI[" << cacheId << "] releasing bus after M→Sm\n";
                bus->release(cacheId);
                
                // Unblock the cache
                blocked = false;
            }
        }
    } else {
        // Cache miss - need to fetch from memory
        if (read) {
            // BusRdMiss - read miss, may get data from memory or other cache
            std::cerr << "MESI[" << cacheId << "] sending BusRdMiss\n";
            DPRINTF(CCache, "Mesi[%d] sending BusRdMiss for addr %#x\n", 
                    cacheId, addr);
            
            // This will be handled in handleCoherentMemResp
            bus->sendMemReq(requestPacket, true, BUS_READ);
            requestPacket = nullptr;
        } else if (write) {
            // BusRdMiss + transition to M - read for ownership (RFO)
            std::cerr << "MESI[" << cacheId << "] sending BusRdMiss for write (RFO)\n";
            DPRINTF(CCache, "Mesi[%d] sending BusRdMiss for write (RFO) addr %#x\n", 
                    cacheId, addr);
            
            // This will be handled in handleCoherentMemResp
            bus->sendMemReq(requestPacket, true, BUS_READ_EXCLUSIVE);
            requestPacket = nullptr;
        }
    }
}

// Track if we're already handling a memory response to prevent reentrant calls
thread_local bool handling_memory_response = false;

void MesiCache::handleCoherentMemResp(PacketPtr pkt) {
    std::cerr << "MESI[" << cacheId << "] mem resp for addr " << std::hex << pkt->getAddr() << std::dec
              << " isResponse=" << pkt->isResponse() << " needsResponse=" << pkt->needsResponse() << "\n";
    DPRINTF(CCache, "Mesi[%d] mem resp: %s\n", cacheId, pkt->print());
    
    // Check for reentrant call
    if (handling_memory_response) {
        std::cerr << "MESI[" << cacheId << "] WARNING: Reentrant memory response handling detected\n";
        return;
    }
    
    // Set flag to prevent reentrance
    handling_memory_response = true;
    
    Addr addr = pkt->getAddr();
    bool write = pkt->isWrite();
    
    // Handle the response from memory
    if (pkt->isResponse()) {
        // Update cache entry
        cachedAddr = addr;
        valid = true;
        cacheData = *(pkt->getPtr<unsigned char>());
        
        // Determine the final state based on the original request
        BusOperationType opType = bus->getOperationType(pkt);
        
        if (opType == BUS_READ_EXCLUSIVE) {
            // For BusRdX (read for ownership), we transition to M state
            cacheState = MODIFIED;
            std::cerr << "MESI[" << cacheId << "] installed addr " << std::hex << addr 
                     << std::dec << " in MODIFIED state\n";
            DPRINTF(CCache, "Mesi[%d] installed addr %#x in MODIFIED state\n", 
                    cacheId, addr);
        } else {
            // For BusRd (normal read), we transition to E state if no other cache has it,
            // otherwise Sc state
            if (bus->hasShared(addr)) {
                cacheState = SHARED_CLEAN;
                std::cerr << "MESI[" << cacheId << "] installed addr " << std::hex << addr 
                         << std::dec << " in SHARED_CLEAN state\n";
                DPRINTF(CCache, "Mesi[%d] installed addr %#x in SHARED_CLEAN state\n", 
                        cacheId, addr);
            } else {
                cacheState = EXCLUSIVE;
                std::cerr << "MESI[" << cacheId << "] installed addr " << std::hex << addr 
                         << std::dec << " in EXCLUSIVE state\n";
                DPRINTF(CCache, "Mesi[%d] installed addr %#x in EXCLUSIVE state\n", 
                        cacheId, addr);
            }
        }
        
        // Create a response to the original request if needed
        if (pkt->needsResponse()) {
            pkt->makeResponse();
        }
        
        // Send response to CPU
        sendCpuResp(pkt);
        
        // Release the bus since we're done with memory operation
        if (cacheId == bus->currentGranted) {
            std::cerr << "MESI[" << cacheId << "] releasing bus after memory response\n";
            bus->release(cacheId);
        }
    } else {
        // Non-response packets - handle special cases like evictions
        std::cerr << "MESI[" << cacheId << "] non-response packet from memory\n";
        
        // For writeback acknowledgements or other special cases
        if (pkt->isWrite()) {
            std::cerr << "MESI[" << cacheId << "] writeback acknowledgement\n";
            
            // Release the bus if we still have it
            if (cacheId == bus->currentGranted) {
                std::cerr << "MESI[" << cacheId << "] releasing bus after writeback\n";
                bus->release(cacheId);
            }
        }
    }
    
    // Unblock the cache
    blocked = false;
    
    // Reset the handling flag
    handling_memory_response = false;
}

void MesiCache::handleCoherentSnoopedReq(PacketPtr pkt) {
    Addr addr = pkt->getAddr();
    BusOperationType opType = bus->getOperationType(pkt);
    
    std::cerr << "MESI[" << cacheId << "] received snoop for addr " << std::hex << addr << std::dec
              << " opType=" << opType 
              << " cachedAddr=" << std::hex << cachedAddr << std::dec
              << " valid=" << valid 
              << " state=" << (valid ? getStateName(cacheState) : "INVALID") << "\n";
    
    DPRINTF(CCache, "Mesi[%d] received snoop for addr %#x opType=%d\n", 
            cacheId, addr, opType);
    
    // If we don't have the line, or it's not the same address, do nothing
    if (!valid || cachedAddr != addr) {
        std::cerr << "MESI[" << cacheId << "] snoop miss\n";
        DPRINTF(CCache, "Mesi[%d] snoop miss\n", cacheId);
        return;
    }
    
    // We have the line and it's the address being snooped
    switch (opType) {
        case BUS_READ:
            // BusRd - another cache wants to read this line
            std::cerr << "MESI[" << cacheId << "] BusRd snoop in state " << getStateName(cacheState) << "\n";
            DPRINTF(CCache, "Mesi[%d] BusRd snoop in state %s\n", cacheId, getStateName(cacheState));
            
            switch (cacheState) {
                case INVALID:
                    // Nothing to do
                    break;
                    
                case EXCLUSIVE:
                    // E → Sc transition (dragon protocol)
                    std::cerr << "MESI[" << cacheId << "] E→Sc transition on BusRd\n";
                    DPRINTF(CCache, "Mesi[%d] E→Sc transition on BusRd for addr %#x\n", cacheId, addr);
                    cacheState = SHARED_CLEAN;
                    bus->setShared(addr); // Mark as shared in the bus
                    
                    // Provide data to the bus (implementation might vary)
                    // This could be done by setting data in the packet or other mechanism
                    pkt->setData(&cacheData);
                    break;
                    
                case MODIFIED:
                    // M → Sc transition: flush to memory and transition to Sc
                    std::cerr << "MESI[" << cacheId << "] M→Sc transition on BusRd\n";
                    DPRINTF(CCache, "Mesi[%d] M→Sc transition on BusRd for addr %#x\n", cacheId, addr);
                    
                    // Write back to memory
                    bus->sendWriteback(cacheId, addr, cacheData);
                    
                    // Change state to shared clean
                    cacheState = SHARED_CLEAN;
                    bus->setShared(addr); // Mark as shared in the bus
                    
                    // Provide data to the bus
                    pkt->setData(&cacheData);
                    break;
                    
                case SHARED_CLEAN:
                    // Sc → Sc (no change)
                    std::cerr << "MESI[" << cacheId << "] Sc→Sc (no change) on BusRd\n";
                    DPRINTF(CCache, "Mesi[%d] Sc→Sc (no change) on BusRd for addr %#x\n", cacheId, addr);
                    bus->setShared(addr); // Ensure it's marked as shared
                    pkt->setData(&cacheData);
                    break;
                    
                case SHARED_MOD:
                    // Sm → Sc transition: flush to memory
                    std::cerr << "MESI[" << cacheId << "] Sm→Sc transition on BusRd\n";
                    DPRINTF(CCache, "Mesi[%d] Sm→Sc transition on BusRd for addr %#x\n", cacheId, addr);
                    
                    // Write back to memory
                    bus->sendWriteback(cacheId, addr, cacheData);
                    
                    // Change state to shared clean
                    cacheState = SHARED_CLEAN;
                    bus->setShared(addr); // Mark as shared
                    
                    // Provide data to the bus
                    pkt->setData(&cacheData);
                    break;
            }
            break;
            
        case BUS_READ_EXCLUSIVE:
            // BusRdX - another cache wants exclusive access
            std::cerr << "MESI[" << cacheId << "] BusRdX snoop in state " << getStateName(cacheState) << "\n";
            DPRINTF(CCache, "Mesi[%d] BusRdX snoop in state %s\n", cacheId, getStateName(cacheState));
            
            switch (cacheState) {
                case INVALID:
                    // Nothing to do
                    break;
                    
                case EXCLUSIVE:
                    // E → I transition
                    std::cerr << "MESI[" << cacheId << "] E→I transition on BusRdX\n";
                    DPRINTF(CCache, "Mesi[%d] E→I transition on BusRdX for addr %#x\n", cacheId, addr);
                    cacheState = INVALID;
                    valid = false;
                    
                    // Provide data to the bus
                    pkt->setData(&cacheData);
                    break;
                    
                case MODIFIED:
                    // M → I transition: flush to memory
                    std::cerr << "MESI[" << cacheId << "] M→I transition on BusRdX\n";
                    DPRINTF(CCache, "Mesi[%d] M→I transition on BusRdX for addr %#x\n", cacheId, addr);
                    
                    // Write back to memory
                    bus->sendWriteback(cacheId, addr, cacheData);
                    
                    // Change state to invalid
                    cacheState = INVALID;
                    valid = false;
                    
                    // Provide data to the bus
                    pkt->setData(&cacheData);
                    break;
                    
                case SHARED_CLEAN:
                    // Sc → I transition
                    std::cerr << "MESI[" << cacheId << "] Sc→I transition on BusRdX\n";
                    DPRINTF(CCache, "Mesi[%d] Sc→I transition on BusRdX for addr %#x\n", cacheId, addr);
                    cacheState = INVALID;
                    valid = false;
                    
                    // Provide data (optional, since it's already clean)
                    pkt->setData(&cacheData);
                    break;
                    
                case SHARED_MOD:
                    // Sm → I transition: flush to memory
                    std::cerr << "MESI[" << cacheId << "] Sm→I transition on BusRdX\n";
                    DPRINTF(CCache, "Mesi[%d] Sm→I transition on BusRdX for addr %#x\n", cacheId, addr);
                    
                    // Write back to memory
                    bus->sendWriteback(cacheId, addr, cacheData);
                    
                    // Change state to invalid
                    cacheState = INVALID;
                    valid = false;
                    
                    // Provide data to the bus
                    pkt->setData(&cacheData);
                    break;
            }
            break;
            
        case BUS_UPDATE:
            // BusUpd - another cache is updating a shared line
            std::cerr << "MESI[" << cacheId << "] BusUpd snoop in state " << getStateName(cacheState) << "\n";
            DPRINTF(CCache, "Mesi[%d] BusUpd snoop in state %s\n", cacheId, getStateName(cacheState));
            
            // Make sure address is marked as shared
            bus->setShared(addr);
            
            switch (cacheState) {
                case INVALID:
                    // Nothing to do
                    break;
                    
                case EXCLUSIVE:
                    // Should not happen in Dragon protocol - but handle anyway
                    std::cerr << "MESI[" << cacheId << "] WARNING: BusUpd in EXCLUSIVE state - updating to SHARED_CLEAN\n";
                    cacheState = SHARED_CLEAN;
                    cacheData = *(pkt->getPtr<unsigned char>());
                    break;
                    
                case MODIFIED:
                    // Should not happen in Dragon protocol - but handle anyway
                    std::cerr << "MESI[" << cacheId << "] WARNING: BusUpd in MODIFIED state - updating to SHARED_CLEAN\n";
                    cacheState = SHARED_CLEAN;
                    cacheData = *(pkt->getPtr<unsigned char>());
                    break;
                    
                case SHARED_CLEAN:
                    // Sc → Sc: Update local data
                    std::cerr << "MESI[" << cacheId << "] Sc→Sc (update data) on BusUpd\n";
                    DPRINTF(CCache, "Mesi[%d] Sc→Sc (update data) on BusUpd for addr %#x\n", cacheId, addr);
                    
                    // Update the data from the packet
                    cacheData = *(pkt->getPtr<unsigned char>());
                    break;
                    
                case SHARED_MOD:
                    // Sm → Sc: Update local data and change state
                    std::cerr << "MESI[" << cacheId << "] Sm→Sc transition on BusUpd\n";
                    DPRINTF(CCache, "Mesi[%d] Sm→Sc transition on BusUpd for addr %#x\n", cacheId, addr);
                    
                    // Update the data from the packet
                    cacheData = *(pkt->getPtr<unsigned char>());
                    
                    // Change state to shared clean
                    cacheState = SHARED_CLEAN;
                    break;
            }
            break;
    }
}

} // namespace gem5