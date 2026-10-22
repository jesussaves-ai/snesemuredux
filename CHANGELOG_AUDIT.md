# SNEmulDS — Technical Audit Closure (Milestones 1–6)

Consolidated record of the engineering fixes, architectural changes and new
configuration options applied during the initial technical audit, before
hardware testing and release tagging.  Every item cites the file(s) it touches
and, where applicable, the audit identifier (`C7`, `C10/G5`, `G4`, `G7`,
`P5`, `P6`, `P7`).

> Build/flash note: the full `make` cannot be reproduced in this environment
> because the ToolchainGenericDS SDK is absent
> (`Makefile:31 /usr/arm-none-eabi/lib/newlib-nano-2.1-nds/6.2_2016q4/Makefile.basenewlib`).
> All changes were instead validated by host unit harnesses and by
> `arm-none-eabi-gcc -mcpu=arm946e-s` / `arm-none-eabi-ld` against the real
> linker scripts (see “Verification” below).

---

## Milestone 1–3 — Core correctness, hack DB, coprocessors, renderers

| Area | Change | Files |
|------|--------|-------|
| Game-specific hacks | Consolidated the scattered title-prefix special cases into a single keyed database (`s_game_hack`, `game_hacks[]`, `GameHack_Find()`) — TWL 16 MB, ROM offset fixes (DKC3/BOF), LoROM direct-ROM mapping, APU BRR cache/slowdown, CPU speedhack. | `arm9/source/snemulds_memmap.{c,h}` |
| Cheat engine | Game Genie / Pro Action Replay / raw code parser + applicability (`s_cheat`, `cheat_parse/load/apply`); codes read from the `[Cheats]` section; applied every VBlank so the assembly CPU core sees them. | `arm9/source/snemulds_memmap.{c,h}`, `arm9/source/engine.c` |
| Mode 0 | Dedicated 4×2bpp renderer (`renderMode0`, `PPU_RenderLineMode0`) behind `CFG.Mode0Fix` (Mode 0 has no valid DS fallback). | `arm9/source/ppu.c`, `arm9/include/cfg.h` |
| C4 coprocessor | C4 register/RAM emulation. | `arm9/source/c4.c`, `arm9/source/c4emu.c` |
| Mode 7 | Dirty tracking so map/tile copies happen only on VRAM writes while Mode 7 is active. | `arm9/source/ppu.c`, `arm9/include/gfx.h` (`GFX.mode7_dirty`, `mode7_last_slot`, `mode7_copied`) |

## Milestone 4 — Scaling, snapshots, streamlined UI

| Area | Change | Files |
|------|--------|-------|
| Scale filters | `PPU_scaledLine()` — exact inverse of the BG scroll-step model (`dest = u − floor(u/(per_px+1))`), used by both `draw_sprites` and `PPU_line_render_scaled`; removes the systematic 1-line sprite/background jitter. | `arm9/source/ppu.c` |
| Bi-linear toggle | New `CFG.Bilinear` gates the vertical-scale `0x40` dither line-flag. | `arm9/source/ppu.c`, `arm9/include/cfg.h`, `arm9/source/main.c`, `arm9/source/gui/gui_console_connector.c` |
| Snapshot v3 | v3 envelope (`TSnapShot_V3Header`, magic `SNE3`) + CRC32 integrity check performed **before** unpacking; payload now also carries the full sprite table and `GFX.lineInfo[]`. Legacy v1/v2 files still load via a magic probe. | `arm9/source/snapshot.c`, `arm9/include/snapshot.h` |
| Named presets | `LayerPresetNames[]` / `LayerPresetIndex()`; `LayerPreset` config string + structured `BG1Pr..BG4Pr`, `Spr1Pr..Spr4Pr` replace the raw octal `BGManualPriority`/`SpritePriority` blobs. `TilePriorityBG` surfaced in the Layers submenu. | `arm9/source/main.c`, `arm9/source/gui/gui_console_connector.c`, `arm9/include/console_str.h` |

## Milestone 5 — Audio de-clicking, SMC cache invalidation, release pipeline

| Area | Change | Files |
|------|--------|-------|
| Direct Sound de-click | New `dsp_declick.{c,h}`: discontinuity-triggered boundary cross-fade + bounded fade-to-silence; continuous material stays bit-exact. Level-triggered hardware refill in `Timer2handlerUser`. | `arm7/source/dsp_declick.{c,h}`, `arm7/source/interrupts/Interrupts_snes.c`, `arm7/source/dsp.c`, `arm7/source/main.c` |
| SMC cache invalidation | New `mem_cache.{c,h}`: range-restricted flush (`mem_cache_flush_range`, raw CP15 I-cache line loop), and a conditional per-8 KB-block SMC dirty tracker flushed lazily on fetch/control-transfer. Wired into `mem_setbyte/setword`, `map_memory`, `mem_getbaseaddress`, `cheat_apply`, and the HiROM/LoROM paging reload (the LoROM flush, previously commented out, is re-enabled). | `arm9/source/mem_cache.{c,h}`, `arm9/source/snemulds_memmap.c` |
| Release pipeline | `scripts/release.fish` + `.github/workflows/release.yml` + `make release`/`dist`; bundles binaries, banner metadata refresh, config templates, SHA256 manifest. ARM946E-S flags pinned; 4 MB EWRAM linker `ASSERT` added. | `scripts/`, `.github/workflows/release.yml`, `Makefile`, `arm9/Makefile`, `arm9/nds_arm9_ld.ld` |

## Milestone 6 — Remaining stability & hot-path sweep

| ID | Change | Files |
|----|--------|-------|
| **C7** | `UnInterleaveROM()`: allocate the 32 KB swap buffer **before** mutating state (clean `NULL`-malloc return that leaves ROM and `SNES.HiROM` untouched); guard the `step` loop against `ROMSize == 0` (former infinite loop); clamp `nblocks <= 128` (`blocks[256]` indexed up to `nblocks*2`). | `arm9/source/snes.c` |
| **C10 / G5** | Per-line **seqlock** for `GFX.lineInfo[]`: `PPU_line_render`/`PPU_line_render_scaled`/`PPU_line_handle_BG3` bump an odd/even sequence around the struct write (with a write-buffer drain), and `PPU_updateGFX` (HblankUser IRQ) skips any line mid-write. Eliminates the producer/consumer torn-read jitter. | `arm9/source/ppu.c` |
| **G4** | `draw_plane_withpriority()` tile loop bound fixed from `nb_tilex*nb_tiley` (n+1 iterations) to `nb_tilex*nb_tiley - 1` (exact count). | `arm9/source/ppu.c` |
| **G7** | `draw_sprites()`: the OAM entry struct is now cleared per sprite (was once before the loop, letting affine `rot_data` leak between sprites); the affine-matrix setup is only run when `CFG.Scaled != 0`; explicit zeroing of the PB/PC shear terms; documented that the matrix writes land in the unused attribute-3 halfwords and must follow the sprite-attribute loop. | `arm9/source/ppu.c` |
| **P5** | Removed the forced `optimize("O0")` from the DMA/HDMA hot path (`mem_getbaseaddress`, `map_memory`); added `-falign-functions=4 -falign-loops=4 -fno-tree-vectorize` for ARM946E-S I-cache efficiency. | `arm9/source/snemulds_memmap.c`, `arm9/Makefile` |
| **P7** | Expanded fast DMA: `PPU_fastDMA_VRAM_0` ($2118/$2119 fixed-register fills) and `PPU_fastDMA_2104` (OAM), byte-exact inline replicas of `W2118/W2119/W2104`, guarded by `GFX.SC_incr == 1`, no FS_incr, clean `map_memory` source, and no 64 KB bank crossing. | `arm9/source/core.c`, `arm9/include/core.h` |
| **P6** | `add_tile_8` measured at ~360–380 bytes (arm946e-s, -O2/-O3). Kept in **EWRAM with `-O3`** rather than `.itcm`: the 32 KB ITCM window already hosts 86 register handlers plus IRQ entry points, and its total footprint could not be re-measured here (SDK/map absent) so the required 1 KB safety margin cannot be guaranteed. | `arm9/source/ppu.c` |

### New / changed configuration options (all six milestones)

`Bilinear` (M4), `LayerPreset` + `BG1Pr..BG4Pr`/`Spr1Pr..Spr4Pr` (M4),
`TilePriorityBG` toggle in the Layers menu (M4), `Mode0Fix` (M1–3), plus the
existing `Scaled`, `YScroll`, `Sound`, `EnableSRAM`, `FastDMA`,
`Transparency`, `HDMA`, `BGLayers`, `BG3Squish`, `BG3TilePriority`,
`BlankTileNumber`, `Vblank`, `MapExtMem`, Mouse options.  Legacy octal
`BGManualPriority`/`SpritePriority` and numeric `BGPriorities` are still
**read** for backward compatibility but are no longer written.

---

## Verification

Host harnesses (all pass):

| Harness | Proves |
|---------|--------|
| `declick_test` | ARM7 de-click: continuous material bit-exact; a 20000→0 step is spread to max\|Δ\| 2500; fade-to-silence; no overflow over 5000 random panels. |
| `smc_test` | SMC tracker: clean blocks never flushed; dirty hash flush exactly one 8 KB range; heavy load 2048 marks → 2048 range flushes, 0 global flushes. |
| `dma_test` | Fast DMA `$2118`/`$2119`/`$2104` byte-exact vs. the generic `PPU_port_write` loop over 9000 randomised transfers (incl. spr_info_ext boundary + OAM wrap). |
| `uninterleave_test` | `UnInterleaveROM`: `ROMSize=0` terminates; malloc failure leaves ROM/HiROM untouched; output is a permutation of the 32 KB blocks across ROM sizes. |
| `drawprio_test` | `draw_plane_withpriority` iterates exactly `nb_tilex*nb_tiley` tiles (canary at index n untouched), 20 configs. |
| `lineinfo_test` | `lineInfo` seqlock: 0 torn reads while the consumer preempts mid-write (unguarded control shows 400000 tears). |

Cross-build checks (`arm-none-eabi-gcc -mcpu=arm946e-s`, `-Wall -Wextra`):
`dsp_declick.c`, `mem_cache.c` (raw CP15 `c7,c5,1` line loop verified in the
disassembly), the three fast-DMA functions, `UnInterleaveROM`, `lineSeqBegin`
and `add_tile_8` all compile clean for the target.

Linker (`arm-none-eabi-ld` against the real `arm9/nds_arm9_ld.ld`): a small
payload links, an oversized `.itcm` trips the 32 KB guard, and a 5 MB payload
trips the 4 MB EWRAM guard.
