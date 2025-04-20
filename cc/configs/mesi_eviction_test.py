# configs/mesi_eviction_test.py
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

system.serializing_bus = SerializingBus()
# Small cache to force evictions (32-byte blocks, 4 sets, 512B total)
system.cc = [MesiCache(cache_id=i, serializing_bus=system.serializing_bus, blockOffset=5, setBit=2, cacheSizeBit=10) for i in range(N)]

system.membus = SystemXBar()
system.serializing_bus.mem_side = system.membus.cpu_side_ports

for i in range(N):
    system.cpu[i].icache_port = system.membus.cpu_side_ports
    system.cpu[i].dcache_port = system.cc[i].cpu_side
    system.cpu[i].createInterruptController()

system.system_port = system.membus.cpu_side_ports

system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

binary = sys.argv[1]

system.workload = SEWorkload.init_compatible(binary)

processes = [Process(pid=i*100) for i in range(N)]
for i in range(N):
    processes[i].cmd = [binary, str(i)]
    system.cpu[i].workload = processes[i]
    system.cpu[i].createThreads()

root = Root(full_system=False, system=system)
m5.instantiate()

# Map shared memory region
for i in range(N):
    processes[i].map(4096*8, 4096*8, 4096, cacheable=True)

print("Beginning simulation!")
exit_event = m5.simulate()

print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}")