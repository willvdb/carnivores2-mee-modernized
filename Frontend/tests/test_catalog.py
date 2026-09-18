from pathlib import Path
import tempfile
import unittest

from lodge.catalog import parse_script, project
from support import game

SCRIPT = """weapons {
{
 name = 'Test weapon'
}
}
characters {
{
 name = 'Small Herbivores'
 ai = 10
 addition area1 {
  name = 'Do not replace the license'
 }
}
{
 name = ''
 ai = 10
}
{
 name = 'Uncheck this to blue'
 ai = 11
}
}
prices {
 start = 0
 area = 20
 area = 999999
 dino = 10
 dino = 15
 dino = 30
 dino = 99
 weapon = 0
 weapon = 999
 acces = 15
 acces = 135
}
.
"""


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = game(self.temp.name)
        (self.root / 'HUNTDAT/_MENU.TXT').write_text(SCRIPT)
        (self.root / 'HUNTDAT/MENU/TXT/AREA1.TXT').write_text('\nAn intentionally blank name\n')

    def test_order_groups_duplicates_labels_and_surplus_prices(self):
        catalog = project(self.root)
        self.assertEqual([e['label'] for e in catalog['licenses']], ['Small Herbivores', '', 'Uncheck this to blue'])
        self.assertEqual(len({e['id'] for e in catalog['licenses']}), 3)
        self.assertTrue(all(e['kind'] == 'license' for e in catalog['licenses']))
        self.assertEqual(catalog['licenses'][0]['price'], 10)
        self.assertEqual(catalog['weapons'][0]['price'], 0)
        codes = [d['code'] for d in catalog['diagnostics']]
        self.assertEqual(codes.count('surplus-price'), 2)
        self.assertIn('duplicate-ai', codes)
        self.assertEqual(catalog['starting_score_observations'][0]['raw'], '0')
        self.assertGreater(catalog['licenses'][0]['line'], 1)
        self.assertEqual(catalog['licenses'][0]['source'], 'HUNTDAT/_MENU.TXT')

    def test_physical_maps_do_not_add_advertised_hunts(self):
        (self.root / 'HUNTDAT/AREAS/AREA9.MAP').write_bytes(b'spare')
        (self.root / 'HUNTDAT/AREAS/AREA9.RSC').write_bytes(b'spare')
        catalog = project(self.root)
        self.assertEqual(len(catalog['areas']), 2)
        self.assertEqual(len(catalog['physical_maps']), 2)
        self.assertEqual(catalog['areas'][0]['label'], '')
        self.assertEqual(catalog['areas'][0]['launch_stem'], 'area1')
        self.assertIsNone(catalog['areas'][1]['launch_stem'])

    def test_dialects_and_equipment_description_ambiguity(self):
        (self.root / 'HUNTDAT/_RES.TXT').write_text('hunterinfo {\n}\nmapambients {\n}\n')
        (self.root / 'HUNTDAT/MENU/TXT/RADAR.NFO').write_text('Old explanation')
        (self.root / 'HUNTDAT/MENU/TXT/EQUIP2.NFO').write_text('Different explanation')
        catalog = project(self.root)
        self.assertEqual(catalog['dialect']['observed'], 'mee-older')
        self.assertEqual(catalog['capabilities']['modern_engine_compatibility'], 'known-dispatch-gap')
        self.assertIn('description-conflict', [d['code'] for d in catalog['diagnostics']])
        self.assertEqual(len(catalog['equipment']), 2)
        self.assertEqual(project(self.root, 'iceage-triassic')['dialect']['effective'], 'iceage-triassic')
        self.assertIn('dialect-conflict', [d['code'] for d in project(self.root, 'mee-newer')['diagnostics']])

    def test_quote_comments_nested_blocks_and_duplicate_assignments(self):
        parsed = parse_script(b"characters\n{\n{\n name='// literal { text }' // comment\n ai=10\n ai=11\n}\n}\n", 'synthetic')
        node = parsed['tree']['children'][0]['children'][0]
        self.assertEqual(len(node['attributes']), 3)
        self.assertEqual(node['attributes'][0]['raw'], "'// literal { text }'")
        broken = parse_script(b'characters {\n{\n', 'broken')
        self.assertEqual(broken['diagnostics'][0]['code'], 'unclosed-block')


if __name__ == '__main__':
    unittest.main()
