# Report

## Week 7

Some notes:
- compiling to a specific microarchitecture so that SIMD instructions are actually used with `-march=skylake` (instead of `-march=native`). 
- writing the result of the SIMD read to a volatile variable so that the loads aren't optimized away.

> 3 pts: Figure out how many ACTs fit into an refresh interval using SIMD instructions

- simple `(void)*a;`: 242 ACTs
- `_mm256_load_ps`: 236 ACTs
- `_mm256_loadu_ps`: 237 ACTs
- `_mm256_i64gather_ps`: 196 ACTs

> 3 pts: Modify Blacksmith's asmjit to emit SIMD instructions

TODO
- [ ] find out how to execute the SIMD gather instruction with AsmJit