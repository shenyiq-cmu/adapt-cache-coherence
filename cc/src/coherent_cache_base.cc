#include "src_740/coherent_cache_base.hh"
#include "base/trace.hh"
#include "debug/CCache.hh"

namespace gem5 {

CoherentCacheBase::CoherentCacheBase(const CoherentCacheBaseParams& params)
    : SimObject(params),
      cpuPort(params.name + ".cpu_side", this),
      cacheId(params.cache_id),
      blocked(false),
      bus(params.serializing_bus),
      cpuRespEvent([this](){ processCpuResp(); }, name()) {}


void CoherentCacheBase::init() {
    DPRINTF(CCache, "C[%d] registering\n\n", cacheId);
    bus->registerCache(cacheId, this);
}

void CoherentCacheBase::processCpuResp() {
    while(!(cpuRespQueue.size() == 0)) {
        auto first = cpuRespQueue.begin();
        auto pkt = *first;
        cpuRespQueue.erase(first);
        cpuPort.sendPacket(pkt);
        cpuPort.trySendRetry();
    }
}


void CoherentCacheBase::sendCpuResp(PacketPtr pkt) {
    cpuRespQueue.push_back(pkt);
    schedule(cpuRespEvent, curTick()+1);
}


Port& CoherentCacheBase::getPort(const std::string& port_name, PortID idx) {
    panic_if(idx != InvalidPortID, "This cache does not support vector ports!");

    if (port_name == "cpu_side") {
        return cpuPort;
    } else {
        // maybe the superclass has it?
        return SimObject::getPort(port_name, idx);
    }
}

AddrRangeList CoherentCacheBase::getAddrRanges() const {
    return bus->getAddrRanges();
}

void CoherentCacheBase::handleFunctional(PacketPtr pkt) {
    bus->sendMemReqFunctional(pkt);
}

void CoherentCacheBase::sendRangeChange() { cpuPort.sendRangeChange(); }

// todo: modify hardcode
bool CoherentCacheBase::isCacheablePacket(PacketPtr pkt) {
    auto addr = pkt->getAddr();
    return (addr >= 0x8000 && addr < 0x9000);
}

bool CoherentCacheBase::handleRequest(PacketPtr pkt) {
    if (blocked) {
        DPRINTF(CCache, "request %#x blocked!\n", pkt->getAddr());
        return false;
    }

    // is packet in cacheable range?
    if (isCacheablePacket(pkt)) {
            handleCoherentCpuReq(pkt);
    }
    else {
        blocked = true;
        requestPacket = pkt;
        // request the bus
        bus->request(cacheId);
    }

    return true;
}

bool CoherentCacheBase::handleResponse(PacketPtr pkt) {
    assert(blocked);

    if (isCacheablePacket(pkt)) {
        handleCoherentMemResp(pkt);
    } else {
        blocked = false;
        bus->release(cacheId);
        cpuPort.sendPacket(pkt);
        cpuPort.trySendRetry();
    }

    return true;
}

AddrRangeList CoherentCacheBase::CpuSidePort::getAddrRanges() const {
    return owner->getAddrRanges();
}

void CoherentCacheBase::CpuSidePort::recvFunctional(PacketPtr pkt) {
    return owner->handleFunctional(pkt);
}

bool CoherentCacheBase::CpuSidePort::recvTimingReq(PacketPtr pkt) {
    if (!owner->handleRequest(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

void CoherentCacheBase::CpuSidePort::sendPacket(PacketPtr pkt) {
    panic_if(blockedPacket != nullptr, "Should not try to send if blocked!");

    if (!sendTimingResp(pkt)) {
        blockedPacket = pkt;
    }
}

void CoherentCacheBase::CpuSidePort::recvRespRetry() {
    assert(blockedPacket != nullptr);
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    sendPacket(pkt);
}

void CoherentCacheBase::CpuSidePort::trySendRetry() {
    if (needRetry && blockedPacket == nullptr) {
        needRetry = false;
        sendRetryReq();
    }
}

void CoherentCacheBase::handleSnoopedReq(PacketPtr pkt) {
    if (isCacheablePacket(pkt)) {
        handleCoherentSnoopedReq(pkt);
    }
}

void CoherentCacheBase::handleBusGrant() {
    assert(requestPacket != nullptr);
    assert(cacheId == bus->currentGranted);

    if (isCacheablePacket(requestPacket)) {
        handleCoherentBusGrant();
    }
    else {
        bus->sendMemReq(requestPacket, true);
        requestPacket = nullptr;
    }
}

void CoherentCacheBase::handleCoherentCpuReq(PacketPtr pkt) {
    DPRINTF(CCache, "C[%d] cpu req: %s\n\n", cacheId, pkt->print());
    blocked = true;

    // store the packet and request bus access
    requestPacket = pkt;
    bus->request(cacheId);
}


void CoherentCacheBase::handleCoherentBusGrant() {
    DPRINTF(CCache, "C[%d] bus granted\n\n", cacheId);
    assert(requestPacket != nullptr);
    assert(cacheId == bus->currentGranted);

    // bus was granted, send the req to memory.
    // this send is guaranteed to succeed since the bus 
    // belongs to this cache for now
    bus->sendMemReq(requestPacket, true);
    requestPacket = nullptr;
    // rest shared wire
    bus->sharedWire = false;
}

void CoherentCacheBase::handleCoherentMemResp(PacketPtr pkt) {
    DPRINTF(CCache, "C[%d] mem resp: %s\n\n", cacheId, pkt->print());
    blocked = false;
    sendCpuResp(pkt);
    
    // signal that this cache is done with the bus
    bus->release(cacheId);
}

void CoherentCacheBase::handleCoherentSnoopedReq(PacketPtr pkt) {
    DPRINTF(CCache, "C[%d] snoop: %s\n\n", cacheId, pkt->print());
}

}