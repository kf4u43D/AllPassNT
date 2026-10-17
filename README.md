# SmearNT

A stereo phase effect for **disting NT**. Smear drum attacks, colour percussion and add movement to drones. Two clocked modulators animate the sound; stereo controls keep the bass centred while spreading the highs.

## Install

Requires NT firmware **1.15.0 or later**.

1. Download and unzip [SmearNT16](https://github.com/kf4u43D/AllPassNT/releases/latest).
2. Copy `programs/` and `presets/` to the root of the NT's MicroSD card.
3. Restart the NT and load a preset from `SmearNT16`.
4. Use audio inputs **1/2** and outputs **1/2**. For mono, set **In R = 1**.

Plug-in: `/programs/plug-ins/smearnt_nt.o`. Presets: `/presets/SmearNT16/`.

## Play

- **Frequency / Distribution** shape the colour; **Stages** changes the phase texture.
- **Resonance / Feedback** add emphasis and intensity. Raise feedback gradually.
- **Spread** widens the highs above **Crossover**; **LR motion** moves them left and right.
- Choose **Internal**, **CV** or **MIDI Clock**, then route **Mod 1/2** to a control. MIDI needs Start or Continue.

Try **Spread = 75**, **Mod 1 cycle = 4 bars**, then route **Mod 1 → LR motion** at **70** for a slow stereo drift.

[16 presets](https://github.com/kf4u43D/AllPassNT/blob/main/presets/README.md) provide starting points for drums, bass and sustained sounds.
