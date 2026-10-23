# Agent protection QEMU microbenchmark

Same user image; 4 modes; median of independent boot medians. Guest time ticks at 10 MHz.

| Workload | Mode | ticks / operation | boot median range | vs baseline |
| --- | --- | ---: | ---: | ---: |
| query | baseline | 111.816 | 110.469–111.846 | +0.0% |
| query | enforce | 129.980 | 128.516–131.055 | +16.2% |
| query | audit | 140.264 | 139.307–141.348 | +25.4% |
| query | filtered | 115.596 | 115.059–115.703 | +3.4% |
| map-cycle | baseline | 741.875 | 738.125–751.094 | +0.0% |
| map-cycle | enforce | 820.156 | 810.156–821.641 | +10.6% |
| map-cycle | audit | 815.234 | 810.781–829.766 | +9.9% |
| map-cycle | filtered | 773.438 | 770.625–780.156 | +4.3% |

query = one brk query; map-cycle = one mmap+munmap pair. Each sample is a batch mean.
TCG and host noise limit interpretation; these are not physical-hardware or real-Agent results.
Raw samples, configuration, source hash and logs are in results.json and the adjacent .log files.
