#include "src_740/serializing_bus.hh"
#include "base/trace.hh"
#include "debug/SBus.hh"
#include <iostream>

namespace gem5 {

SerializingBus::SerializingBus(const SerializingBusParams& params)
    : SimObject(params),
      memPort(params.name + ".mem_side", this),
      memReqEvent([this](){ processMemReqEvent(); }, name()), 
      grantEvent([this](){ processGrantEvent(); }, name()) {}



void SerializingBus::processMemReqEvent() {
    while(!(memReqQueue.size() == 0)) {
        auto first = memReqQueue.begin();
        auto bundle = *first;
        memReqQueue.erase(first);

        // send snoops
        for (auto& it : cacheMap) {
            if (it.first != currentGranted) {
                it.second->handleSnoopedReq(bundle.first);
            }
        }

        // send to memory system?
        if (bundle.second) {
            memPort.sendPacket(bundle.first);
        }
        else {
            // cannot be a read packet!
            assert(!bundle.first->isRead());
            bundle.first->makeResponse();
            cacheMap[currentGranted]->handleResponse(bundle.first);
        }
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
    assert(currentGranted != -1);

    cacheMap[currentGranted]->handleResponse(pkt);
    return true;
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

void SerializingBus::sendMemReq(PacketPtr pkt, bool sendToMemory) {
    memReqQueue.push_back({pkt, sendToMemory});
    schedule(memReqEvent, curTick()+1);
}

void SerializingBus::request(int cacheId) {
    DPRINTF(SBus, "access request from %d\n\n", cacheId);
    busRequestQueue.push_back(cacheId);
    // if there is no request currently being handled
    // start the grant process
    if (currentGranted==-1 && !grantEvent.scheduled()) {
        schedule(grantEvent, curTick()+1);
    }
}

void SerializingBus::release(int cacheId) {
    DPRINTF(SBus, "release from %d\n\n", cacheId);
    assert(cacheId == currentGranted);
    currentGranted = -1;
    schedule(grantEvent, curTick()+1);
}

void SerializingBus::sendWriteback(int cacheId, long addr, unsigned char data) {
    DPRINTF(SBus, "sending writeback from %d @ %#x, %d\n\n", cacheId, addr, data);
    RequestPtr req = std::make_shared<Request>(addr, 1, 0, 0);
    PacketPtr new_pkt = new Packet(req, MemCmd::WriteReq, 1);
    unsigned char* dataBlock = new unsigned char[1];
    *dataBlock = data;
    new_pkt->dataDynamic(dataBlock);
    memPort.sendFunctional(new_pkt);
}

// need a data block version

void SerializingBus::sendBlkWriteback(int cacheId, long addr, uint8_t *data, int blockSize) {
    DPRINTF(SBus, "sending writeback from %d @ %#x\n\n", cacheId, addr);
    RequestPtr req = std::make_shared<Request>(addr, blockSize, 0, 0);
    PacketPtr new_pkt = new Packet(req, MemCmd::WriteReq, blockSize);
    unsigned char* dataBlock = new uint8_t[blockSize];
    memcpy(dataBlock, data, blockSize);
    new_pkt->dataDynamic(dataBlock);
    memPort.sendFunctional(new_pkt);
}

}