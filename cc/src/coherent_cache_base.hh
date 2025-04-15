#pragma once

#include "mem/port.hh"
#include "params/CoherentCacheBase.hh"
#include "sim/sim_object.hh"

namespace gem5 {

// Forward declaration
class SerializingBus;

class CoherentCacheBase : public SimObject {
  protected:
    class CpuSidePort : public ResponsePort {
      private:
        CoherentCacheBase* owner;
        PacketPtr blockedPacket;
        bool needRetry;

      public:
        CpuSidePort(const std::string& name, CoherentCacheBase* owner)
            : ResponsePort(name, owner),
              owner(owner),
              blockedPacket(nullptr),
              needRetry(false) {}

        void sendPacket(PacketPtr pkt);
        void trySendRetry();

        AddrRangeList getAddrRanges() const override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
        
        // Add implementation for pure virtual function
        Tick recvAtomic(PacketPtr pkt) override {
            // In a timing-based model, atomic accesses aren't really used
            // This is just a placeholder that returns a fixed delay
            return 1;
        }
    };

    CpuSidePort cpuPort;
    const int cacheId;
    std::list<PacketPtr> cpuRespQueue;
    bool blocked;
    PacketPtr requestPacket;
    SerializingBus* bus;

    EventFunctionWrapper cpuRespEvent;

    bool isCacheablePacket(PacketPtr pkt);

    void processCpuResp();

  public:
    CoherentCacheBase(const CoherentCacheBaseParams& params);

    void init() override;

    Port& getPort(const std::string& port_name,
                  PortID idx = InvalidPortID) override;

    void sendRangeChange();

    void sendCpuResp(PacketPtr pkt);

    AddrRangeList getAddrRanges() const;

    void handleFunctional(PacketPtr pkt);

    bool handleRequest(PacketPtr pkt);
    bool handleResponse(PacketPtr pkt);

    void handleSnoopedReq(PacketPtr pkt);
    void handleBusGrant();
    
    // Make these non-pure virtual by providing default empty implementations
    virtual void handleCoherentCpuReq(PacketPtr pkt) {}
    virtual void handleCoherentBusGrant() {}
    virtual void handleCoherentMemResp(PacketPtr pkt) {}
    virtual void handleCoherentSnoopedReq(PacketPtr pkt) {}
};

}