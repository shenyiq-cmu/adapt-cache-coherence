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
                DPRINTF(CCache, "Mesi[%d] via PrWr(S') for addr %#x\n", 
                        cacheId, addr);
                
                // Store our state so we don't invalidate ourselves via snoop
                int oldState = cacheState;
                
                // Send a BusUpd to notify other caches
                bus->sendMemReq(requestPacket, false, BUS_UPDATE);
                
                // Make sure we restore our state
                cacheState = SHARED_MOD;
                valid = true;
                
                // Make response and send it
                if (requestPacket->needsResponse()) {
                    std::cerr << "MESI[" << cacheId << "] making response after Sc→Sm\n";
                    requestPacket->makeResponse();
                    sendCpuResp(requestPacket);
                }
                requestPacket = nullptr;
                
                // Release the bus
                std::cerr << "MESI[" << cacheId << "] releasing bus after Sc→Sm\n";
                bus->release(cacheId);
            }
            else if (cacheState == SHARED_MOD) {
                // Sm → Sm via PrWr(S) - need to update other shared copies
                std::cerr << "MESI[" << cacheId << "] Sm→Sm via PrWr(S)\n";
                DPRINTF(CCache, "Mesi[%d] Sm→Sm via PrWr(S) for addr %#x\n", 
                        cacheId, addr);
                
                // Store our state
                int oldState = cacheState;
                
                // Send a BusUpd to update other caches
                bus->sendMemReq(requestPacket, false, BUS_UPDATE);
                
                // Restore our state
                cacheState = oldState;
                valid = true;
                
                // Make response and send it
                if (requestPacket->needsResponse()) {
                    std::cerr << "MESI[" << cacheId << "] making response after Sm→Sm\n";
                    requestPacket->makeResponse();
                    sendCpuResp(requestPacket);
                }
                requestPacket = nullptr;
                
                // Release the bus
                std::cerr << "MESI[" << cacheId << "] releasing bus after Sm→Sm\n";
                bus->release(cacheId);
            }
            else if (cacheState == MODIFIED) {
                // M → Sm transition with PrWr(S')
                std::cerr << "MESI[" << cacheId << "] M→Sm transition via PrWr(S')\n";
                DPRINTF(CCache, "Mesi[%d] M→Sm via PrWr(S') for addr %#x\n", 
                        cacheId, addr);
                
                // Transition to shared modified state
                cacheState = SHARED_MOD;
                
                // Send BusUpd to notify other caches (no invalidation)
                bus->sendMemReq(requestPacket, false, BUS_UPDATE);
                
                // Make sure our state is preserved
                valid = true;
                
                // Make response and send it
                if (requestPacket->needsResponse()) {
                    std::cerr << "MESI[" << cacheId << "] making response after M→Sm\n";
                    requestPacket->makeResponse();
                    sendCpuResp(requestPacket);
                }
                requestPacket = nullptr;
                
                // Release the bus
                std::cerr << "MESI[" << cacheId << "] releasing bus after M→Sm\n";
                bus->release(cacheId);
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
        if (write) {
            // For write, we transition to M state
            cacheState = MODIFIED;
            std::cerr << "MESI[" << cacheId << "] installed addr " << std::hex << addr 
                     << std::dec << " in MODIFIED state\n";
            DPRINTF(CCache, "Mesi[%d] installed addr %#x in MODIFIED state\n", 
                    cacheId, addr);
        } else {
            // For read, we transition to E state if no other cache has it,
            // otherwise Sc state
            // This info is determined by snooping during bus transaction
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
        
        // Unblock the cache
        blocked = false;
        
        // Send response to CPU
        std::cerr << "MESI[" << cacheId << "] sending response to CPU\n";
        sendCpuResp(pkt);
        
        // Release the bus
        std::cerr << "MESI[" << cacheId << "] releasing bus after mem resp\n";
        bus->release(cacheId);
    }
    
    // Reset flag after handling
    handling_memory_response = false;
}

void MesiCache::handleCoherentSnoopedReq(PacketPtr pkt) {
    std::cerr << "MESI[" << cacheId << "] snoop for addr " << std::hex << pkt->getAddr() << std::dec
              << " isRead=" << pkt->isRead() << " isWrite=" << pkt->isWrite() << "\n";
    DPRINTF(CCache, "Mesi[%d] snoop: %s\n", cacheId, pkt->print());
    
    // Safety check - make sure bus has a valid originator
    if (bus->currentGranted == -1) {
        std::cerr << "MESI[" << cacheId << "] IGNORING SNOOP WITH NO ORIGINATOR for addr " 
                 << std::hex << pkt->getAddr() << std::dec << "\n";
        return;
    }
    
    // Don't snoop your own requests
    if (bus->currentGranted == cacheId) {
        std::cerr << "MESI[" << cacheId << "] IGNORING OWN REQUEST FOR ADDR " 
                 << std::hex << pkt->getAddr() << std::dec 
                 << " - CRITICAL CHECK\n";
        DPRINTF(CCache, "Mesi[%d] IGNORING OWN REQUEST\n", cacheId);
        return;
    }
    
    Addr addr = pkt->getAddr();
    bool read = pkt->isRead() && !pkt->isWrite();
    bool write = pkt->isWrite();
    
    // Get the bus operation type
    BusOperationType opType = bus->getOperationType(pkt);
    
    // Check if we have the address in cache
    if (valid && cachedAddr == addr && cacheState != INVALID) {
        // We have the cache line, respond to snoop
        if (read) {
            // BusRdMiss from another cache
            switch (cacheState) {
                case EXCLUSIVE:
                    // E → Sc transition
                    std::cerr << "MESI[" << cacheId << "] E→Sc transition on snoop\n";
                    DPRINTF(CCache, "Mesi[%d] E→Sc for addr %#x\n", cacheId, addr);
                    cacheState = SHARED_CLEAN;
                    bus->setShared(addr);  // Indicate that line is shared
                    break;
                    
                case MODIFIED:
                    // M → Sc transition with data update
                    std::cerr << "MESI[" << cacheId << "] M→Sc transition on snoop\n";
                    DPRINTF(CCache, "Mesi[%d] M→Sc for addr %#x\n", cacheId, addr);
                    cacheState = SHARED_CLEAN;
                    bus->setShared(addr);  // Indicate that line is shared
                    
                    // Write back our modified data
                    std::cerr << "MESI[" << cacheId << "] writing back modified data on snoop\n";
                    bus->sendWriteback(cacheId, addr, cacheData);
                    break;
                    
                case SHARED_CLEAN:
                    // Sc → Sc (no change)
                    std::cerr << "MESI[" << cacheId << "] Sc→Sc (no change) on snoop\n";
                    DPRINTF(CCache, "Mesi[%d] Sc→Sc (no change) for addr %#x\n", cacheId, addr);
                    bus->setShared(addr);  // Indicate that line is shared
                    break;
                    
                case SHARED_MOD:
                    // Sm → Sc with data update
                    std::cerr << "MESI[" << cacheId << "] Sm→Sc transition on snoop\n";
                    DPRINTF(CCache, "Mesi[%d] Sm→Sc for addr %#x\n", cacheId, addr);
                    cacheState = SHARED_CLEAN;
                    bus->setShared(addr);  // Indicate that line is shared
                    
                    // Write back our modified data
                    std::cerr << "MESI[" << cacheId << "] writing back modified data on snoop\n";
                    bus->sendWriteback(cacheId, addr, cacheData);
                    break;
                    
                default:
                    panic("Invalid state");
            }
        } else if (write) {
            // Could be BusWr or BusUpd
            if (opType == BUS_UPDATE) {
                // This is a BusUpd operation
                std::cerr << "MESI[" << cacheId << "] handling BusUpd operation\n";
                
                switch (cacheState) {
                    case EXCLUSIVE:
                        // E should not be possible with BusUpd
                        std::cerr << "MESI[" << cacheId << "] WARNING: Invalid state transition: E with BusUpd\n";
                        break;
                        
                    case MODIFIED:
                        // M should not be possible with BusUpd
                        std::cerr << "MESI[" << cacheId << "] WARNING: Invalid state transition: M with BusUpd\n";
                        break;
                        
                    case SHARED_CLEAN:
                    case SHARED_MOD:
                        // Sc/Sm → Sc with data update (no invalidation)
                        std::cerr << "MESI[" << cacheId << "] updating data on BusUpd\n";
                        DPRINTF(CCache, "Mesi[%d] updating data for addr %#x on BusUpd\n", 
                                cacheId, addr);
                        
                        // Update our data with the new value
                        cacheData = *(pkt->getPtr<unsigned char>());
                        cacheState = SHARED_CLEAN;
                        break;
                        
                    default:
                        panic("Invalid state");
                }
            } else {
                // This is a BusWr operation (invalidation)
                switch (cacheState) {
                    case EXCLUSIVE:
                    case MODIFIED:
                    case SHARED_CLEAN:
                    case SHARED_MOD:
                        // All states → I on BusWr (invalidation)
                        std::cerr << "MESI[" << cacheId << "] →I transition on write snoop\n";
                        DPRINTF(CCache, "Mesi[%d] →I for addr %#x due to BusWr\n", cacheId, addr);
                        cacheState = INVALID;
                        valid = false;
                        break;
                        
                    default:
                        panic("Invalid state");
                }
            }
        }
    }
}

}