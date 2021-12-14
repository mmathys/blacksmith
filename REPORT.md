# Report

## Week 7

Some notes:
- compiling to a specific microarchitecture so that SIMD instructions are actually used with `-march=skylake` (instead of `-march=native`). 
- writing the result of the SIMD read to a volatile variable so that the loads aren't optimized away.

> 3 pts: Figure out how many ACTs fit into an refresh interval using SIMD instructions

- simple `(void)*a;`: 242 ACTs
- `_mm256_load_ps`: 236 ACTs
- `_mm256_loadu_ps`: 237 ACTs
- `_mm256_i64gather_ps`: 164 ACTs

> 3 pts: Modify Blacksmith's asmjit to emit SIMD instructions

## Week 8

TODO: save aggressors into AVX (sixteen YMM) -> sixteen aggressor rows

TODO
- [x] find out how to execute the SIMD gather instruction with AsmJit
- [ ] update report
  - [ ] run benchmarks again with fixed code
  - [ ] add testing of i32 stuff
- [ ] find out how to benchmark execution: fuzzer saves the best pattern (?); benchmark this pattern (for 4x parallelization)
  - [ ] save fuzzer run (do not pass sweep)
  - [ ] use --load-json and --sweeping
- [ ] do this on DRAM 2, 3, 6.
- [ ] benchmark different levels of parallelization on node 6 (fuzzer + sweep in one go)
- [ ] prepare report
  - [ ] explain why vgather didn't work out as well as simple pipelined accesses.
- [ ] prepare short presentation
