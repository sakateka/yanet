## CPU
```text
Model name:             13th Gen Intel(R) Core(TM) i7-13700H
Caches (sum of all):
  L1d:                    544 KiB (14 instances)
  L1i:                    704 KiB (14 instances)
  L2:                     11.5 MiB (8 instances)
  L3:                     24 MiB (1 instance)
```

## Results

```bash
Configuration:
  Threads: 8
  Total values: 4.2M
  Value size: 64 bytes
  Total data size: 256 MB (8x L3 cache)


=== chain_spinlock_t Multi-threaded ===
  Hashtable key slots: 262.1K

+ Write Phase Results +
Total write time(with joins): 2.770 seconds
Elapsed write time: 22.157 seconds
Total write operations: 335.5M
Successful writes: 335.5M
Write throughput: 15.1M ops/sec
Write success rate: 335544320/335544320

Hashtable statistics after writes:
  Total pairs: 4.2M
  Extended chunks count: 0
  Longest chain: 4
  Insert failed: 0
+ Read Phase Results +
Wall read time: 1.935 seconds
Elapsed read CPU time (sum): 14.171 seconds
Total read operations: 335.5M
Read checksum: 8796537618432
Successful reads: 335.5M
Read throughput: 23.7M ops/sec
Read success rate: 335544320/335544320


=== mod_spinlock Single-threaded ===
Required memory size: 306.2M bytes
  Hashtable pairs: 4.2M

+ Write Phase Results +
Total write time: 0.281 seconds
Write checksum: 9007.2T
Successful writes: 4.2M
Write throughput: 14.9M ops/sec
Write success rate: 4194304/4194304

+ Read Phase Results +
Total read time: 0.156 seconds
Read checksum: 8796415983536
Successful reads: 4.2M
Read throughput: 26.9M ops/sec
Read success rate: 4194304/4194304


=== mod_spinlock Multi-threaded ===
  Hashtable pairs: 4.2M

+ Write Phase Results +
Wall write time(with joins): 3.035 seconds
Elapsed write CPU time (sum): 22.014 seconds
Write checksum: 8.8T
Successful writes: 335.5M
Write throughput: 15.2M ops/sec
Write success rate: 335544320/335544320
+ Read Phase Results +
Wall read time: 2.230 seconds
Elapsed read CPU time (sum): 16.653 seconds
Total read operations: 335.5M
Read checksum: 8796936077312
Successful reads: 335.5M
Read throughput: 20.1M ops/sec
Read success rate: 335544320/335544320
  Time (mean ± σ):     10.385 s ±  0.264 s    [User: 73.487 s, System: 0.316 s]
  Range (min … max):    9.876 s … 10.729 s    10 runs
``` 
