# DSA profiling

This repository contains a reproducible 64 KiB DSA MEMMOVE experiment that models
the data movement of a kernel-buffer-to-userspace `read()` copy.

## Quick start

Build and configure `dsa0` as one dedicated WQ (size 32) backed by four engines:

```bash
sudo ./scripts/run_copy_to_iter_bench.sh --configure
```

Rerun the benchmark without changing the device configuration:

```bash
./scripts/run_copy_to_iter_bench.sh
```

Optional controls:

```bash
CPU=0 ITERATIONS=20000 REPEATS=5 ./scripts/run_copy_to_iter_bench.sh
```

The script validates the active hardware configuration, sweeps QD 1 through 32,
repeats the QD32 test, and measures the effect of DSA destination cache-control.
It uses the `dsa-perf-micros` Git submodule.

An actual CPU `pread()` → `copy_page_to_iter()` baseline is also included:

```bash
make tools/cached_read_bw
./tools/cached_read_bw 5 512 0
```

The arguments are duration in seconds, page-cache source size in MiB, and CPU.
The source is a populated memfd, so storage I/O is excluded while the read syscall
and kernel-to-userspace copy remain in the timed path.

See [the host result and analysis](results/host-2026-09-14.md) for measured
bandwidth and the distinction between a pipelined DSA upper bound and synchronous
`read()` semantics.
