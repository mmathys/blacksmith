# Report

## Week 7

Some notes:
- compiling to a specific microarchitecture so that SIMD instructions are actually used with `-march=skylake` (instead of `-march=native`). 
- writing the result of the SIMD read to a volatile variable so that the loads aren't optimized away.

> 3 pts: Figure out how many ACTs fit into an refresh interval using SIMD instructions

- simple `(void)*a;`: 242 ACTs      (1 bank)
- `_mm256_load_ps`: 236 ACTs        (1 bank)
- `_mm256_loadu_ps`: 237 ACTs       (1 bank)
- `_mm256_i64gather_ps`: 182 ACTs   (4 banks in parallel)
- `_mm256_i32gather_ps`: 137 ACTS   (8 banks in parallel)

> 3 pts: Modify Blacksmith's asmjit to emit SIMD instructions

TODO
- [ ] find out how to execute the SIMD gather instruction with AsmJit