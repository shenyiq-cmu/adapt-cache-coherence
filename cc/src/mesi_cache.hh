#pragma once

#include "mem/port.hh"
#include "params/MesiCache.hh"
#include "sim/sim_object.hh"

#include "coherent_cache_base.hh"
#include "src_740/serializing_bus.hh"

#include <list>
#include <vector>
#include <unordered_map>

namespace gem5 {

class MesiCache : public CoherentCacheBase {
   public:
    MesiCache(const MesiCacheParams &params);

    // coherence state machine, data storage etc. here
    enum class MesiState {
        Invalid,
        Modified,
        Shared,
        Exclusive,
        Error
    } state = MesiState::Invalid;

    typedef struct CacheLine{
        std::vector<uint8_t> cacheBlock;
        uint64_t tag;
        MesiState cohState;
        bool dirty;
        bool clkFlag;
        // for replacement, more like existence, should be the same
        // as found in tagMap
        bool valid; 
    } cacheLine;

    typedef struct CacheSetMgr{
        std::vector<CacheLine> cacheSet;
        std::unordered_map<uint64_t, int> tagMap;
        int clkPtr;
    } cacheSetMgr;

    // // single entry cache = all bits are used for tag
    // unsigned char data = 0;
    // long tag = 0;
    // bool dirty = false;

    // unsigned char dataToWrite = 0;
    std::vector<uint8_t> dataToWrite;

    // bool share[4096];

    int blockOffset = 5;
    int blockSize = 32;

    int setBit = 4;
    int numSets = 16;

    int cacheSizeBit = 15;
    int cacheSize = 32 * 1024;
    int numLines;

    std::vector<cacheSetMgr> MesiCacheMgr;

    uint64_t getTag(long addr);
    uint64_t getSet(long addr);
    bool isHit(long addr, int &lineID);
    int allocate(long addr);
    void evict(long addr);
    void writeback(long addr, uint8_t* data);
    void printDataHex(uint8_t* data, int length);
    uint64_t getBlkAddr(long addr);
    
    void handleCoherentCpuReq(PacketPtr pkt) override;
    void handleCoherentBusGrant() override;
    void handleCoherentMemResp(PacketPtr pkt) override;
    void handleCoherentSnoopedReq(PacketPtr pkt) override;
};
}