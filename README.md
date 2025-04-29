# Adaptive Cache Coherence Protocol

This repository implements four cache coherence protocols (MESI, Dragon, Hybrid and Adapt) using the gem5 simulator, demonstrating how multicore systems maintain cache coherency.
## code structure
    /
    ├── work/
    │   └── cc/
    │       ├── src/
    │       │   ├── adapt_cache.cc
    │       │   ├── adapt_cache.hh
    │       │   ├── dragon_cache.cc
    │       │   ├── dragon_cache.hh
    │       │   ├── mesi_cache.cc
    │       │   └── mesi_cache.hh
    │       │   ├── hybrid_cache.cc
    │       │   └── hybrid_cache.hh
    │       ├── configs/
    │       │   └── coherentcache.py
    │       │   └── [other cache config files]
    │       ├── binaries/
    │       │   ├── tas_lock
    │       │   ├── producer_consumer
    │       │   └── [other test binaries]
    │       └── scripts/
    │           └── rebuild_gem5
    │           └── log files
    └── docs/

## Building and Testing

### Installation on ECE Cluster
#### ECE Cluster Setup

1. Log in to your ECE cluster machine to enter AFS
```
ssh AndrewID@eceXXX.ece.local.cmu.edu   # Replace XXX with your machine number (000-031)
```
2. Set up your workspace
```
cd private
mkdir workspace && cd workspace
```
3. Run the setup script
```
/afs/ece.cmu.edu/class/ece740/labs/lab3/scripts/setup_lab3
```
4. Set up the additional cache coherence files
```
/afs/ece.cmu.edu/class/ece740/labs/lab3/scripts/cc_setup
```

### Build gem5 with cache coherence protocols
```
cd work/cc
./rebuild_gem5
```

### Run tests 
```
./gem5.opt configs/<test_config>.py binaries/<binary_name>
```
if you want to print cache debug output use ```--debug-flag=CCache``` (will be print in terminal):
```
./gem5.opt --debug-flag=CCache configs/<test_config>.py binaries/<binary_name>
```

### Switching Protocols

Edit ```work/cc/configs/<test_config>.py``` to use the desired cache type (take MESI and Dragon as Example):

#### For Dragon protocol
```
system.l1_caches = [DragonCache(options) for i in range(options.num_cpus)]
```
#### For MESI protocol
```
system.l1_caches = [MesiCache(options) for i in range(options.num_cpus)]
```
Remember to rebuild gem5 after any C++ file changes.

## Performance Analysis

When analyzing protocol performance parameters, consider:
```
./gem5.opt --debug-flags=CCache configs/<test_config>.py binaries/<binary_name> > <log_name>.log
```
to output the log files ```<log_name>.log``` and see all the debug prints to find out data for: Total Bus transaction,	BusRdX,	BusRd,	BusUpd,	Rd Data,	Update Data

