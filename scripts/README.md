# SNEmulDS release / packaging tools

## `release.fish`

Builds both hardware targets and assembles a distribution bundle.

```
scripts/release.fish [--version 0.6d] [--out dist] [--skip-build] [--keep]
```

Steps:

1. **Build** — runs the top-level `make`, which produces the NTR `.nds`
   (`arm7.bin` + `arm9.bin`, ARM7TDMI / ARM946E-S) and the TWL/DSi `.srl`
   (`arm7_dsi.bin` + `arm9_twl.bin`).
2. **Validate** — checks the ARM9 payload fits the 4 MB NTR main-RAM window
   and that the linker scripts still carry their ITCM/DTCM/EWRAM `ASSERT`
   guard rails.
3. **Refresh banner metadata** — re-packs the `.nds` with `ndstool`, stamping
   the release version and game/maker codes into the banner.
4. **Bundle** — copies the binaries, `README.md`, `LICENSE` and the
   `config-templates/` into `dist/SNEmulDS-<version>/`.
5. **Manifest** — writes `SHA256SUMS` and (if `zip` is present) a
   `SNEmulDS-<version>.zip`.

`make release` is a thin wrapper around the same script.

## `config-templates/snemul.cfg`

A documented default configuration consumed by
`readOptionsFromConfig()` (`arm9/source/main.c`). Ships as
`config-templates/snemul.cfg`; users copy it next to the `.nds` and rename it
to `snemul.cfg`. Per-game overrides live in `[<ROM title>]` sections.

## Toolchain requirements

The build uses the ToolchainGenericDS SDK
(`/usr/arm-none-eabi/lib/newlib-nano-2.1-nds/...`); `Makefile.basenewlib` and
the TGDS libraries must be present. `ndstool`, `zip` and `sha256sum` are
optional refinements — the pipeline degrades gracefully without them.
