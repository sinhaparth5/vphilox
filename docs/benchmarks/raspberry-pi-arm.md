# Raspberry Pi ARM benchmark guide

Use this procedure to collect the native AArch64 result required by Phase 1.
The run compares scalar Philox with `std::mt19937`, checks correctness, records
cycles per byte, and saves enough machine information to review the result.

Use a 64-bit Raspberry Pi OS or Ubuntu installation. Active cooling and a
stable power supply are required. Do not overclock the board.

## Prepare the repository

Install the build tools and libpfm:

```bash
sudo apt update
sudo apt install -y git cmake ninja-build g++ libpfm4-dev
```

Clone the repository. If the benchmark work has not reached the default branch
yet, replace `master` with the branch containing it.

```bash
git clone --branch master https://github.com/sinhaparth5/vphilox.git
cd vphilox
```

Confirm that the operating system is 64-bit ARM:

```bash
uname -m
```

The result must be `aarch64`. An `armv7l` result means the Pi is running a
32-bit OS and cannot provide the required AArch64 measurement.

## Check correctness first

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Do not benchmark a build with failing tests.

## Stabilize and record the machine

Close other programs and leave the desktop idle. Save the current CPU governor,
then select the performance governor for the run:

```bash
benchmark_previous_governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)

for governor_file in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
  echo performance | sudo tee "$governor_file"
done
```

Record the environment before benchmarking:

```bash
mkdir -p results

{
  date -Iseconds
  uname -a
  lscpu
  g++ --version
  cmake --version
  vcgencmd measure_temp
  vcgencmd measure_clock arm
  vcgencmd get_throttled
  cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
} | tee results/pi-arm-environment.txt
```

## Build and measure

Enable libpfm so Google Benchmark can read the ARM hardware cycle counter:

```bash
cmake --preset bench -DBENCHMARK_ENABLE_LIBPFM=ON
cmake --build --preset bench
```

Pin the benchmark to the last CPU core and collect seven randomly interleaved
repetitions:

```bash
benchmark_cpu=$(( $(nproc) - 1 ))

taskset --cpu-list "$benchmark_cpu" \
  ./build/bench/benchmarks/bench_engines \
  --benchmark_perf_counters=CYCLES \
  --benchmark_min_time=1s \
  --benchmark_repetitions=7 \
  --benchmark_enable_random_interleaving=true \
  --benchmark_report_aggregates_only=true \
  --benchmark_counters_tabular=true \
  --benchmark_context=machine=raspberry-pi,affinity="$benchmark_cpu" \
  --benchmark_out=results/pi-arm-baseline.json \
  --benchmark_out_format=json
```

The output should contain both `bytes_per_second` and `cycles_per_byte`. If the
performance counter cannot be opened, record the error and check
`/proc/sys/kernel/perf_event_paranoid` before changing system security settings.

## Check the run and restore the Pi

Immediately check temperature, clock, and throttling state:

```bash
vcgencmd measure_temp
vcgencmd measure_clock arm
vcgencmd get_throttled
```

`get_throttled=0x0` is the clean result. Any undervoltage or throttling flag
invalidates the performance run. Improve power or cooling and repeat it.

Restore the original governor:

```bash
for governor_file in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
  echo "$benchmark_previous_governor" | sudo tee "$governor_file"
done
```

Keep these two artifacts:

```text
results/pi-arm-environment.txt
results/pi-arm-baseline.json
```

Archive reviewed results under `docs/benchmarks/raw/`. Report the median and
coefficient of variation for every benchmark. Compare
`BM_philox_scalar_median` with `BM_mt19937_median`; do not use mean throughput
when a noisy sample skews the run.

## References

- [Raspberry Pi clock and thermal controls](https://www.raspberrypi.com/documentation/computers/config_txt.html)
- [Google Benchmark performance counters](https://github.com/google/benchmark/blob/main/docs/perf_counters.md)
