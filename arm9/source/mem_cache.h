#ifndef __mem_cache_h__
#define __mem_cache_h__

#include "typedefsTGDS.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Precise, range-restricted ARM9 (ARM946E-S) cache maintenance.
 *
 * The NDS has a Harvard-style split cache with 32-byte lines.  Whole-cache
 * flushes (flush_icache_all/flush_dcache_all) are extremely expensive because
 * they throw away the entire warmed cache, so any hot path that needs
 * coherency - the 65816 paging/ROM-patch path and self-modifying code - must
 * operate only on the affected byte range.
 */

/* Clean+invalidate the D lines and invalidate the I lines covering
 * [addr, addr+size).  Also drains the write buffer so preceding stores are
 * visible.  Safe with size == 0. */
void mem_cache_flush_range(void *addr, uint32 size);

/* Invalidate only the I lines (no D clean) covering [addr, addr+size). */
void mem_cache_invalidate_icache_range(void *addr, uint32 size);

/* Whole-cache fallback, kept for the rare global-coherency case. */
void mem_cache_flush_all(void);

/*
 * Conditional self-modifying-code (SMC) tracking.
 *
 * Dirty state is kept per SNES 8 KB map block (0 .. 0x7FF), which is exactly
 * the granularity of the emulator's MAP[] page table.  A store marks the block
 * dirty; the block's range is flushed lazily, and only when something actually
 * executes/fetches from it, so a game that hammers WRAM does not pay for a
 * flush on every store.
 */
void mem_smc_mark_dirty(uint32 byteAddr);
void mem_smc_flush_block(int block, void *blockBase);
void mem_smc_clear(void);

/* Diagnostics / host tests. */
int  mem_smc_dirty_count(void);

#ifdef __cplusplus
}
#endif

#endif
