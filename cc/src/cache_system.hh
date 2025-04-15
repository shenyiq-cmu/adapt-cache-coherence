#pragma once

// This header solves circular dependencies between coherent_cache_base.hh and serializing_bus.hh

// First declare all the classes to avoid circular dependencies
namespace gem5 {
    class CoherentCacheBase;
    class SerializingBus;
}

// Then include the original headers
#include "src_740/coherent_cache_base.hh"
#include "src_740/serializing_bus.hh"