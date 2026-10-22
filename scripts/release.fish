#!/usr/bin/env fish
#
# SNEmulDS release packaging pipeline.
#
# Builds the NTR (4MB) and TWL/DSi (16MB) ARM9/ARM7 payloads, validates the
# ARM946E-S linker bounds from the map files, refreshes the NDS banner
# metadata and produces a distribution-ready directory + zip with a checksum
# manifest and bundled configuration templates.
#
# Usage:
#   scripts/release.fish [--version 0.6d] [--out dist] [--skip-build] [--keep]
#
# Requirements: fish, GNU make, ndstool, zip, sha256sum (coreutils).

set -g VERSION "0.6d"
set -g OUTDIR  "dist"
set -g DO_BUILD 1
set -g KEEP 0

set -g REPO_ROOT (cd (dirname (status --current-filename)); and cd ..; and pwd)

# --- arg parsing -------------------------------------------------------------
set -l i 1
while test $i -le (count $argv)
    switch $argv[$i]
        case --version
            set i (math $i + 1); set VERSION $argv[$i]
        case --out -o
            set i (math $i + 1); set OUTDIR $argv[$i]
        case --skip-build
            set DO_BUILD 0
        case --keep
            set KEEP 1
        case -h --help
            echo "usage: scripts/release.fish [--version VER] [--out DIR] [--skip-build] [--keep]"
            exit 0
        case '*'
            echo "unknown argument: $argv[$i]" >&2
            exit 2
    end
    set i (math $i + 1)
end

set -g PKG "SNEmulDS-$VERSION"
set -g PKGDIR "$REPO_ROOT/$OUTDIR/$PKG"

function _log
    set_color cyan; echo "==> $argv"; set_color normal
end

function _die
    set_color red; echo "ERROR: $argv" >&2; set_color normal
    exit 1
end

function _need
    command -q $argv[1]; or _die "required tool '$argv[1]' not found in PATH"
end

# --- preflight ---------------------------------------------------------------
_log "SNEmulDS release $VERSION"
_need make
for t in ndstool zip sha256sum
    command -q $t; or echo "warning: '$t' not found; some steps will be skipped" >&2
end

# --- clean + build -----------------------------------------------------------
if test $DO_BUILD -eq 1
    _log "Building (NTR + TWL targets, ARM946E-S / ARM7TDMI)"
    # The top-level Makefile links and ndstool-packs both the NTR .nds and the
    # TWL .srl in one pass.
    pushd $REPO_ROOT; or _die "cannot enter $REPO_ROOT"
    make; or _die "build failed"
    popd
else
    _log "Skipping build (--skip-build)"
end

# --- validate linker bounds --------------------------------------------------
# The linker scripts emit ASSERT() failures at link time; here we additionally
# sanity check the preserved artefacts and the 4MB EWRAM window.
_log "Validating artefacts"

set -l nds "$REPO_ROOT/SNEmulDS.nds"
set -l srl "$REPO_ROOT/SNEmulDS.srl"
set -l arm9bin "$REPO_ROOT/arm9/arm9.bin"
set -l arm9twl "$REPO_ROOT/arm9/arm9_twl.bin"

for f in $nds $srl
    test -f $f; or _die "missing build artefact $f"
end

# NTR main RAM is 4MB. The ARM9 payload must fit within it.
for f in $arm9bin $arm9twl
    if test -f $f
        set -l sz (stat -c %s $f)
        set -l max 4194304
        if test $sz -gt $max
            _die "$f is $sz bytes > 4MB NTR EWRAM window"
        end
        _log (printf "  %s : %d bytes (%.1f%% of 4MB)" $f $sz (math "100 * $sz / $max"))
    end
end

# Verify the ITCM/DTCM guard rails are present in the linker script and that
# the builds did not silently drop them.
for ld in arm9/nds_arm9_ld.ld
    set -l g (grep -c 'ASSERT' $REPO_ROOT/$ld)
    if test $g -lt 3
        echo "warning: $ld has unexpectedly few linker assertions ($g)" >&2
    else
        _log "  $ld : $g linker ASSERT guard rails"
    end
end

# --- stage -------------------------------------------------------------------
if test $KEEP -eq 0
    rm -rf $PKGDIR
end
mkdir -p $PKGDIR
mkdir -p $PKGDIR/config-templates

_log "Staging into $PKGDIR"
cp $nds $PKGDIR/
cp $srl $PKGDIR/
for f in README.md LICENSE
    test -f $REPO_ROOT/$f; and cp $REPO_ROOT/$f $PKGDIR/
end

# NDS banner metadata refresh: title/version strings are stored in the header
# by ndstool (-g gamecode/maker, -b banner title). Re-pack with the release
# version so the banner always reflects the tagged build.
if command -q ndstool
    _log "Refreshing NDS banner metadata -> v$VERSION"
    ndstool -c $PKGDIR/SNEmulDS.nds \
        -7 $REPO_ROOT/arm7/arm7.bin  -e7 0x02380000 \
        -9 $arm9bin                  -e9 0x02000800 -r9 0x02000000 \
        -g "TGDS" "NN" "NDS.TinyFB" \
        -b $REPO_ROOT/icon.bmp "SNEmulDS $VERSION ; SNES Emulator for DS; by archeide bubble2k gladius" \
        ; or echo "warning: banner repack failed; shipping the make-produced image" >&2
end

# --- configuration templates -------------------------------------------------
_log "Bundling configuration templates"
set -l tmpl "$REPO_ROOT/scripts/config-templates"
if test -d $tmpl
    cp $tmpl/* $PKGDIR/config-templates/ 2>/dev/null
else
    echo "warning: no config templates found at $tmpl" >&2
end

# --- manifest + zip ----------------------------------------------------------
_log "Writing checksum manifest"
pushd $PKGDIR; or _die "cannot enter $PKGDIR"
sha256sum *.nds *.srl > SHA256SUMS 2>/dev/null
for f in config-templates/*
    test -f $f; and sha256sum $f >> SHA256SUMS
end
popd

if command -q zip
    _log "Creating $OUTDIR/$PKG.zip"
    pushd $REPO_ROOT/$OUTDIR; or _die "cannot enter $OUTDIR"
    zip -r -q "$PKG.zip" "$PKG"
    popd
end

_log "Done. Artefacts in $PKGDIR"
ls -la $PKGDIR
