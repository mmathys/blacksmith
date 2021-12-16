# Report

## Week 7

This week, we researched hammering with SIMD using Blacksmith. We first tried hammering in one row only using the intrinsic `mm256_loadu_pd`. We used the unaligned version because we the loading address is not aligned. Then, we tried hammering four rows in parallel using `_mm256_i64gather_pd`. This is a gather instruction which takes a base and multiple 64-bit pointer offsets. Finally we tried `_mm256_i32gather_ps` which takes eight 32-bit offsets instead. We benchmarked how many ACTs fit into a refresh interval in `DramAnalyzer.cpp`. The results are below.

> 3 pts: Figure out how many ACTs fit into an refresh interval using SIMD instructions

- simple `(void)*a;`: 198 ACTs      (1 bank)
- `_mm256_load_ps`: 194 ACTs        (1 bank)
- `_mm256_loadu_ps`: 196 ACTs       (1 bank)
- `_mm256_i64gather_ps`: 175*4 ACTs   (4 banks in parallel)
- `_mm256_i32gather_ps`: 137*4 ACTS   (8 banks in parallel)


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

> 3 pts: Run the SIMD Blacksmith on three DIMMs
  We benchmarked our SIMD implementation in `CodeJitter` on cn006, cn002 and cn005 and got the following results.
  The 4-way indicates that we hammered 4 banks in parallel.

Node   | Code   | Total bitflips of sweep
=========================================
cn006    1-way    133
cn006    4-way    317
cn002    4-way    2
cn005    4-way    0

> Fixes from last week
  We now correctly counted the ACTs and counted the double of `count` per refresh interval. As one can see
  in the above results we now get much more realistic number of activations. Especially for the 8 banks
  in parallel version we have. In a further step we now collect addresses into a, a', ..., a''' and
  sort them in ascending order, because the addresses are from random banks.
  Then we switched the intrinsic functions from `_mm256_i[32|64]gather_epi32` to `_mm256_i[32|64]gather_ps`. With the
  i32gather instruction we were able to hammer 8 banks in parallel and reach a high number of ACTs as can be seen above.
  Also we fixed our flushing and accesses to always loop through all elements in the vector, with 4 and also 8 elements.
  In the SIMD part in `CodeJitter` we found that using the `vpinsrq` instruction leads to bitflips instead of using the 
  `mov` instruction for filling the vector registers.
  Additionally this week, we fixed some issues from week 7 and benchmarked our code (see `benchmarks/`). We first modified 
  the bitflip checking logic to check for multiple banks as well in `Memory.cpp`. We submit two variants.

1. SIMD variant (on GitHub)
2. Scalar access variant (this submission)

In the **SIMD variant**, we succeeded in implementing a parallel version of Blacksmith. Before each hammer, we set the take the aggressor row a, then add an offset of 1, 2, 3 banks. This results in four aggressor rows a, a', a'', a'''. See the assembly part here:
- https://github.com/mmathys/blacksmith/blob/submission-simd/src/Fuzzer/CodeJitter.cpp#L197-L213
Unfortunately, this version does not really trigger bitflips – we were only able to see one single bitflip with this version.
We conclude that our SIMD implementation in Assembly is probably slow because the `mov` and `vpinsrq` instructions are quite slow
and blocking, thus we loose a lot of speed and fast accesses there. In our version they have to be done on each access.
Storing the offsets bottlenecks our parrallel execution.

> Future Work
  As we have seen the ACTs for 8 banks parallel hammering are very promising. We struggled a lot with the translation of
  the intrinsics functions to assembly code. We expect with more time and knowledge the ACTs benefit could be moved to the
  `CodeJitter` and hammering can be done much more efficiently.
  As a next step we know that we have 32 registers, only two of them are needed for the mask and the result, thus we could
  run 30 aggressors for even more parallel hammering.

We tried out a different version with **scalar accesses**, which is this submission. See lines L198-L205 in `CodeJitter.cpp`. This version send scalar accesses to rows a..a'''. This yielded much better results, see the next section.

### Benchmarks of Week 8

We benchmarked the scalar access variant on nodes cn002, cn005, cn006. We tried other nodes as well but the fuzzer didn't threw an error or got stuck.

We show the results of a sweep over 1MB of DRAM (instead of a plot we list the following table):

Node   | Code   | Total bitflips of sweep
=========================================
cn006    1-way    133
cn006    4-way    317
cn002    4-way    2
cn005    4-way    0

On node 6, we tested both the original Blacksmith code ("1-way") and the scalar access variant ("4-way"). On nodes 2 and 5 we only tested the scalar access variant ("4-way").

We conclude that hammering multiple rows with the scalar access variant is actually beneficial to finding more bitflips in a given time span! 