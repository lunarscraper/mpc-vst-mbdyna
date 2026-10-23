# mpc-vst-mbdyna

MB Dyna: a **four-band compressor for the master bus** as a VST2 **insert effect** for the Akai MPC OS plugin host
(Force, MPC Live/One/X/Key). An unofficial port of the
[LSP Multiband Dynamics Processor](https://github.com/lsp-plugins/lsp-plugins-mb-dyna-processor)
(`lsp-plugins-mb-dyna-processor` 1.0.32, stereo) by Vladimir Sadovnikov and the LSP Project.

The audio runs through **LSP's own plugin class, unchanged**, with LSP's DSP libraries (lsp-dsp-lib with its NEON
code for the Force's Cortex-A17, lsp-dsp-units). What is new: the VST2 shell with float audio, the MPC page, the
mapping of its controls onto LSP's ports, 32 slots and the factory settings.

## Pages

**BANDS**: one column per band, 1 LOW (below SPLIT 1), 2 LOW MID, 3 HIGH MID, 4 HIGH (above SPLIT 3).
- **THRESHOLD** (-60..0 dB), **RATIO** (1:1..20:1), **MAKEUP** (-12..+24 dB)
- **ON** (off = the band passes unprocessed), **SOLO** (hear only that band, for setting the splits)
- Q-Links: one column per band (THRESHOLD, RATIO, MAKEUP)

**TIME**: **ATTACK** (0.1..500 ms) and **RELEASE** (5 ms..2 s) per band; on the right **REACT** (the detector's
reaction time), **DETECTOR** (PEAK, RMS, LPF, SMA) and **SC BOOST** for all bands.
- SC BOOST weights the detector: NONE (default here), PINK BT/MT, BROWN BT/MT. LSP's own default is PINK BT,
  which makes the low bands react more strongly than their threshold says; with NONE a threshold reads as the
  band's level.

**SETUP**: **SPLIT 1/2/3** (crossover points), **INPUT**, **OUTPUT** (±24 dB), **MIX** (0 = dry, 100 = processed,
in between parallel compression), **CROSSOVER** CLASSIC or MODERN (LSP's two IIR crossovers; MODERN is LSP's
default), and the slots: **SLOT** (1..32), **LOAD**, **SAVE**.

## Slots and the PRESET menu

The 32 slots are the plugin's VST programs, so MPC's PRESET menu lists them. Slots 1..10 start as factory
settings: INIT (ratio 1 everywhere: transparent), GLUE, TECHNO MASTER, GOA MASTER, PUNCH, BASS TIGHT, DE-MUD,
TAME HIGHS, LOUD, PARALLEL. They are starting points for a live techno / Goa master, to finish by ear:
THRESHOLD depends on how hot the mix arrives. **SAVE** writes the current settings into the slot set with SLOT
(`slots/NN.txt` in the plugin folder; the installer keeps that folder), **LOAD** reads it back; a saved slot is
called `NN USER` in the menu. The project stores the knobs as they are.

## Left out of LSP's plugin

The graphs and meters (MPC skins have no curve display), Linear Phase crossover (FFT, latency, CPU), bands 5..8,
the L/R and M/S variants and external side-chain, the extra curve points (LSP has four per band; MB Dyna uses the
first as a plain compressor: threshold = gain, ratio above it, 1:1 below), per-point attack/release levels, hold,
knee (LSP's default -6 dB), lookahead, side-chain filters and source selection, shared-memory link. They stay at
LSP's defaults.

## Why a hand-written shell and not the shared wrapper

mpc-vst-plugins' shared `wrapper/vst2_wrap.c` hands an engine int16 audio. On the master bus that clips anything
above 0 dBFS and cuts the mix to 16 bits, so `vst/fxshell.h` keeps the host's float buffers. It follows the rules
of the shared wrapper's 2026-10-21 fix (mpc-vst-plugins #486-#489): host writes never send `audioMasterUpdateDisplay`
(Q-Links keep working on the automation screen), at most one UpdateDisplay per 50 ms, values changed by a preset
or slot are reported from `processReplacing`, option lists and whole numbers move one step per data-wheel click
or Q-Link event, `params.lock` keeps the parameter order (append only).

## Build

`vst/build.sh` builds `vst/build/mbdyna.so` (armhf, gcc 11 / glibc 2.31 in an `arm32v7` container) and the skin;
`vst/test.sh` runs the offline test on x86-64 (unity at ratio 1 and at MIX 0, a band compressing only its own
range by what the curve says, SOLO, MAKEUP, the splits, slots and PRESET menu, project chunk, no UpdateDisplay on
host writes) and a CPU benchmark (x86-64: about 1.4 % of a core in CLASSIC, 2.5 % in MODERN; the Force's
Cortex-A17 needs several times that, so measure with mpc-vst-plugins' `tools/bench.sh`). The workflow under
`.github/workflows` does both and makes a draft release. LSP's code is vendored: `vst/lsp/VENDORED.md`.

## License and credits

GNU Lesser General Public License v3 or later ([LICENSE](LICENSE), which builds on the GNU GPL v3 in
[COPYING](COPYING)), as LSP. LSP Plugins: Copyright (C) Vladimir Sadovnikov and the LSP contributors,
<https://lsp-plug.in>. The LSP code in `vst/lsp/lsp-vendor.tar.gz` is unchanged; see `vst/lsp/VENDORED.md`.
Not affiliated with the LSP Project. The font is Titillium Web (SIL Open Font License, `vst/fonts/OFL.txt`).

## Hinweis

Entwickelt mit Unterstützung von Claude (Anthropic)
