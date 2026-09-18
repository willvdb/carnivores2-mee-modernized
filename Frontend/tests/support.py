from pathlib import Path


def game(parent, name='Game'):
    root = Path(parent) / name
    for part in ('HUNTDAT/MENU/TXT', 'HUNTDAT/MENU/PICS', 'HUNTDAT/AREAS'):
        (root / part).mkdir(parents=True)
    (root / 'HUNTDAT/_RES.TXT').write_text("characters {\n{\n name = 'Synthetic animal'\n ai = 10\n}\n}\n")
    (root / 'HUNTDAT/AREAS/AREA1.MAP').write_bytes(b'synthetic map evidence')
    (root / 'HUNTDAT/AREAS/AREA1.RSC').write_bytes(b'synthetic resource evidence')
    (root / 'CARN2.EXE').write_bytes(b'synthetic executable evidence - never run')
    return root
