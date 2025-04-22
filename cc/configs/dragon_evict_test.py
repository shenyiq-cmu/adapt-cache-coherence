import m5
from m5.objects import *
import sys

system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '1GHz'
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('512MB')]

N = 2

system.cpu = [TimingSimpleCPU(cpu_id=i) for i in range(N)]

# Configure the serializing bus
system.serializing_bus = SerializingBus()

# Configure caches with VERY small size to force evictions
# blockOffset=5 (32-byte blocks), setBit=1 (2 sets), cacheSizeBit=8 (256 bytes total)
# This tiny cache will have only 2 sets × 4 ways = 8 total cache lines
system.cc = [DragonCache(cache_id=i, serializing_bus=system.serializing_bus, 
                      blockOffset=5, setBit=1, cacheSizeBit=8) for i in range(N)]

# Configure memory hierarchy
system.membus = SystemXBar()
system.serializing_bus.mem_side = system.membus.cpu_side_ports

for i in range(N):
    system.cpu[i].icache_port = system.membus.cpu_side_ports
    system.cpu[i].dcache_port = system.cc[i].cpu_side
    system.cpu[i].createInterruptController()

system.system_port = system.membus.cpu_side_ports

# Configure memory controller
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# Get the test binary from command line
binary = sys.argv[1]

system.workload = SEWorkload.init_compatible(binary)

# Create processes for each CPU
processes = [Process(pid=i*100) for i in range(N)]
for i in range(N):
    processes[i].cmd = [binary, str(i)]
    system.cpu[i].workload = processes[i]
    system.cpu[i].createThreads()

# Create the root object
root = Root(full_system=False, system=system)
m5.instantiate()

# Map shared memory region at 0x8000
for i in range(N):
    processes[i].map(4096*8, 4096*8, 4096, cacheable=True)

print("Beginning eviction test simulation!")
exit_event = m5.simulate()

print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")