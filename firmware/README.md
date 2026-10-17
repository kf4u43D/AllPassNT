# Firmware disting NT

Version stable vérifiée le **5 octobre 2026** : **1.18.0**, publiée le **4 septembre 2026** sur la [page officielle Expert Sleepers](https://www.expert-sleepers.co.uk/distingNTfirmwareupdates.html).

Les fichiers téléchargés sont conservés localement dans `1.18.0/`. Le ZIP et le PDF sont intentionnellement exclus de Git ; les liens ci-dessous permettent de les télécharger depuis le fabricant. La provenance et les sommes SHA-256 sont incluses dans le dépôt :

- [Paquet firmware original du fabricant](https://www.expert-sleepers.co.uk/downloads/firmware/distingNT_1.18.0.zip), copie locale : `1.18.0/distingNT_1.18.0.zip`.
- [Manuel 1.18 du fabricant](https://www.expert-sleepers.co.uk/downloads/manuals/disting_NT_user_manual_1.18.pdf), copie locale : `1.18.0/disting_NT_user_manual_1.18.pdf`.
- [Provenance et validations](1.18.0/provenance.json) et [sommes SHA-256](1.18.0/SHA256SUMS.txt).

Les SHA-256 ont été calculés **localement après téléchargement** ; ce ne sont pas des sommes publiées par Expert Sleepers. Toutes les entrées ZIP ont été lues sans erreur, et le manuel possède un en-tête `%PDF-1.7`.

Le paquet contient `MANIFEST.json`, les scripts `write_image_win.bat`, `write_image_mac.sh` et `write_image_lnx.sh`, `configs/write_parameters.json`, le chargeur `bootable_images/unsigned_MIMXRT1060_flashloader.bin` et l'image `bootable_images/disting_NT.bin`. Il est préparé pour MCUXpresso Secure Provisioning Tool **26.06** si cette méthode est utilisée.

## Installation avec NT Helper

Le fabricant recommande [NT Helper pour Windows/macOS/Linux](https://nosuch.dev/nt-helper/#firmware-update).

1. Sauvegarder la carte MicroSD, les presets et les plug-ins avant la mise à jour.
2. Brancher le module en USB et ouvrir NT Helper sur l'ordinateur.
3. Ouvrir **Firmware** dans le menu, ou cliquer sur le numéro de firmware.
4. Choisir le canal **stable**, puis **1.18.0**, ou sélectionner le paquet local téléchargé.
5. Suivre la confirmation de passage en bootloader et de flash. NT Helper peut automatiser le bootloader à partir du firmware 1.15 ; les versions antérieures affichent les instructions manuelles.
6. Après redémarrage, vérifier la version 1.18.0 et tester le chargement des presets et de SmearNT sur le module.

**Aucun flash ni déploiement matériel n'a été exécuté dans cette session.** La version actuellement installée sur le module n'a pas été interrogée.

## Compatibilité du SDK

SmearNT conserve l'en-tête officiel **API v13** épinglé au commit `f440d20`, et requiert le firmware **1.15.0 ou supérieur**. Le firmware 1.18.0 est la cible stable proposée pour la validation matérielle.

L'API upstream **v14** a été ajoutée le **29 septembre 2026** dans le [commit officiel f8b7d69](https://github.com/expertsleepersltd/distingNT_API/commit/f8b7d69b629f5bf40ee14979bcda7e50a31111e7), après la publication du firmware stable 1.18.0. Son firmware stable minimal n'est pas confirmé par l'historique du fabricant. Ses nouvelles fonctions de paramètres texte ne sont pas utilisées par SmearNT ; le projet reste donc sur API v13 pour préserver sa compatibilité documentée.
