#pragma once

#include "mem/port.hh"
#include "params/SerializingBus.hh"
#include "sim/sim_object.hh"

#include <list>
#include <map>
#include <unordered_set>
#include <tuple>

namespace gem5 {

// Forward declaration
class CoherentCacheBase;

// Define bus operation types
enum BusOperationType {
    BusRdX = 0, // BusRdX - read exclusive (invalidate other copies)
    BusRd = 1,           // BusRd - read request
    BusUpd = 2,          // BusUpd - update operation for Sm state
    BusRdUpd = 3
};

typedef struct BUSStats{
  int transCount;
} BusStats;


class SerializingBus : public SimObject {
  private:
    // port for memory
    class MemSidePort : public RequestPort {
      private:
        SerializingBus* owner;
        PacketPtr blockedPacket;

      public:
        MemSidePort(const std::string& name, SerializingBus* owner)
            : RequestPort(name, owner), owner(owner), blockedPacket(nullptr) {}

        void sendPacket(PacketPtr pkt);

        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;
    };

    // Memory side port
    MemSidePort memPort;

    // Map from cache ID to cache object
    std::map<int, CoherentCacheBase*> cacheMap;

    // List of pending memory requests (packet, sendToMemory, originator)
    std::list<std::tuple<PacketPtr, bool, int>> memReqQueue;

    // // Track the operation type for each packet
    // std::map<PacketPtr, BusOperationType> packetOpTypes;

    // List of caches waiting for bus
    std::list<int> busRequestQueue;

    // Events for sending memory requests and granting the bus
    EventFunctionWrapper memReqEvent;
    EventFunctionWrapper grantEvent;
    
    // Event handling functions
    void generateAlignAccess(PacketPtr pkt);
    void processMemReqEvent();
    void processGrantEvent();

    // Set of addresses currently in shared state
    // std::unordered_set<Addr> sharedAddresses;

  public:
    // The cache that currently has bus access - made public so caches can check
    int currentGranted;

    int cacheBlockSize = 32;

    bool sharedWire = false;

    BusOperationType currBusOp = BusRd;

    // statistics

    BusStats stats = {0};

    SerializingBus(const SerializingBusParams& params);

    Port& getPort(const std::string& port_name, PortID idx = InvalidPortID) override;

    void sendMemReq(PacketPtr pkt, bool sendToMemory, BusOperationType opType);
    void sendMemReqFunctional(PacketPtr pkt);

    void registerCache(int cacheId, CoherentCacheBase* cache);

    bool handleResponse(PacketPtr pkt);

    AddrRangeList getAddrRanges() const;
    void sendRangeChange();

    // request bus access
    void request(int cacheId);

    // release bus
    void release(int cacheId);

    // write back
    void sendWriteback(int cacheId, long addr, unsigned char data);
    
    // block write back
    void sendBlkWriteback(int cacheId, long addr, uint8_t *data, int blockSize);

    // // Methods for shared state tracking
    // bool hasShared(Addr addr) const { return sharedAddresses.find(addr) != sharedAddresses.end(); }
    // void setShared(Addr addr) { sharedAddresses.insert(addr); }
    // void clearShared(Addr addr) { sharedAddresses.erase(addr); 

    
    
    bool hasBusRd(int BusOp){
      return BusOp & BusRd;
    }

    bool hasBusUpd(int BusOp){
      return BusOp & BusUpd;
    }

    // Get operation type for a packet
    BusOperationType getOperationType(PacketPtr pkt) {
        // auto it = packetOpTypes.find(pkt);
        // if (it != packetOpTypes.end()) {
        //     return it->second;
        // }
        // // Default to READ if not found
        // return BusRd;
        return currBusOp;
    }


    // void rmBusTrans(PacketPtr pkt) {
    //   packetOpTypes.erase(pkt);
    // }
};

}