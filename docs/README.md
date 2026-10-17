# SmearNT (disting NT, API v13)

Canonical layout:

- `src/smearnt_core/`: DSP core (allpass network, smoother, etc.)
- `src/SmearNTAdapter.cpp`: NT adapter using API v13
- `include/distingnt/api.h`: pinned disting NT API header used by the build
- `tests/`: native DSP and adapter regression suite
- `tools/`: dependency-free offline WAV renderer
- `resources/`: reserved plug-in resources
- `.github/workflows/ci.yml`: native/sanitizer and Cortex-M7 CI
- Optional local SDK checkout: `distingNT_API/` is ignored by git and can be used for examples/reference

## Build

Requirements: `arm-none-eabi-g++` (found in PATH). The required NT API header is vendored under `include/distingnt/api.h`.

Commands (from repo root):

- PowerShell: `.\build.ps1` (produces `build/smearnt_nt.o`)
- If script execution is blocked: `powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1`
- PowerShell clean: `.\build.ps1 -Clean`
- Override toolchain: `.\build.ps1 -Toolchain "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.3 rel1\bin\arm-none-eabi-g++.exe"`
- `make` (produces `build/smearnt_nt.o`)
- Override toolchain if needed: `make TOOLCHAIN="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.3 rel1/bin/arm-none-eabi-g++"`
- Clean: `make clean`

Both the PowerShell and Make builds validate the pinned API before compilation and run `verify.ps1` on a temporary object before replacing the final object. Cross-platform Make builds require PowerShell 7 (`pwsh`); native Windows scripts support PowerShell 5.1 or 7. It checks the exact API-header SHA-256, ELF32/little-endian/relocatable format, ARMv7E-M/Thumb-2/FPv5 hard-float attributes, the `pluginEntry` export, and a strict unresolved-symbol allowlist, including weak imports. It can also be run independently with `.\verify.ps1` or `make verify`.

On Windows, run `make` from MSYS2 or Git Bash so that `mkdir` and `rm` are available. The PowerShell build is the native Windows path.

## Tests hôte

The portable DSP core and the disting NT adapter have a native regression suite. It checks the cosine approximation, coefficient interpolation and Jury stability, allpass energy, long-run numerical stability, in-place processing, the factory contract, routing, and Add/Replace modes.

- PowerShell: `.\test.ps1`
- Address/undefined-behaviour sanitizers: `.\test.ps1 -Sanitize`
- Warnings as errors plus strict core warnings: `.\test.ps1 -WarningsAsErrors -StrictCore`
- Override the native compiler: `.\test.ps1 -Compiler "C:\Program Files\LLVM\bin\clang++.exe"`
- Make: `make test`
- Complete local gate: `.\check.ps1` or `make check`

`check.ps1` runs strict native tests, ASan+UBSan, the Cortex-M7 build and ABI verifier, then impulse and logarithmic-sweep renders. It rejects clipping/non-finite samples and checks RIFF sizes, PCM rate/alignment and non-silent payloads. Use `-SkipSanitizers` only on a host without sanitizer runtimes.

The test executable is written under `build/`. Tests use `src/NTHostShim.cpp` to provide the small set of host globals required by the adapter; that shim is never included in the ARM plug-in.

## Rendu audio hors ligne

The renderer compiles the same `SmearCore` used by the plug-in and writes stereo PCM without external libraries:

```powershell
.\tools\render.ps1 -Source impulse -Duration 2 -BitDepth 24 `
  -Frequency 62 -Resonance 70 -Stages 85 -Distribution 40 -Feedback 35

.\tools\render.ps1 -Source sweep -Duration 5 -BitDepth 16 `
  -SweepStart 20 -SweepEnd 20000 -Output build\smear-sweep.wav
```

All controls use the same `0..100` values shown by the plug-in. The source is sent identically to both inputs and the file contains the 100% wet stereo result.

## Installation

SmearNT targets plug-in API v13 and therefore requires disting NT firmware 1.15.0 or newer. Copy `build/smearnt_nt.o` to `/programs/plug-ins/` on the MicroSD card, then reboot/remount the card or trigger a plug-in rescan.

## API conformance (v13)

- `pluginEntry` returns `kNT_apiVersionCurrent` (currently `kNT_apiVersion13`), single factory.
- Lifecycle: `calculateRequirements`, `construct`, `parameterChanged`, `step` and `midiRealtime` implemented; other callbacks null.
- I/O: bus array in `step` (`numFrames = numFramesBy4*4`); defaults: In 1/2, Out 13/14 with add/replace modes.
- Parameters: the original eleven persisted indices are unchanged; 45 parameters are appended for stereo, clock, two modulators, two CV inputs and eight modulation routes. See the usage guide in the root README.

## SDK status

The vendored `include/distingnt/api.h` is an exact copy of the official Expert Sleepers API v13 header at upstream commit `f440d20` (SHA-256 `2FB0A626EE495CD69D7C2C68AA3EC1206CA7F853E7967AAD8F709FC4613E4206`, verified 2026-10-05). A full `distingNT_API/` checkout is optional and ignored; `-NtApiPath` is accepted only when its compiled header has that same pinned hash.

## CI

The workflow runs strict native tests, ASan+UBSan and a renderer smoke test on Windows, then cross-compiles and verifies the Cortex-M7 object on Ubuntu. It will start on the first push, pull request, or manual dispatch once this directory is committed to a GitHub repository.

## Notes

- Core/DSP is isolated under `src/`; the pinned API header is under `include/`.
- Removed earlier SimpleGain/demo to avoid duplicates; this is the single canonical plug-in.
- The right allpass network uses a small alternating frequency offset for stereo decorrelation.
- Optional frequency-selective stereo processing centers bass and spreads/moves treble above an adjustable crossover. Frequency now spans poles from 20 Hz to 0.45 Fs, with +/-4-octave distribution shifts; resonance pole radii span 0.60..0.97. Old presets retain their controls but have an expanded sound range.
- Pole coefficients are refreshed and linearly interpolated over 32 samples; static settings do not pay the interpolation loop.
- The coefficient cosine uses a branch-free polynomial over the exact design domain. Dense native testing bounds its absolute error below `2e-7`, and the ARM object no longer imports `cosf`.
- `Stages` continuously crossfades adjacent 4/8/12/16/20/24-section taps. Intermediate values intentionally create phase-cancellation colour; only the six exact tap positions retain a strict allpass magnitude response.

## Maintenance release, 2026-10-05

The complete gate also checks repeated initialization, exact nonzero smoother endpoints, the host maximum block size, successful replacement of an existing ARM object, failure recovery, extreme sweep endpoints, oversized RIFF requests and empty inline CLI values. WAV validation checks the format chunk size and expected frame count.

Use `.\check.ps1 -OutDir .\build\audit-2026-10-05` to keep the full gate outputs together. The gate output directory must be inside the project. `tools/render.ps1` and `tools/test_build.ps1` also accept `-OutDir`. The ARM and native standalone scripts retain their absolute output-directory support.

The stable firmware checked on 2026-10-05 is **1.18.0**, released 2026-09-04. The package, manual, provenance, local SHA-256 checksums and NT Helper instructions are described in [the firmware guide](../firmware/README.md). API v14 appeared upstream on 2026-09-29, after this stable firmware; the pinned API v13 remains the build contract, with firmware 1.15.0 minimum.

The current findings and completed local validation are in [AUDIT.md](AUDIT.md). Old artifacts, the previous audit and original DSP/renderer sources are preserved locally under `archive/2026-10-05/`. The local release ZIP contains the standalone project, verified ARM object, official firmware package and validation evidence. Archives, build outputs and downloaded firmware ZIP/PDF files are intentionally excluded from Git. For a GitHub checkout, follow [Build](#build) above and use the manufacturer download links in [the firmware guide](../firmware/README.md).
