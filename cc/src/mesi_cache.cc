#include "src_740/mesi_cache.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"
#include <iostream>

namespace gem5 {

// Define the cache states according to the diagram
enum MesiState {
    INVALID = 0,
    EXCLUSIVE = 1,    // E: Exclusive clean
    MODIFIED = 2,     // M: Modified (dirty)
    SHARED_CLEAN = 3, // Sc: Shared clean
    SHARED_MOD = 4    // Sm: Shared modified
};

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
            std::cerr << "MESI[" << cacheId << "] read hit in state " << cacheState << "\n";
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
            std::cerr << "MESI[" << cacheId << "] write hit in state " << cacheState << "\n";
            DPRINTF(CCache, "Mesi[%d] write hit in state %d\n", cacheId, cacheState);
            
            switch (cacheState) {
                case EXCLUSIVE:
                    // E → M on write
                    std::cerr << "MESI[" << cacheId << "] E→M transition\n";
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
                    // M → M on write (silent)
                    std::cerr << "MESI[" << cacheId << "] M→M (silent)\n";
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
                    
                case SHARED_CLEAN:
                case SHARED_MOD:
                    // Sc/Sm → Sm on write with PrWr(S) transaction
                    // Need to inform other caches via bus
                    std::cerr << "MESI[" << cacheId << "] " 
                             << (cacheState == SHARED_CLEAN ? "Sc" : "Sm") 
                             << "→Sm transition\n";
                    DPRINTF(CCache, "Mesi[%d] %s→Sm for addr %#x\n", 
                            cacheId, (cacheState == SHARED_CLEAN) ? "Sc" : "Sm", addr);
                    cacheState = SHARED_MOD;
                    cacheData = *(pkt->getPtr<unsigned char>());
                    
                    // We need bus access for this
                    blocked = true;
                    requestPacket = pkt;
                    std::cerr << "MESI[" << cacheId << "] requesting bus for shared write\n";
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
            if (cacheState == MODIFIED) {
                // Need to write back first
                std::cerr << "MESI[" << cacheId << "] writing back modified line " 
                         << std::hex << cachedAddr << std::dec << "\n";
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
    std::cerr << "MESI[" << cacheId << "] bus request for addr " << std::hex << addr << std::dec
              << " isRead=" << read << " isWrite=" << write 
              << " needsResponse=" << requestPacket->needsResponse() << "\n";
    
    // Check cache state after getting bus grant
    bool hit = valid && (cachedAddr == addr) && (cacheState != INVALID);
    
    if (hit) {
        // We had a hit but needed the bus (e.g., for write to shared line)
        if (write) {
            if (cacheState == SHARED_CLEAN || cacheState == SHARED_MOD) {
                // PrWr(S) - inform other caches about our write to shared line
                std::cerr << "MESI[" << cacheId << "] broadcasting PrWr(S)\n";
                DPRINTF(CCache, "Mesi[%d] broadcasting PrWr(S) for addr %#x\n", 
                        cacheId, addr);
                
                // Store our state so we don't invalidate ourselves via snoop
                // This is a critical fix: we'll retain our state even if we get a snoop
                int oldState = cacheState;
                
                // Send a write notification over the bus
                // This will cause other caches to invalidate or update their state
                bus->sendMemReq(requestPacket, false);
                
                // Make sure we restore our state in case it got invalidated
                cacheState = oldState;
                valid = true;
                
                // We already updated our local data in handleCoherentCpuReq
                // Just make the response and send it
                if (requestPacket->needsResponse()) {
                    std::cerr << "MESI[" << cacheId << "] making response after PrWr(S)\n";
                    requestPacket->makeResponse();
                    sendCpuResp(requestPacket);
                } else {
                    std::cerr << "MESI[" << cacheId << "] packet doesn't need response after PrWr(S)\n";
                }
                requestPacket = nullptr;
                
                // Release the bus
                std::cerr << "MESI[" << cacheId << "] releasing bus after PrWr(S)\n";
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
            bus->sendMemReq(requestPacket, true);
            requestPacket = nullptr;
        } else if (write) {
            // BusRdMiss + transition to M - read for ownership
            std::cerr << "MESI[" << cacheId << "] sending BusRdMiss for write (RFO)\n";
            DPRINTF(CCache, "Mesi[%d] sending BusRdMiss for write (RFO) addr %#x\n", 
                    cacheId, addr);
            
            // This will be handled in handleCoherentMemResp
            bus->sendMemReq(requestPacket, true);
            requestPacket = nullptr;
        }
    }
}

void MesiCache::handleCoherentMemResp(PacketPtr pkt) {
    std::cerr << "MESI[" << cacheId << "] mem resp for addr " << std::hex << pkt->getAddr() << std::dec
              << " isResponse=" << pkt->isResponse() << " needsResponse=" << pkt->needsResponse() << "\n";
    DPRINTF(CCache, "Mesi[%d] mem resp: %s\n", cacheId, pkt->print());
    
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
            // BusWr from another cache
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