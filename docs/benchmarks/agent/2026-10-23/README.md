# Agent protection QEMU microbenchmark

Same user image; 3 modes; median of independent boot medians. Guest time ticks at 10 MHz.

| Workload | Mode | ticks / operation | boot median range | vs baseline |
| --- | --- | ---: | ---: | ---: |
| query | baseline | 112.715 | 107.930–113.105 | +0.0% |
| query | enforce | 125.342 | 123.789–126.953 | +11.2% |
| query | audit | 137.891 | 137.773–139.639 | +22.3% |
| map-cycle | baseline | 748.984 | 747.969–751.016 | +0.0% |
| map-cycle | enforce | 800.859 | 795.469–802.031 | +6.9% |
| map-cycle | audit | 818.125 | 807.969–819.531 | +9.2% |

query = one brk query; map-cycle = one mmap+munmap pair. Each sample is a batch mean.
TCG and host noise limit interpretation; these are not physical-hardware or real-Agent results.
Raw samples, configuration, source hash and logs are in results.json and the adjacent .log files.
