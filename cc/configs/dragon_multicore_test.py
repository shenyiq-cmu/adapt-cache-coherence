# configs/dragon_simple_multicore.py
import m5
from m5.objects import *
import sys

# Get command line arguments
if len(sys.argv) < 2:
    print("Usage: python3 configs/dragon_simple_multicore.py <binary> [num_cores]")
    sys.exit(1)

binary = sys.argv[1]
num_cores = int(sys.argv[2]) if len(sys.argv) > 2 else 4  # Default to 4 cores

system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '1GHz'
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('512MB')]

# Create CPUs
system.cpu = [TimingSimpleCPU(cpu_id=i) for i in range(num_cores)]

# Configure the serializing bus
system.serializing_bus = SerializingBus()

# Configure caches for multicore testing
# blockOffset=5 (32-byte blocks), setBit=3 (8 sets), cacheSizeBit=11 (2KB total)
system.cc = [DragonCache(cache_id=i, serializing_bus=system.serializing_bus, 
                     blockOffset=5, setBit=3, cacheSizeBit=11) for i in range(num_cores)]

# Configure memory hierarchy
system.membus = SystemXBar()
system.serializing_bus.mem_side = system.membus.cpu_side_ports

for i in range(num_cores):
    system.cpu[i].icache_port = system.membus.cpu_side_ports
    system.cpu[i].dcache_port = system.cc[i].cpu_side
    system.cpu[i].createInterruptController()

system.system_port = system.membus.cpu_side_ports

# Configure memory controller
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

system.workload = SEWorkload.init_compatible(binary)

# Create processes for each CPU
processes = [Process(pid=i*100) for i in range(num_cores)]
for i in range(num_cores):
    processes[i].cmd = [binary, str(i), str(num_cores)]
    system.cpu[i].workload = processes[i]
    system.cpu[i].createThreads()

# Create the root object
root = Root(full_system=False, system=system)
m5.instantiate()

# Map shared memory region at 0x8000
for i in range(num_cores):
    processes[i].map(4096*8, 4096*8, 4096, cacheable=True)

print(f"Beginning {num_cores}-core simulation!")
exit_event = m5.simulate()

print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")