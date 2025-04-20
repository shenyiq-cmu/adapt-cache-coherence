#pragma once

#include "mem/port.hh"
#include "params/MesiCache.hh"
#include "sim/sim_object.hh"

#include "src_740/coherent_cache_base.hh"
#include "src_740/serializing_bus.hh"

#include <list>
#include <vector>
#include <unordered_map>

namespace gem5 {

// Define the cache states
enum class MesiState {
    INVALID = 0,
    EXCLUSIVE = 1,    // E: Exclusive clean
    MODIFIED = 2,     // M: Modified (dirty)
    SHARED_CLEAN = 3, // Sc: Shared clean
    SHARED_MOD = 4    // Sm: Shared modified
};

class MesiCache : public CoherentCacheBase {
private:
    // // Single address cache
    // bool valid;             // Whether the cache line is valid
    // Addr cachedAddr;        // The address currently in cache
    // int cacheState;         // State of the cache line (using enum MesiState)
    // unsigned char cacheData; // The actual data stored

public:

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

    MesiCache(const MesiCacheParams &params);

    // coherence state machine implementation
    void handleCoherentCpuReq(PacketPtr pkt) override;
    void handleCoherentBusGrant() override;
    void handleCoherentMemResp(PacketPtr respPacket) override;
    void handleCoherentSnoopedReq(PacketPtr pkt) override;
    
    // Helper method to get state name for logging
    const char* getStateName(MesiState state) {
        switch (state) {
            case MesiState::INVALID: return "INVALID";
            case MesiState::EXCLUSIVE: return "EXCLUSIVE";
            case MesiState::MODIFIED: return "MODIFIED";
            case MesiState::SHARED_CLEAN: return "SHARED_CLEAN";
            case MesiState::SHARED_MOD: return "SHARED_MOD";
            default: return "UNKNOWN";
        }
    }
};

}