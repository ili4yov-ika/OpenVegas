# VEGAS Shared Plug-Ins — comparative DSP fixtures

OpenVegas **does not** `LoadLibrary` proprietary MAGIX/VEGAS Shared Plug-Ins DLLs.
Instead it maps pack FX names to **builtin substitutes** (`VegasSharedAudioCatalog`).

## Default roots (discovery only)

- `C:\Program Files (x86)\VEGAS\Shared Plug-Ins\Audio_x64\`
- `C:\Program Files (x86)\Sony\Shared Plug-Ins\Audio_x64\` (legacy)

## Unit tests (no Vegas binary load)

```text
ctest -R vegas-shared --test-dir build/Windows_MinGW-x64 --output-on-failure
```

Tags: `[vegas-shared]`, `[dsp]`, `[discovery]`.

## Future golden comparison vs Vegas Pro

Goal: verify OpenVegas builtins approximate Vegas Shared FX on known signals.

Suggested workflow (manual / offline — do not automate LoadLibrary of Vegas DLLs):

1. In Vegas Pro, insert one Shared FX (e.g. Track EQ @ +6 dB @ 1 kHz) on a short mono WAV.
2. Render / bounce to `golden/<fx_id>/vegas_out.wav`.
3. Store dry input as `golden/<fx_id>/input.wav`.
4. Add a Catch2 case that runs `processBuiltinFx` on `input.wav` and compares RMS / spectral distance to `vegas_out.wav` within a documented tolerance.

Place files under this folder as:

```text
tests/fixtures/vegas_shared/golden/
  track_eq_boost_1k/
    input.wav
    vegas_out.wav
    README.txt   # Vegas version, preset, params
```

Keep WAVs short (≤2 s) and prefer synthetic tones so diffs stay interpretable.
