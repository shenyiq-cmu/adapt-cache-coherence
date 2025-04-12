#pragma once

#include "mem/port.hh"
#include "params/SerializingBus.hh"
#include "sim/sim_object.hh"
#include "src_740/coherent_cache_base.hh"
#include <list>
#include <map>

namespace gem5 {

class CoherentCacheBase;

class SerializingBus : public SimObject {
   public:
    
    class MemSidePort : public RequestPort {
       public:
        SerializingBus *owner;
        PacketPtr blockedPacket = nullptr;

        MemSidePort(const std::string &name, SerializingBus *owner)
            : RequestPort(name, owner), owner(owner) {}

        void sendPacket(PacketPtr pkt);

        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    MemSidePort memPort;

    std::list<std::pair<PacketPtr, bool>> memReqQueue;
    EventFunctionWrapper memReqEvent;
    void processMemReqEvent();

    std::list<int> busRequestQueue;
    int currentGranted = -1;
    EventFunctionWrapper grantEvent;
    void processGrantEvent();

    std::map<int, CoherentCacheBase*> cacheMap;

    SerializingBus(const SerializingBusParams &params);

    Port &getPort(const std::string &port_name,
                  PortID idx = InvalidPortID) override;

    AddrRangeList getAddrRanges() const;
    void sendRangeChange();

    bool handleResponse(PacketPtr pkt);
    void sendMemReqFunctional(PacketPtr pkt);


    // public API
    void sendMemReq(PacketPtr pkt, bool sendToMemory);
    void registerCache(int cacheId, CoherentCacheBase* cache);
    void request(int cacheId);
    void release(int cacheId);
    void sendWriteback(int cacheId, long addr, unsigned char data);
    void sendBlkWriteback(int cacheId, long addr, uint8_t *data, int blockSize);
};
}