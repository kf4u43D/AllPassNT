# SmearNT16 — 16 presets natifs disting NT

Mise a jour du 2026-10-15 : la nouvelle version conserve les onze indices historiques et ajoute 45 reglages. Les fichiers historiques ci-dessous restent inchanges ; le verificateur les execute avec les nouvelles valeurs par defaut (routes inactives, Spread = 0). Le son change avec les plages etendues : Frequency couvre maintenant +/-4 octaves, poles limites a 20 Hz..0,45 Fs ; Resonance couvre un rayon de 0,60..0,97. Les anciennes valeurs de plage mentionnees plus bas decrivent le pack archive du 2026-10-05. Le guide des modulateurs, horloges et routes est dans le README principal. Le chargement et la recharge sur le module restent a verifier.

Les 16 fichiers de `SmearNT16/` sont des presets JSON natifs **disting NT, version 2**. Chaque fichier charge un seul slot SmearNT, sans autre algorithme ni fichier audio nécessaire. Les noms affichés et les noms de fichiers sont en ASCII.

Le pack SD local `archive/2026-10-05/SmearNT16-NT-2026-10-05.zip` contient les 16 presets, le plug-in ARM vérifié et le rapport de validation. Les archives et objets compilés sont exclus de Git ; depuis un checkout GitHub, compiler le plug-in selon les [instructions de construction](../docs/README.md#build) puis copier les fichiers comme indiqué ci-dessous. Le pack utilise SmearNT **API v13**, avec firmware **1.15.0 minimum** ; la cible préparée est **1.18.0**. Le chargement et la sauvegarde sur un module physique restent à vérifier.

## Installation et écoute

1. Depuis le dépôt, copier le dossier `presets/SmearNT16/` dans `/presets/SmearNT16/` sur la MicroSD et l'objet compilé `build/smearnt_nt.o` dans `/programs/plug-ins/`. Avec un pack SD local, extraire le ZIP puis copier les mêmes dossiers depuis `presets/` et `programs/plug-ins/`.
2. Redémarrer le disting NT pour charger le plug-in, puis ouvrir un fichier de la banque depuis le menu de chargement des presets.
3. Envoyer une source stéréo sur les entrées physiques **1 et 2** ; écouter les sorties physiques **1 et 2**, soit les bus **13 et 14**. Les deux sorties sont en mode **Replace**, avec le signal traité à 100 %.
4. Pour une source mono, envoyer le même signal aux deux entrées, ou changer **In R** de `2` à `1` dans la page Routing de SmearNT.

Les presets fixent le moteur à **48 kHz / blocs de 24 échantillons**. Ce sont des presets complets du NT : leur chargement restaure leur moteur et leur unique slot.

Comparer les presets d'une même famille avec la même source, au même niveau. Une boucle de batterie, des impulsions ou du bruit révèlent mieux la dispersion qu'une sinusoïde tenue. Pour 14–16, espacer les attaques et commencer avec un niveau modéré ; le feedback peut augmenter les crêtes. Les tests sans écrêtage utilisent des entrées limitées à ±0,2, et ne garantissent pas ce résultat à tout niveau.

## La banque

F = Frequency, R = Resonance, S = Stages, D = Distribution, FB = Feedback. Les valeurs ci-dessous sont les entiers natifs, de 0 à 100.

| N° / nom | F | R | S | D | FB | Ce que le preset permet de comparer | Source utile |
| --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| 01 Phase 04 | 50 | 30 | 0 | 50 | 0 | Sortie après 4 sections ; référence courte | Kick, percussion sèche |
| 02 Phase 08 | 50 | 30 | 20 | 50 | 0 | Sortie après 8 sections | Même boucle que 01 |
| 03 Phase 12 | 50 | 30 | 40 | 50 | 0 | Sortie après 12 sections | Même boucle que 01 |
| 04 Phase 16 | 50 | 30 | 60 | 50 | 0 | Sortie après 16 sections | Même boucle que 01 |
| 05 Phase 20 | 50 | 30 | 80 | 50 | 0 | Sortie après 20 sections | Même boucle que 01 |
| 06 Phase 24 | 50 | 30 | 100 | 50 | 0 | Réseau complet de 24 sections | Même boucle que 01 |
| 07 Lineaire bas | 15 | 60 | 100 | 0 | 0 | Répartition linéaire des fréquences, déplacée vers le bas | Batterie large bande |
| 08 Log bas | 15 | 60 | 100 | 50 | 0 | Répartition logarithmique ; comparer à 07 | Même boucle que 07 |
| 09 Groupes bas | 15 | 60 | 100 | 100 | 0 | Répartition en groupes ; comparer à 07–08 | Même boucle que 07 |
| 10 Phase grave | 0 | 60 | 100 | 50 | 0 | Décalage de fréquence maximal vers le bas ; comparer à 08 | Kick, basse percussive |
| 11 Phase aigu | 85 | 60 | 100 | 50 | 0 | Décalage vers le haut ; comparer à 08 et 10 | Hi-hats, shaker, bruit |
| 12 Couleur 04 08 | 50 | 30 | 10 | 50 | 0 | Mélange égal des sorties après 4 et 8 sections | Accord tenu, bruit rose |
| 13 Couleur 12 16 | 50 | 30 | 50 | 50 | 0 | Mélange égal des sorties après 12 et 16 sections | Même source que 12 |
| 14 Phase resonante | 50 | 100 | 100 | 50 | 0 | Rayon des pôles maximal ; comparer à 06 | Percussions espacées |
| 15 Feedback 50 | 50 | 100 | 100 | 50 | 50 | Ajout de feedback interne à 0,3 ; comparer à 14 | Pluck, percussion espacée |
| 16 Feedback 100 | 50 | 100 | 100 | 50 | 100 | Feedback interne maximal à 0,6 ; comparer à 15 | Même source que 15 |

01–06 isolent le choix de sortie du réseau. 07–09 isolent Distribution. 08, 10 et 11 isolent Frequency. 12–13 montrent la coloration produite par le mélange des taps ; comparer également 12 aux presets 01–02 et 13 aux presets 03–04. 06 et 14 isolent Resonance. 14–16 isolent Feedback.

## Comprendre les réglages

**Stages** sélectionne ou mélange six sorties situées après 4, 8, 12, 16, 20 et 24 sections. Les valeurs exactes sont respectivement `0, 20, 40, 60, 80, 100` ; `0` utilise donc quatre sections. Entre deux valeurs exactes, les sorties voisines sont mélangées.

Sur une sortie exacte, avec Feedback à zéro et des paramètres stabilisés, le réseau allpass modifie la phase et la forme des transitoires tout en conservant la réponse en amplitude par canal. Les mélanges de sorties peuvent colorer le spectre par interférence de phase. Le canal droit possède un léger décalage des fréquences des pôles, qui peut produire une différence stéréo même avec une entrée mono dupliquée.

**Frequency** déplace la répartition jusqu'à ±2 octaves autour de `50`, avec des limites de 200 Hz et 45 % de la fréquence d'échantillonnage. **Distribution** passe de linéaire (`0`) à logarithmique (`50`), puis à une répartition en groupes (`100`). **Resonance** règle le rayon des pôles de 0,6 à 0,8. **Feedback** règle une boucle de 0 à 0,6, avec saturation douce dans la boucle.

Pour explorer les transitions, faire varier lentement Frequency, Stages ou Distribution après le chargement. Les presets sont des points de départ statiques ; ils ne contiennent pas de source de modulation.

## Format strict et validation

La structure reprend celle d'un [preset natif officiel Expert Sleepers](https://github.com/expertsleepersltd/distingNT/blob/main/presets/Barberpole%20Phaser.json). Le [manuel officiel 1.18](https://www.expert-sleepers.co.uk/downloads/manuals/disting_NT_user_manual_1.18.pdf) décrit les presets, les bus, le paramètre commun Bypass et les plug-ins. Les fichiers générés n'indiquent pas une sauvegarde matérielle fictive dans `saved-from`.

Chaque slot contient `guid: "SMNT"`, `specs: [0,0,0]`, puis exactement **12 entiers** dans `parameters` :

```text
Bypass, In L, In R, Out L, Out L mode, Out R, Out R mode,
Frequency, Resonance, Stages, Distribution, Feedback
```

Le préfixe commun Bypass vaut `0`. Le routage vaut `1,2,13,1,14,1`. Le GUID, les onze paramètres propres au plug-in et leurs plages sont contrôlés contre la factory compilée depuis l'adaptateur réel, et non seulement contre le générateur.

Depuis la racine du dépôt, `python tools/generate_presets.py` régénère les fichiers et `.\tools\verify_presets.ps1` compile l'inspecteur puis valide la banque. Le vérificateur refuse notamment les clés dupliquées, les BOM, les nombres non finis, les mauvais types, un nombre incorrect de paramètres et les valeurs hors plage. Chaque preset est ensuite exécuté par l'adaptateur natif à 48 kHz / 24 échantillons, sur silence, impulsions, multisinusoïdes et fin de signal. Il contrôle les valeurs finies, les crêtes, les sorties non nulles, les modes Replace et la préservation des autres bus.

Le rapport `build/preset-validation/validation.json` consigne les sommes SHA-256 des fichiers, le contrat de la factory et les résultats audio. **Ces contrôles ne passent pas par le chargeur JSON du firmware : le chargement, la sauvegarde et la recharge sur disting NT restent non testés.**
