#pragma once

#include "mem/port.hh"
#include "params/CoherentCacheBase.hh"
#include "sim/sim_object.hh"

#include "src_740/serializing_bus.hh"

#include <list>

namespace gem5 {

class SerializingBus;

class CoherentCacheBase : public SimObject {
   public:
    class CpuSidePort : public ResponsePort {
       public:
        CoherentCacheBase *owner;
        PacketPtr blockedPacket = nullptr;
        bool needRetry = false;

        CpuSidePort(const std::string &name, CoherentCacheBase *owner)
            : ResponsePort(name, owner), owner(owner) {}

        AddrRangeList getAddrRanges() const override;
        void sendPacket(PacketPtr pkt);
        void trySendRetry();

        Tick recvAtomic(PacketPtr pkt) override { panic("recvAtomic unimpl."); }
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
    };

    typedef struct CACHEStats{
        int missCount;
        int hitCount;
    } CacheStats;

    // cache stats struct for all caches
    CacheStats localStats = {0, 0};

    CpuSidePort cpuPort;

    int cacheId = 0;
    bool blocked = false;

    // bus connected to other caches and memory
    SerializingBus* bus;

    // send CPU responses asynchronously
    std::list<PacketPtr> cpuRespQueue;
    EventFunctionWrapper cpuRespEvent;
    void processCpuResp();
    void sendCpuResp(PacketPtr pkt);

    PacketPtr requestPacket = nullptr;

    CoherentCacheBase(const CoherentCacheBaseParams &params);

    Port &getPort(const std::string &port_name,
                  PortID idx = InvalidPortID) override;

    AddrRangeList getAddrRanges() const;
    void init() override;

    void sendRangeChange();


    bool handleRequest(PacketPtr pkt);
    bool handleResponse(PacketPtr pkt);
    void handleFunctional(PacketPtr pkt);

    bool isCacheablePacket(PacketPtr pkt);

    void handleBusGrant();
    void handleSnoopedReq(PacketPtr pkt);

    virtual void handleCoherentCpuReq(PacketPtr pkt);
    virtual void handleCoherentBusGrant();
    virtual void handleCoherentMemResp(PacketPtr pkt);
    virtual void handleCoherentSnoopedReq(PacketPtr pkt);

    void busStatsUpdate(BusOperationType busop, int dataSize);

    virtual ~CoherentCacheBase() {}
};
}