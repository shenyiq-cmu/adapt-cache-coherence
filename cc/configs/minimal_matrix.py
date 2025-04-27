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

# Number of CPU cores
N = 2

# Create CPU cores
system.cpu = [TimingSimpleCPU(cpu_id=i) for i in range(N)]

# Create the Dragon cache coherence components
system.serializing_bus = SerializingBus()

# Configure Dragon caches with very simple parameters
system.dragon_cache = [DragonCache(
    cache_id=i, 
    serializing_bus=system.serializing_bus, 
    blockOffset=3,  # 8-byte blocks
    setBit=2,       # 4 sets
    cacheSizeBit=10 # 1KB cache
) for i in range(N)]

# Create the memory bus
system.membus = SystemXBar()
system.serializing_bus.mem_side = system.membus.cpu_side_ports

# Connect CPU and caches
for i in range(N):
    system.cpu[i].icache_port = system.membus.cpu_side_ports
    system.cpu[i].dcache_port = system.dragon_cache[i].cpu_side
    system.cpu[i].createInterruptController()

# Connect system port
system.system_port = system.membus.cpu_side_ports

# Set up memory controller
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# Load the binary from command line argument
binary = sys.argv[1]

# Set up workload
system.workload = SEWorkload.init_compatible(binary)

# Create processes for each CPU
processes = [Process(pid=i*100) for i in range(N)]
for i in range(N):
    processes[i].cmd = [binary, str(i)]
    system.cpu[i].workload = processes[i]
    system.cpu[i].createThreads()

# Instantiate the system
root = Root(full_system=False, system=system)
m5.instantiate()

# Map shared memory region (4KB is enough for the minimal test)
for i in range(N):
    processes[i].map(4096*8, 4096*8, 4096, cacheable=True)

# Reset stats before simulation
m5.stats.reset()

print("Beginning simulation of Dragon protocol minimal benchmark!")

# Limit simulation time to avoid potential hangs
tick_limit = 10000000000  # 10 billion ticks (10 seconds at 1GHz)
exit_event = m5.simulate(tick_limit)

print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")

