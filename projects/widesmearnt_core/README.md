# WideSmearNT Core

This folder is kept only as a project note. The canonical source now lives in:

- `src/widesmearnt_core/`: portable DSP core
- `src/WideSmearNTAdapter.cpp`: disting NT adapter
- `include/distingnt/api.h`: pinned API v13-compatible header
- `docs/README.md`: build and API notes

Current state:

- Target: disting NT plug-in object `build/WideSmearNT.o`
- Vendored API header: exact API v13 pin under `include/distingnt/api.h`
- Optional SDK reference checkout: `distingNT_API/` (ignored)
- Build entry point on Windows: `build.ps1`
- Complete hardware-independent gate: `check.ps1`
- Offline impulse/sweep renderer: `tools/render.ps1`

Do not use older examples that reference `distingNT_API/examples/smearnt_nt/`, API v4, or the previous CV mapping. They are stale relative to the current adapter.
