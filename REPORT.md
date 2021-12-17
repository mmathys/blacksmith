# Report

## Week 7

This week, we researched hammering with SIMD using Blacksmith. We first tried hammering in one row only using the intrinsic `mm256_loadu_pd`. We used the unaligned version because we the loading address is not aligned. Then, we tried hammering four rows in parallel using `_mm256_i64gather_pd`. This is a gather instruction which takes a base and multiple 64-bit pointer offsets. Finally we tried `_mm256_i32gather_ps` which takes eight 32-bit offsets instead. We benchmarked how many ACTs fit into a refresh interval in `DramAnalyzer.cpp`. The results are below.

> 3 pts: Figure out how many ACTs fit into an refresh interval using SIMD instructions

- simple `(void)*a;`: 198 ACTs          (1 bank)
- `_mm256_load_ps`: 194 ACTs            (1 bank)
- `_mm256_loadu_ps`: 196 ACTs           (1 bank)
- `_mm256_i64gather_ps`: 175 * 4 ACTs   (4 banks in parallel)
- `_mm256_i32gather_ps`: 137 * 8 ACTS   (8 banks in parallel)


Even though the gather instructions are slower, the throughput is much higher. We will investigate whether this will lead to a greater amount of bitflips overall.

> 3 pts: Modify Blacksmith's asmjit to emit SIMD instructions

We implemented the `_mm256_i64gather_ps` intrisnic using the assembly instructions `vgatherdpd` for gathering and `vpinsrd` for setting the offsets. We were even able to get bitflips with this when we set the offsets equal to zero (so that only one bank was hammered).

Our next steps are:
- Given an aggressor address a, we want to compute a', a'', a''' which are addresses in the **same row and column** as a, but in **different banks**. Then we execute the gather instruction for a, a', a'', a''' which has the effect that four rows are hammered in parallel.
- Implement the victim checking function so that it checks a', a'', a''' as well.

In our opinion, he hardest parts were:
- figuring out how to compile to a specific microarchitecture so that SIMD instructions are actually used with `-march=skylake` (instead of `-march=native`).
- writing the result of the SIMD read to a volatile variable so that the loads aren't optimized away.
- The translation of the tranlsation of the `_mm256_i64gather_ps` intrinsic into assembly. We had to use a disassembler to inspect the assembly and reproduce it in `CodeJitter.cpp`. This was tricky.

## Week 8

> Fixes from last week

  - We now correctly counted the ACTs and counted the double of `count` per refresh interval. As one can see in the table in section "Week 7" above we now get much more realistic number of activations. Especially for the version where 8 banks are hammered in parallel.
  - We switched the intrinsic functions from `_mm256_i[32|64]gather_epi32` to `_mm256_i[32|64]gather_ps`. With the i32gather instruction we were able to hammer 8 banks in parallel and reach a high number of ACTs as can be seen in the table in "Week 7" above.
  - In the activation counter we fixed our flushes and accesses to always loop through all elements in the vector, with 4 and also 8 elements.
  - In the SIMD part in `CodeJitter` we found that using the `vpinsrq` instruction leads to bitflips instead of using the  `mov` instruction for filling the vector registers. We are still unsure why that is exactly – maybe the mov instruction was not designed to move data from registers to YMM registers and failed silently; while `vpinsrq` worked.
  - Fixed more minor issues from week 7 and benchmarked our code (see `benchmarks/`).

> New features

  We modified the bitflip checking logic to check for multiple banks as well in `Memory.cpp`.

> Submission

  We submit two variants of our code:

  1. SIMD variant (see GitHub Link)
  2. Scalar access variant (this submission)

  In the **1. SIMD variant**, we succeeded in implementing a parallel version of Blacksmith. Before each hammer, we set the take the aggressor row a, then add an offset of 1, 2, 3 banks. This results in four aggressor rows a, a', a'', a'''. See the assembly part here: https://github.com/mmathys/blacksmith/blob/submission-simd/src/Fuzzer/CodeJitter.cpp#L197-L213 
  Unfortunately, this version does not really trigger bitflips – we were only able to see one single bitflip with this version.
  We conclude that our SIMD implementation in assembly is presumably slow because before each hammer, we have to manually build the offset vector register with `mov` and/or `vpinsrq` instructions. These instructions are likely quite slow and blocking, thus we lose a lot of speed there.

  In the **2. Scalar access variant** (this submission), we resorted to using plain mov instructions instead, which do not use any AVX/AVX2 extension. See lines L198-L205 in `CodeJitter.cpp`. This version send scalar accesses to rows a..a''', which are parallelized by the CPU. This yielded much better results, see the next section.

> 3 pts: Run the SIMD Blacksmith on three DIMMs
> 1 pts: Plot and write a short report

  We benchmarked our SIMD implementation in `CodeJitter` on cn006, cn002 and cn005 and got the following results. We benchmarked the scalar access variant on nodes cn002, cn005, cn006. Other nodes were also considered but not available at the time.
  
  We show the results of a sweep over 1MB of DRAM. "4-way" indicates that we hammered 4 banks in parallel with variant 2.

  Node   | Code   | Total bitflips of sweep
  =========================================
  cn006    1-way    133
  cn006    4-way    317
  cn002    4-way    2
  cn005    4-way    0

  On node 6, we tested both the original Blacksmith code ("1-way") and the scalar access variant ("4-way"). On nodes 2 and 5 we only tested the scalar access variant ("4-way").

  As for the node cn002 and cn005, our 4-way approach was not quite successful. The reason could be one or more of the following things:

  - the fuzzing time was too short and no effective pattern was found in the given time frame
  - the sweeping area was too small and no vulnerable area was found
  - nodes are more sensitive to synchronization

> Future Work

  As we have seen the ACTs for 8 banks parallel hammering are very promising. The translation of the intrinsics functions to jitting assembly code was challenging and hard to debug. We expect with more time, knowledge and debugging the SIMD vgather hammering instructions in `CodeJitter` can be done much more efficiently.

  A way to improve the SIMD variant: in the AVX2 extension there are 32 YMM registers in total, or 64 XMM registers. If we want to access up to 8 banks in parallel, YMM registers must be used for vgather. For 4 banks in parallel, XMM registers can be used. To avoid manually storing the vgather offsets in a new register before each access, we could assign an aggressor address to a register and store them once before the hammering process. This would allow us to just perform vgather instruction and not manually craft the offset vector before each access. However, this would limit the number of aggressors of a pattern to (32 - 2) or (62 - 2) rows; 2 registers must be reserved for the vgather instruction itself and cannot be reused as offset register (result and mask vector).

> Conclusion

  We have shown that for node cn006 hammering multiple rows with the scalar access variant is actually beneficial to finding more bitflips in a given time span and leads to an increase in flips of ~2.3x. We also presented a SIMD version which was not fast enough to trigger bitflips. We presented the benefits, drawbacks and challenges of using vgather for row hammering.