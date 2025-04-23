from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject


class CoherentCacheBase(SimObject):
    type = 'CoherentCacheBase'
    cxx_header = 'src_740/coherent_cache_base.hh'
    cxx_class = 'gem5::CoherentCacheBase'

    cpu_side = ResponsePort('CPU side port, receives reqs')
    serializing_bus = Param.SerializingBus('serializing cache coherence bus')
    cache_id = Param.Int(0, 'unique id of private cache in system')


class SerializingBus(SimObject):
    type = 'SerializingBus'
    cxx_header = 'src_740/serializing_bus.hh'
    cxx_class = 'gem5::SerializingBus'

    mem_side = RequestPort('Mem side port, talks to memory')


class MiCache(CoherentCacheBase):
    type = 'MiCache'
    cxx_header = 'src_740/mi_cache.hh'
    cxx_class = 'gem5::MiCache'


class MsiCache(CoherentCacheBase):
    type = 'MsiCache'
    cxx_header = 'src_740/msi_cache.hh'
    cxx_class = 'gem5::MsiCache'


class MesiCache(CoherentCacheBase):
    type = 'MesiCache'
    cxx_header = 'src_740/mesi_cache.hh'
    cxx_class = 'gem5::MesiCache'

    blockOffset = Param.Int(5, 'number of bits for blockOffset')
    setBit = Param.Int(4, 'number of bits for cache set')
    cacheSizeBit = Param.Int(15, 'number of bits for cache size')

class DragonCache(CoherentCacheBase):
    type = 'DragonCache'
    cxx_header = 'src_740/dragon_cache.hh'
    cxx_class = 'gem5::DragonCache'

    blockOffset = Param.Int(5, 'number of bits for blockOffset')
    setBit = Param.Int(4, 'number of bits for cache set')
    cacheSizeBit = Param.Int(15, 'number of bits for cache size')

class HybridCache(CoherentCacheBase):
    type = 'HybridCache'
    cxx_header = 'src_740/hybrid_cache.hh'
    cxx_class = 'gem5::HybridCache'

    blockOffset = Param.Int(5, 'number of bits for blockOffset')
    setBit = Param.Int(4, 'number of bits for cache set')
    cacheSizeBit = Param.Int(15, 'number of bits for cache size')
    invalidThreshold = Param.Int(5, 'initial value of invalid threshold')