"""Generate the 16 native disting NT SmearNT showcase presets (stdlib only)."""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]
BANK = ROOT / "presets" / "SmearNT16"

# name, Frequency, Resonance, Stages, Distribution, Feedback, explanation, source
PRESETS = [
    ("Phase 04", 50, 30, 0, 50, 0, "Rotation de phase apres 4 sections.", "Kick ou percussion seche"),
    ("Phase 08", 50, 30, 20, 50, 0, "8 sections : deuxieme degre de dispersion.", "Meme boucle que 01"),
    ("Phase 12", 50, 30, 40, 50, 0, "12 sections : comparer la forme des attaques.", "Meme boucle que 01"),
    ("Phase 16", 50, 30, 60, 50, 0, "16 sections : dispersion plus etendue.", "Meme boucle que 01"),
    ("Phase 20", 50, 30, 80, 50, 0, "20 sections : approche du reseau complet.", "Meme boucle que 01"),
    ("Phase 24", 50, 30, 100, 50, 0, "24 sections : reseau complet sans feedback.", "Meme boucle que 01"),
    ("Lineaire bas", 15, 60, 100, 0, 0, "Distribution lineaire deplacee vers le bas.", "Batterie large bande"),
    ("Log bas", 15, 60, 100, 50, 0, "Distribution logarithmique : davantage de poles dans le bas.", "Meme boucle que 07"),
    ("Groupes bas", 15, 60, 100, 100, 0, "Poles regroupes dans plusieurs zones.", "Meme boucle que 07"),
    ("Phase grave", 0, 60, 100, 50, 0, "Deplacement bas maximal, avec limite basse a 20 Hz.", "Kick ou basse percussive"),
    ("Phase aigu", 85, 60, 100, 50, 0, "Deplacement logarithmique vers le haut.", "Hi-hats, shaker ou bruit"),
    ("Couleur 04 08", 50, 30, 10, 50, 0, "Melange egal des sorties 4 et 8 sections.", "Accord tenu ou bruit rose"),
    ("Couleur 12 16", 50, 30, 50, 50, 0, "Melange egal des sorties 12 et 16 sections.", "Meme source que 12"),
    ("Phase resonante", 50, 100, 100, 50, 0, "Rayon des poles maximal, sans boucle de feedback.", "Percussions espacees"),
    ("Feedback 50", 50, 100, 100, 50, 50, "Reseau de 14 avec feedback interne a 0,3.", "Pluck ou percussion espacee"),
    ("Feedback 100", 50, 100, 100, 50, 100, "Feedback interne maximal a 0,6 et saturation de la boucle.", "Meme source que 15"),
]


def native_preset(index, row):
    name, frequency, resonance, stages, distribution, feedback, _, _ = row
    return {
        "kind": "disting NT preset",
        "author": "SmearNT",
        "engine": {"sampleRate": 48000, "blockSize": 24},
        "version": 2,
        "name": f"SMNT {index:02d} {name}",
        "slots": [{
            "guid": "SMNT",
            "specs": [0, 0, 0],
            "name": "SmearNT",
            # The single host Bypass precedes the 11 plug-in parameters.
            "parameters": [0, 1, 2, 13, 1, 14, 1,
                           frequency, resonance, stages, distribution, feedback],
            "ui": {"page": 0, "items": [0] * 16, "display": "params"},
        }],
        "ui": {"currentSlot": 0, "displayMode": "algorithm"},
    }


def main():
    assert len(PRESETS) == 16
    BANK.mkdir(parents=True, exist_ok=True)
    for index, row in enumerate(PRESETS, 1):
        path = BANK / f"{index:02d}_{row[0].replace(' ', '_')}.json"
        document = native_preset(index, row)
        assert len(document["name"]) <= 31 and document["name"].isascii()
        assert all(type(value) is int and 0 <= value <= 100 for value in row[1:6])
        path.write_text(json.dumps(document, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
    print(f"Generated 16 native NT presets in {BANK}")


if __name__ == "__main__":
    main()
