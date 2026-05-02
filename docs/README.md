# SmearNT (disting NT, API v13)

Canonical layout:
- `src/smearnt_core/`: DSP core (allpass network, smoother, etc.)
- `src/SmearNTAdapter.cpp`: NT adapter using API v13
- `include/distingnt/api.h`: pinned disting NT API header used by the build
- `resources/`: reserved (empty)
- `docs/`: documentation (this file)
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

Copy `build/smearnt_nt.o` to the disting NT SD card `disting/plugins/` directory.

## API conformance (v13)
- `pluginEntry` returns `kNT_apiVersionCurrent` (currently `kNT_apiVersion13`), single factory.
- Lifecycle: `calculateRequirements`, `construct`, `parameterChanged`, `step` implemented; others null.
- I/O: bus array in `step` (`numFrames = numFramesBy4*4`); defaults: In 1/2, Out 13/14 with add/replace modes.
- Parameters: Frequency, Resonance, Stages, Distribution, Feedback (percent units).

## SDK status
The vendored `include/distingnt/api.h` header has been updated for API v13 compatibility: bus-count constants, expanded `_NT_parameterPage`, `parameterString()`, stream globals, and `NT_updateParameterPages()`. A full `distingNT_API/` checkout is optional and intentionally ignored.

## Notes
- Core/DSP is isolated under `src/`; the pinned API header is under `include/`.
- Removed earlier SimpleGain/demo to avoid duplicates; this is the single canonical plug-in.
- The right allpass network uses a small alternating frequency offset for stereo decorrelation.
