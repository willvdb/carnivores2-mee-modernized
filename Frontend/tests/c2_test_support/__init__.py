"""Authored shared fixtures; no test discovery or native-generated expectations."""
import struct
from pathlib import Path


def game(parent, name='Game'):
    root = Path(parent).resolve() / name
    for part in ('HUNTDAT/MENU/TXT', 'HUNTDAT/MENU/PICS', 'HUNTDAT/AREAS'):
        (root / part).mkdir(parents=True)
    (root / 'HUNTDAT/_RES.TXT').write_text("weapons {\n}\ncharacters {\n{\n name = 'Synthetic animal'\n ai = 10\n}\n}\n")
    (root / 'HUNTDAT/AREAS/AREA1.MAP').write_bytes(b'synthetic map evidence')
    (root / 'HUNTDAT/AREAS/AREA1.RSC').write_bytes(b'synthetic resource evidence')
    (root / 'CARN2.EXE').write_bytes(b'synthetic executable evidence - never run')
    return root


def save_bytes(slot=0, score=100):
    # Original synthetic bytes, including noncanonical options and opaque words.
    result = bytearray((i * 73 + 19) % 256 for i in range(1660))
    result[:128] = b'Test hunter\0' + bytes(range(116))
    struct.pack_into('<iii', result, 128, slot, score, 1000)
    return bytes(result)


def room_bytes():
    return bytes((i * 37) % 256 for i in range(7176))


SCRIPT = """weapons {
{
 name = 'Synthetic weapon'
}
}
characters {
{
 name = 'Synthetic group'
 ai = 10
}
}
prices {
 area = 5
 dino = 10
 weapon = 20
}
"""


def selection():
    return {'area': 'areas:0', 'mode': 'hunt', 'time_of_day': 1,
            'licenses': ['licenses:0'], 'weapons': ['weapons:0'], 'equipment': []}
