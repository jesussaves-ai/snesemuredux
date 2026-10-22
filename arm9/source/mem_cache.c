/*
 * Precise, range-restricted ARM9 cache maintenance + conditional SMC
 * invalidation.  See mem_cache.h.
 *
 * Only compiled for ARM9 (the file lives under arm9/source).  The bookkeeping
 * logic is covered by a host unit test that compiles this file with
 * -DMEM_CACHE_TEST_EXTERNAL_FLUSH and supplies a recording flush.
 */

#include "typedefsTGDS.h"

#include "mem_cache.h"

#ifdef ARM9
#include "nds_cp15_misc.h"
#include "utilsTGDS.h"
#endif

#define MEM_CACHE_LINE 32U
#define MEM_BLOCK_SIZE 8192U

/* ---- range maintenance -------------------------------------------------- */

/* The definition is skipped only for the host-side cache/SMC unit test
 * (MEM_CACHE_TEST_EXTERNAL_FLUSH), which provides its own recording
 * implementation to observe the exact ranges requested. */
#ifndef MEM_CACHE_TEST_EXTERNAL_FLUSH
void mem_cache_flush_range(void *addr, uint32 size) {
    if (size == 0)
        return;
#ifdef ARM9
    /* SDK helper: cleans+invalidates the D range and invalidates the matching
     * I lines, then drains the write buffer.  It is already range scoped. */
    coherent_user_range_by_size((uint32)addr, (int)size);
#else
    (void)addr;
#endif
}
#endif

void mem_cache_invalidate_icache_range(void *addr, uint32 size) {
#ifdef ARM9
    uint32 a   = ((uint32)addr) & ~(MEM_CACHE_LINE - 1U);
    uint32 end =  (uint32)addr + size;
    if (size == 0)
        return;
    while (a < end) {
        /* ARM946E-S: c7, c5, 1 = Invalidate I-cache line by MVA. */
        asm volatile("mcr p15, 0, %0, c7, c5, 1" :: "r"(a) : "memory");
        a += MEM_CACHE_LINE;
    }
    asm volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(0) : "memory"); /* drain WB */
#else
    (void)addr; (void)size;
#endif
}

void mem_cache_flush_all(void) {
#ifdef ARM9
    flush_icache_all();
    flush_dcache_all();
    asm volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(0) : "memory");
#endif
}

/* ---- conditional SMC tracking ------------------------------------------- */

/* One dirty flag per SNES 8 KB map block. 0x800 blocks = 2 KB of state. */
static unsigned char mem_smc_dirty[0x800];
static int mem_smc_dirty_n = 0;

void mem_smc_clear(void) {
    int i;
    for (i = 0; i < 0x800; i++)
        mem_smc_dirty[i] = 0;
    mem_smc_dirty_n = 0;
}

void mem_smc_mark_dirty(uint32 byteAddr) {
    int block = (int)((byteAddr >> 13) & 0x7FF);
    if (!mem_smc_dirty[block]) {
        mem_smc_dirty[block] = 1;
        mem_smc_dirty_n++;
    }
}

void mem_smc_flush_block(int block, void *blockBase) {
    if (block < 0 || block >= 0x800)
        return;
    if (!mem_smc_dirty[block])
        return;
    /* The block's data lives contiguously in cached memory: flush exactly the
     * 8 KB window that was modified. */
    mem_cache_flush_range(blockBase, MEM_BLOCK_SIZE);
    mem_smc_dirty[block] = 0;
    if (mem_smc_dirty_n)
        mem_smc_dirty_n--;
}

int mem_smc_dirty_count(void) {
    return mem_smc_dirty_n;
}
