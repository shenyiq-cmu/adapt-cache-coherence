import m5
from m5.objects import *
import sys

# Create system
system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '1GHz'
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('512MB')]

# Set number of cores
N = 4  # Using 4 cores to really stress the lock

system.cpu = [TimingSimpleCPU(cpu_id=i) for i in range(N)]

# Configure caches with 1024 byte size
# cacheSizeBit=10 gives 2^10 = 1024 bytes
system.serializing_bus = SerializingBus()
system.cc = [MesiCache(cache_id=i, 
                      serializing_bus=system.serializing_bus,
                      blockOffset=2,   # 4-byte blocks
                      setBit=4,        # 16 sets
                      cacheSizeBit=10) # 1024 bytes total
             for i in range(N)]

system.membus = SystemXBar()
system.serializing_bus.mem_side = system.membus.cpu_side_ports

# Connect CPUs to caches and memory
for i in range(N):
    system.cpu[i].icache_port = system.membus.cpu_side_ports
    system.cpu[i].dcache_port = system.cc[i].cpu_side
    system.cpu[i].createInterruptController()

system.system_port = system.membus.cpu_side_ports

# Set up memory controller
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# Get binary path
binary = sys.argv[1]

system.workload = SEWorkload.init_compatible(binary)

# Create processes for all cores
processes = [Process(pid=i*100) for i in range(N)]
for i in range(N):
    processes[i].cmd = [binary, str(i)]
    system.cpu[i].workload = processes[i]
    system.cpu[i].createThreads()

root = Root(full_system=False, system=system)
m5.instantiate()

# Map shared memory region
for i in range(N):
    processes[i].map(4096*8, 4096*8, 4096*4, cacheable=True)

print("Beginning simulation for ticket lock test!")
exit_event = m5.simulate()

print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")

# Analyze results
if hasattr(system.serializing_bus, 'getTotalBusTransactions'):
    print("\n=== Bus Transaction Statistics ===")
    print(f"Total Bus Transactions: {system.serializing_bus.getTotalBusTransactions()}")
    print(f"Bus Read Transactions: {system.serializing_bus.getBusReadCount()}")
    print(f"Bus Write Transactions: {system.serializing_bus.getBusWriteCount()}")
    if hasattr(system.serializing_bus, 'getBusRdXCount'):
        print(f"Bus Read-for-Ownership (RdX): {system.serializing_bus.getBusRdXCount()}")

# Print cache statistics
for i in range(N):
    cache = system.cc[i]
    print(f"\n=== Cache {i} Statistics ===")
    if hasattr(cache, 'hits') and hasattr(cache, 'misses'):
        print(f"Hits: {cache.hits}")
        print(f"Misses: {cache.misses}")
        hit_rate = cache.hits/(cache.hits+cache.misses) if (cache.hits+cache.misses) > 0 else 0
        print(f"Hit Rate: {hit_rate:.2%}")
    if hasattr(cache, 'evictions'):
        print(f"Evictions: {cache.evictions}")
    if hasattr(cache, 'invalidations'):
        print(f"Invalidations: {cache.invalidations}")