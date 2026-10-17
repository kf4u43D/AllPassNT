# AllPassNT / SmearNT

Custom disting NT effect built around a stereo allpass smearing network.

See [docs/README.md](docs/README.md) for build instructions, API notes, and installation details.
The current findings and remaining hardware work are tracked in `docs/AUDIT.md`.

## Version modulation / stereo du 2026-10-15

La version precedente (sources, presets, objet ARM et fichiers de validation presents) est conservee dans `archive/2026-10-15/smearnt-before-modulation-2026-10-15.zip`. Son manifeste embarque et son fichier `.sha256` ont ete verifies avant modification. Les anciennes archives et le checkout SDK facultatif ne sont pas dupliques.

Cette version ajoute deux modulateurs synchronises sur une horloge interne, une horloge CV ou MIDI Clock, huit routes de modulation et une largeur stereo selective en frequence. Les nouveaux reglages sont ajoutes apres les onze parametres existants. Le GUID `SMNT` et l'API v13 sont conserves. Les anciens presets gardent leurs indices, mais leur son evolue avec les nouvelles plages de frequence et de resonance. La recharge sur le module reste a verifier.

- **Clock** : choisir Internal (20 a 300 BPM), CV ou MIDI. Pour CV, choisir le bus Clock CV et 1, 2, 4 ou 24 impulsions par noire. Reset CV accepte un front montant pour recaler la phase, avec toute source d'horloge. Les entrees sont en volts : front haut a 1 V, rearme sous 0,5 V. Le bus 0 desactive une entree.
- **Mod 1 / Mod 2** : sinus, triangle, rampe montante, descendante ou carre ; cycles de 1/64 de note a 16 mesures, dont valeurs pointees D et triolets T. Une mesure contient le nombre de noires choisi dans Beats per bar (4 par defaut). Amount est un attenuateur inverseur de -100 a +100 %, Offset ajoute une constante et Phase decale le cycle. La sortie normalisee est limitee a -1..+1 apres amount et offset.
- **CV inputs** : affecter deux bus a CV 1 et CV 2. +/-5 V correspond a +/-1 ; les tensions au-dela sont limitees. Une entree non finie devient zero.
- **Route 1..8** : choisir source, cible et amount signe. Les sources sont Mod 1, Mod 2, CV 1 et CV 2 ; les cibles sont Frequency, Resonance, Stages, Distribution, Feedback, Spread, LR motion et Crossover. Les routes s'additionnent au reglage manuel, puis la valeur finale est limitee a sa plage. Les reglages affiches et sauvegardes restent les valeurs manuelles.
- **Stereo** : Spread a 0 desactive la nouvelle spatialisation. En montant Spread, le grave se rapproche du centre et les aigus s'elargissent. Crossover (80..4000 Hz, 250 par defaut) fixe la transition. LR motion (-100..+100) deplace les aigus ; une route depuis un modulateur anime ce mouvement. Monter Spread pour entendre LR motion.

Exemple : Stereo spread = 75, Crossover = 250 Hz ; Mod 1 cycle = 4 bars, Amount = 100, Offset = 0 ; Route 1 source = Mod 1, target = LR motion, amount = 70. Pour balayer le reseau en parallele : Route 2 source = Mod 2, target = Frequency, amount = 20 ; Mod 2 cycle = 16 bars. Pour une commande externe : CV 1 input = 3 ; Route 3 source = CV 1, target = Resonance, amount = 25.

MIDI Clock utilise 24 impulsions par noire, Start remet la phase a zero, Continue reprend et Stop fige les modulateurs. Envoyer Start ou Continue pour demarrer en mode MIDI. Les periodes longues sont conservees entre les impulsions. Une horloge externe absente finit par figer la phase ; aucune bascule automatique sur le tempo interne n'est faite. Les changements de source/PPQN recalent le transport. Song Position Pointer n'est pas gere par l'API v13 utilisee.

Frequency deplace maintenant les distributions de +/-4 octaves, avec des poles limites entre **20 Hz et 45 % de la frequence d'echantillonnage** (21,6 kHz a 48 kHz). Le rayon des poles de Resonance passe de **0,60..0,80 a 0,60..0,97** ; ce reglage allpass n'est pas une valeur Q. Le feedback maximal reste 0,60. Les horloges sont detectees a chaque echantillon ; les routes sont actualisees tous les 32 echantillons et les commandes DSP sont lissees. Le rendu audio hors ligne existant porte sur les commandes statiques du coeur ; les horloges et les routes sont exercees par les tests de l'adaptateur.

[SmearNT16](presets/README.md) provides 16 native disting NT presets demonstrating network lengths, distributions, tap blends and feedback. The current local SD-ready pack is `archive/2026-10-15/SmearNT16-NT-2026-10-15.zip`, with the new modulation/stereo plug-in and its usage guide. The original 2026-10-05 pack remains archived. These archives are excluded from Git; use the [build instructions](docs/README.md#build) and [preset installation guide](presets/README.md#installation-et-écoute) with a GitHub checkout.

Quick checks from PowerShell:

- `.\check.ps1` runs the complete hardware-independent validation.
- `.\build.ps1` builds the ARM plug-in object.
- `.\test.ps1` builds and runs the native DSP/adapter regression suite.
- `.\tools\render.ps1` generates an impulse or sweep WAV through the real DSP core.
- `.\verify.ps1` checks the pinned API, ARM ABI, export, and firmware-facing symbols.

The 2026-10-05 maintenance release fixes repeated initialization, exact control smoothing, host block bounds, and renderer edge cases. The complete validation passes with sanitizers and verified Cortex-M7 builds.

The official stable firmware is disting NT 1.18.0 (2026-09-04). See [firmware/README.md](firmware/README.md) for the downloaded package, provenance and hardware update procedure. The plug-in retains its pinned API v13 compatibility.

The new local source release is `archive/2026-10-15/smearnt-release-2026-10-15.zip`. Earlier artifacts, sources, firmware and validation evidence remain locally under `archive/2026-10-05/`. The `archive/` directory, build outputs and downloaded firmware ZIP/PDF files are intentionally excluded from Git. Firmware provenance and checksums are included; the [firmware guide](firmware/README.md) links to the manufacturer downloads.
