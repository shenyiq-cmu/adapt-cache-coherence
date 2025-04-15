#include "src_740/serializing_bus.hh"
#include "src_740/coherent_cache_base.hh"
#include "base/trace.hh"
#include "debug/SBus.hh"
#include <iostream>

namespace gem5 {

SerializingBus::SerializingBus(const SerializingBusParams& params)
    : SimObject(params),
      memPort(params.name + ".mem_side", this),
      memReqEvent([this](){ processMemReqEvent(); }, name()), 
      grantEvent([this](){ processGrantEvent(); }, name()),
      currentGranted(-1) {}

void SerializingBus::processMemReqEvent() {
    // If there's no valid originator but we have pending requests,
    // delay processing until later when we might have a valid originator
    if (currentGranted == -1 && !memReqQueue.empty()) {
        if (!memReqEvent.scheduled()) {
            schedule(memReqEvent, curTick()+100);
        }
        return;
    }
    
    while(!memReqQueue.empty()) {
        auto first = memReqQueue.begin();
        auto bundle = *first;
        memReqQueue.erase(first);

        // Unpack the tuple
        PacketPtr pkt = std::get<0>(bundle);
        bool sendToMemory = std::get<1>(bundle);
        int originator = std::get<2>(bundle);

        // Reset shared flag for this transaction
        Addr addr = pkt->getAddr();
        bool isRead = pkt->isRead() && !pkt->isWrite();
        
        // Get the operation type
        BusOperationType opType = getOperationType(pkt);
        
        if (isRead) {
            // For read requests, clear shared status initially
            // It will be set again during snooping if any cache has the line
            clearShared(addr);
        }

        // Send snoops to all other caches (not the originating cache)
        for (auto& it : cacheMap) {
            // Only send snoops if there's a valid originator and it's not this cache
            if (originator != -1 && it.first != originator) {
                std::cerr << "Bus sending snoop to cache " << it.first 
                          << " (originator was " << originator 
                          << ", opType=" << opType << ")\n";
                DPRINTF(SBus, "Bus sending snoop to cache %d (originator was %d, opType=%d)\n", 
                        it.first, originator, opType);
                it.second->handleSnoopedReq(pkt);
            } else {
                std::cerr << "Bus SKIPPING snoop to cache " << it.first 
                          << " (originator was " << originator << ")\n";
                DPRINTF(SBus, "Bus SKIPPING snoop to cache %d (originator was %d)\n", 
                        it.first, originator);
            }
        }

        // Send to memory system or process locally based on the sendToMemory flag
        if (sendToMemory) {
            memPort.sendPacket(pkt);
        }
        else {
            // Cannot be a read packet!
            assert(!pkt->isRead());
            
            // Make response only if needed and if there's a valid originator
            if (originator != -1) {
                if (pkt->needsResponse()) {
                    pkt->makeResponse();
                }
                cacheMap[originator]->handleResponse(pkt);
            } else {
                std::cerr << "Bus: Warning - no valid originator to handle response\n";
            }
        }
        
        // Clean up the operation type entry
        packetOpTypes.erase(pkt);
    }
}

Port& SerializingBus::getPort(const std::string& port_name, PortID idx) {
    panic_if(idx != InvalidPortID, "this bus does not support vector ports!");

    if (port_name == "mem_side") {
        return memPort;
    } else {
        // maybe the superclass has it?
        return SimObject::getPort(port_name, idx);
    }
}

AddrRangeList SerializingBus::getAddrRanges() const {
    return memPort.getAddrRanges();
}

void SerializingBus::sendRangeChange() {
    for (auto& it : cacheMap) {
        it.second->sendRangeChange(); 
    }
}

bool SerializingBus::handleResponse(PacketPtr pkt) {
    // Make sure we have a valid currentGranted before accessing the cache map
    if (currentGranted != -1) {
        cacheMap[currentGranted]->handleResponse(pkt);
        return true;
    } else {
        std::cerr << "Bus: Warning - received response with no valid granted cache\n";
        return false;
    }
}

void SerializingBus::MemSidePort::recvRangeChange() {
    owner->sendRangeChange();
}

void SerializingBus::MemSidePort::sendPacket(PacketPtr pkt) {
    panic_if(blockedPacket != nullptr, "Should not try to send if blocked!");
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

void SerializingBus::MemSidePort::recvReqRetry() {
    panic_if(blockedPacket == nullptr, "Retrying null packet!");

    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    sendPacket(pkt);
}

void SerializingBus::registerCache(int cacheId, CoherentCacheBase* cache) {
    cacheMap[cacheId] = cache;
}

bool SerializingBus::MemSidePort::recvTimingResp(PacketPtr pkt) {
    return owner->handleResponse(pkt);
}

void SerializingBus::processGrantEvent() {
    assert(currentGranted == -1);

    if (busRequestQueue.size() != 0) {
        auto requestIt = busRequestQueue.begin();
        int requestingCache = *requestIt;
        busRequestQueue.erase(requestIt);
        currentGranted = requestingCache;
        DPRINTF(SBus, "granting %d\n\n", currentGranted);
        cacheMap[requestingCache]->handleBusGrant();
    }
}

void SerializingBus::sendMemReqFunctional(PacketPtr pkt) {
    memPort.sendFunctional(pkt);
}

void SerializingBus::sendMemReq(PacketPtr pkt, bool sendToMemory, BusOperationType opType) {
    // Store the operation type
    packetOpTypes[pkt] = opType;
    
    // Store the request in the queue with the current granted cache as originator
    memReqQueue.push_back(std::make_tuple(pkt, sendToMemory, currentGranted));
    
    // Schedule the event to process the request
    if (!memReqEvent.scheduled()) {
        schedule(memReqEvent, curTick()+1);
    }
}

void SerializingBus::request(int cacheId) {
    DPRINTF(SBus, "access request from %d\n\n", cacheId);
    
    // Add the request to the queue
    busRequestQueue.push_back(cacheId);
    
    // If there is no request currently being handled, start the grant process
    if (currentGranted == -1 && !grantEvent.scheduled()) {
        schedule(grantEvent, curTick()+1);
    }
}

void SerializingBus::release(int cacheId) {
    DPRINTF(SBus, "release from %d\n\n", cacheId);
    
    // Check if this cache actually has the bus before asserting
    if (cacheId != currentGranted) {
        std::cerr << "Warning: Cache " << cacheId 
                 << " tried to release bus but currentGranted is " 
                 << currentGranted << "\n";
        return;  // Just return without asserting
    }
    
    // Release the bus
    currentGranted = -1;
    
    // Schedule the event to potentially grant the bus to another cache
    if (!grantEvent.scheduled()) {
        schedule(grantEvent, curTick()+1);
    }
}

void SerializingBus::sendWriteback(int cacheId, long addr, unsigned char data) {
    DPRINTF(SBus, "sending writeback from %d @ %#x, %d\n\n", cacheId, addr, data);
    
    // Create a new request
    RequestPtr req = std::make_shared<Request>(addr, 1, 0, 0);
    
    // Create a write packet
    PacketPtr new_pkt = new Packet(req, MemCmd::WriteReq, 1);
    
    // Set up the data
    unsigned char* dataBlock = new unsigned char[1];
    *dataBlock = data;
    new_pkt->dataDynamic(dataBlock);
    
    // Send the packet functionally (i.e., immediately update memory)
    memPort.sendFunctional(new_pkt);
    
    // Clean up
    delete new_pkt;
}

void SerializingBus::sendBlkWriteback(int cacheId, long addr, uint8_t *data, int blockSize) {
    DPRINTF(SBus, "sending writeback from %d @ %#x\n\n", cacheId, addr);
    RequestPtr req = std::make_shared<Request>(addr, blockSize, 0, 0);
    PacketPtr new_pkt = new Packet(req, MemCmd::WriteReq, blockSize);
    unsigned char* dataBlock = new uint8_t[blockSize];
    memcpy(dataBlock, data, blockSize);
    new_pkt->dataDynamic(dataBlock);
    memPort.sendFunctional(new_pkt);
    
    // Clean up
    delete new_pkt;
}

}