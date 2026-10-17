# SmearNT engineering audit - 2026-10-15

The modulation/stereo release passes the complete local gate. Two shared-clock modulators support Internal/CV/MIDI timing, musical divisions through 16 bars, signed attenuation, offset and phase. Eight summed routes connect the modulators or two CV inputs to the five original DSP controls and three stereo controls. The appended schema has 56 parameters; the original eleven indices and SMNT GUID are unchanged.

Frequency pole limits are now 20 Hz..0.45 Fs with +/-4-octave shifts. Resonance pole radii span 0.60..0.97. Optional stereo processing narrows bass and spreads treble, including mono sources; signed HF balance animates LR movement above an adjustable 80..4000 Hz crossover. New stereo processing is exactly bypassed at Spread = 0. The expanded ranges change the sound of old presets.

Validation commands: `pwsh -NoProfile -File ./check.ps1 -OutDir ./build/audit-2026-10-15` and `pwsh -NoProfile -File ./tools/verify_presets.ps1`. Both returned exit code 0 on final sources. Evidence is in `build/audit-2026-10-15/check.log`, `presets.log` and `build/preset-validation/validation.json`.

- Strict native DSP/adapter regressions and ASan/UBSan pass. Tests include multi-bar drift and meter changes; CV Schmitt edges, reset and PPQN; MIDI Start/Continue/Stop and timeout recovery; 300 BPM MIDI delivered on 24/64-frame boundaries; CV scaling, inverse depths, summed routes and same-sample aliased bus reads; and invariant output across host block sizes.
- Spectral tests verify bass centering, mono HF spread and signed HF balance. Extended poles and coefficient ramps retain positive Jury stability margins; aggressive automated sweeps and extreme finite-input recovery pass at 8, 48 and 384 kHz.
- The Cortex-M7 object passes the pinned API v13, ARMv7E-M/Thumb-2/FPv5 hard-float, pluginEntry and import checks. Imports remain `_GLOBAL_OFFSET_TABLE_`, `NT_globals`, `expf`, `memset`. Two builds are byte-identical; failed builds preserve the previous verified object.
- Impulse and sweep renders contain 4800 frames each at 48 kHz: peaks 0.353610 and 0.895803, zero clipped samples for these default-setting renders. Renderer boundary/rejection tests pass.
- All 16 historical preset JSON files remain unchanged and pass native execution with appended defaults supplied by the host inspector. This verifies the adapter and project envelope, not the firmware JSON loader.

Before edits, `archive/2026-10-15/smearnt-before-modulation-2026-10-15.zip` captured 80 project files including sources, the previous object, presets, firmware and existing evidence. Every payload was read back against the embedded SHA-256 manifest; the ZIP also has a sidecar checksum. Existing archives and the optional SDK checkout were excluded and remain untouched.

No module was flashed or tested. Physical loading, preset save/reload, clock reception over the device's MIDI connection, listening tests and Cortex-M7 CPU measurement remain hardware checks. No hosted CI run is claimed. The previous dated audit follows for provenance.

## Previous maintenance audit - 2026-10-05

The hardware-independent audit and validation are complete. The current plug-in object is rebuilt and verified. The official disting NT firmware 1.18.0 and its manual are downloaded; the module has not been flashed or tested on hardware.

## Scope and findings

The review covered the DSP core, parameter smoothing, coefficient design, host adapter, persisted parameters, routing and aliasing, renderer/CLI, PowerShell and Make builds, API/ABI verification, CI, firmware compatibility, documentation and legacy artifacts. An independent review checked the routing and build changes.

| Area | Defect and correction | Evidence |
| --- | --- | --- |
| DSP initialization | Reusing a core could leave allpass poles at the network initialization values because the cached controls suppressed coefficient design. Invalidate the cache before designing initial coefficients. | Reinitialized cores match fresh instances at four sample rates. |
| Parameter smoothing | A nonzero endpoint could stall when a float increment rounded away; a target of 1 stalled at 0.9999714 at 48 kHz. Snap when the next float equals the current float. | Exact upward and downward endpoints at four sample rates; eight failing assertions before the fix. |
| Host adapter | An oversized positive frame count could pass the integer-overflow check and exceed the host bus buffer. Reject frames above NT_globals.maxFramesPerStep. | Oversized blocks leave the entire bus buffer and algorithm state untouched. |
| WAV frame count | A duration near or above 2^64 frames could be converted to uint64_t before the RIFF size limit was enforced. Check the much smaller RIFF bound first. | Giant-duration and uint64-boundary CLI requests fail without overwriting an existing file. |
| Log sweep | Computing end/start could overflow although both endpoints were finite, producing a silent successful render. Interpolate logarithms. | The formerly silent 1e-307 to 20000 Hz sweep produces valid non-silent stereo PCM. |
| CLI | Empty inline values could be confused with absent inline values. Track the equals delimiter explicitly and reject values on boolean/help flags. | --duration= and --help= fail and preserve existing output. |
| Build publication | Compilation could overwrite the previous object before API/ABI verification. Validate the API first, compile to a unique temporary object, verify it, then publish atomically. Use a typed NullString for the .NET replacement call. | Two successful ARM builds have identical SHA-256; invalid SDK and partially failing compiler preserve the existing object and remove the temporary file. |
| ARM verifier | Weak unresolved symbols were omitted from the advertised strict allowlist. Inspect U, w and v imports; support explicitly requested command names as well as paths. | The released object imports only the approved symbols listed below. |
| WAV validation | Format chunk length and expected frame count were unchecked. Validate both. | The 16-bit and 24-bit gate renders have exactly 4800 frames. |

Before the DSP fixes, the new regression suite reported 16392 failed assertions (8 smoothing endpoints and 16384 impulse-response comparisons). After the fixes, all native tests pass. The renderer silence was also reproduced before correction.

## Completed validation

Command: `pwsh -NoProfile -ExecutionPolicy Bypass -File .\check.ps1 -OutDir .\build\audit-2026-10-05`

The complete gate returned exit code 0:

- Native DSP and adapter regressions, warnings as errors and strict core warnings.
- AddressSanitizer and UndefinedBehaviorSanitizer.
- Cortex-M7/Thumb-2/FPv5 hard-float relocatable ARM build and API/ABI/export verification.
- Rebuild of the existing ARM object, with identical SHA-256.
- Invalid SDK rejection before compiler invocation; partial compiler failure recovery.
- 48000 Hz stereo impulse render, 24-bit, 4800 frames, peak 0.282483, zero clipped samples.
- 48000 Hz stereo sweep render, 16-bit, 4800 frames, peak 0.887110, zero clipped samples.
- RIFF sizes, PCM format, alignment, frame counts and non-silent payloads.
- Four rejected renderer requests preserve their pre-existing files; the extreme sweep contains 960 frames of non-silent PCM.

`tools/test_build.ps1` also passed independently under Windows PowerShell 5.1. Script syntax was checked. The current GitHub workflow includes build-failure and renderer regressions plus successful PowerShell object replacement on its ARM runner; hosted CI has not run locally.

The exact API v13 header remains SHA-256 `2FB0A626EE495CD69D7C2C68AA3EC1206CA7F853E7967AAD8F709FC4613E4206` from upstream `f440d20`. `pluginEntry` is globally exported. The allowed imports in the produced object are `_GLOBAL_OFFSET_TABLE_`, `NT_globals`, `expf` and `memset`.

## Firmware and compatibility

The latest official stable release checked on 2026-10-05 is **disting NT 1.18.0**, released **2026-09-04**. The untouched ZIP and PDF are in `firmware/1.18.0/`, with source URLs, local SHA-256 checksums and validation in `provenance.json`. The ZIP was read fully; its manifest identifies MIMXRT1060 and MCUXpresso 26.06. The manufacturer manifest provides no file hashes, so the recorded checksums are local integrity records.

API v14 was added upstream on 2026-09-29, after stable firmware 1.18.0. Its stable firmware minimum is not confirmed; SmearNT retains pinned API v13 and firmware 1.15.0 minimum. GUID `SMNT` and the existing 11 persisted parameter indices are preserved.

Hardware update instructions are in [the firmware guide](../firmware/README.md). A connected module or accessible microSD location is required to continue the physical update.

## Cleanup and archive

- 33 old generated files were archived and removed from their previous locations after matching SHA-256 copies; old validation folders and the duplicate root DLL were cleared.
- `archive/2026-10-05/legacy/manifest.json` records those archived files and the previous audit.
- `archive/2026-10-05/legacy-source/` contains the original SmearCore.cpp, ParamSmoother.h and smear_render.cpp for rollback.
- `archive/2026-10-05/smearnt-release-2026-10-05.zip` contains current sources, the verified object, official firmware package/manual, and validation evidence. Its embedded manifest covers every payload file; the ZIP is read back and checked against the manifest.
- The optional SDK checkout has a local modification and an older checkout commit. The deterministic build uses the pinned header under include; any SDK override must match its SHA-256.
- The repository had no initial Git commit, with application sources already untracked. The release archive provides a complete current source snapshot.

## Remaining hardware validation

Load/scan and unload/reload SmearNT on firmware 1.18.0, check preset save/reload and parameter automation, measure Cortex-M7 CPU under automation, and listen for artifacts. The continuous Stages crossfade and same-bus stereo averaging/Replace policy remain product choices documented in README.md. No physical firmware flash, hardware CPU measurement or listening test has been performed.

A final read-only Windows PnP and removable-disk check detected no disting NT/NXP bootloader device and no removable disk. The result is recorded as validation/hardware-detection.json in the release archive.

Official references: [firmware history](https://www.expert-sleepers.co.uk/distingNTfirmwareupdates.html), [pinned API v13](https://github.com/expertsleepersltd/distingNT_API/blob/f440d20/include/distingnt/api.h), [API v14 addition](https://github.com/expertsleepersltd/distingNT_API/commit/f8b7d69b629f5bf40ee14979bcda7e50a31111e7), [GNU nm symbol types](https://sourceware.org/binutils/docs/binutils/nm.html), and [PowerShell NullString](https://learn.microsoft.com/en-us/dotnet/api/system.management.automation.language.nullstring).
