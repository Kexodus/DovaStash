"""Self-tests of the data checker's rules that need no game files (`esm_dump.py check`; tools/README.md).

Run: python -I tools/tests/test_esm_dump_check.py (ctest --preset core-tests runs it too, as esm_dump_check).
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import esm_dump as E  # noqa: E402


def template(cell, cells=(), replaces=()):
    return {"$schema": "dovastash-layout-1", "name": "Home", "cell": cell, "cells": list(cells), "replaces": list(replaces)}


def conflicts(files):
    docs = [(Path(f), doc, None) for f, doc in files]
    results = [{"errors": [], "warnings": []} for _ in files]
    E.shipped_template_conflicts(docs, results)
    return [r["errors"] for r in results]


class ShippedTemplateConflicts(unittest.TestCase):
    def test_two_shipped_templates_share_a_cell(self):
        errors = conflicts([("dist/Templates/Vanilla/Lakeview.json", template("HearthFires.esm|0x00002FEC", ["HearthFires.esm|0x00004849"])),
                            ("dist/Templates/Vanilla/LakeviewCellar.json", template("HearthFires.esm|0x00004849"))])
        self.assertEqual(errors[0], [])
        self.assertEqual(len(errors[1]), 1)
        self.assertIn("a home ships as one template", errors[1][0])

    def test_player_templates_may_share_a_cell(self):
        errors = conflicts([("dist/Templates/Vanilla/Breezehome.json", template("Skyrim.esm|0x000165A8")),
                            ("Data/Templates/User/My Breezehome.json", template("Skyrim.esm|0x000165A8"))])
        self.assertEqual(errors, [[], []])

    def test_replaces_a_file_that_still_ships(self):
        errors = conflicts([("dist/Templates/Vanilla/Lakeview.json",
                             template("HearthFires.esm|0x00002FEC", replaces=["Templates/Vanilla/LakeviewCellar.json"])),
                            ("dist/Templates/Vanilla/LakeviewCellar.json", template("HearthFires.esm|0x00004849"))])
        self.assertEqual(errors[0], ["replaces Templates/Vanilla/LakeviewCellar.json, which still ships"])
        self.assertEqual(errors[1], [])

    def test_replaces_a_retired_file(self):
        errors = conflicts([("dist/Templates/Vanilla/Lakeview.json",
                             template("HearthFires.esm|0x00002FEC", replaces=["Templates/Vanilla/LakeviewCellar.json"]))])
        self.assertEqual(errors, [[]])


class LabelForm(unittest.TestCase):
    def test_accepted(self):
        for label in ("Chest", "Chest (by the forge)", "Chest (by the forge, 2)", "Large kitchen sack (second)",
                      "Main hall chest (east)", "Main hall safe (back room)", "Laboratory chest (upgrade)", "Food barrel"):
            self.assertIsNone(E.label_problem(label), label)
        self.assertIsNone(E.label_problem("Bookshelf (Niflholm Mage Hall)", {"Niflholm Mage Hall"}))

    def test_contents_in_parentheses(self):
        self.assertIn("what it holds", E.label_problem("Barrel (food)"))
        self.assertIn("what it holds", E.label_problem("Ingredients cupboard (alchemy corner)"))

    def test_word_ordinal_after_a_comma(self):
        self.assertIn('", 2"', E.label_problem("Main hall display wardrobe (back room, second)"))
        self.assertIn('", 3"', E.label_problem("Chest (by the forge, third)"))

    def test_long_place(self):
        self.assertIn("long for a place", E.label_problem("Chest (in the far corner of the hall)"))

    def test_repeat_without_its_first(self):
        def misses(*labels):
            return E.orphan_ordinals([(f"containers[{i}]", label) for i, label in enumerate(labels)])
        self.assertEqual(misses("Chest", "Chest (second)", "Chest (by the table)", "Chest (by the table, 2)", "Chest (by the table, 3)"), [])
        self.assertEqual(misses("Chest (by the second bed)", "Main hall chest (east)"), [])
        self.assertEqual(len(misses("Wardrobe (second)")), 1)
        self.assertIn("'Wardrobe'", misses("Wardrobe (second)")[0])
        self.assertIn("'Chest (by the table)'", misses("Chest (by the table, 2)")[0])
        self.assertIn("'Chest (by the table, 2)'", misses("Chest (by the table)", "Chest (by the table, 3)")[0])
        self.assertIn("'Chest (second)'", misses("Chest", "Chest (third)")[0])


class FakeRec:
    def __init__(self, persistent=False):
        self.flags = E.REC_FLAG_PERSISTENT if persistent else 0


class FakeLoadOrder:
    def __init__(self, cells):
        self.cells = cells  # cell key -> {ref key: (FakeRec, None)}

    def cell_refs(self, cell):
        return self.cells.get(E.kl(cell), {})


class CatchAll(unittest.TestCase):
    """The master only in reach in its cell: each other area needs its own master in reach (DESIGN 6.7)."""

    MAIN, FOYER, CELLAR = ("Home.esp", 0x10), ("Home.esp", 0x20), ("Home.esp", 0x30)
    MASTER, URN, CHEST, PERSISTENT = ("Home.esp", 0x100), ("Home.esp", 0x200), ("Home.esp", 0x300), ("Home.esp", 0x400)

    def setUp(self):
        self.decode = E.decode_ref
        E.decode_ref = lambda _lo, _rec, _types, _refs: {"baseType": "CONT", "deleted": False}

    def tearDown(self):
        E.decode_ref = self.decode

    def run_check(self, area_masters, foyer_refs=None, persistent=()):
        refs = {E.kl(self.MASTER): (FakeRec(), None), E.kl(self.URN): (FakeRec(), None), E.kl(self.CHEST): (FakeRec(), None),
                E.kl(self.PERSISTENT): (FakeRec(True), None)}
        for r in persistent:
            refs[E.kl(r)] = (FakeRec(True), None)
        ref_cell = {E.kl(self.MASTER): self.MAIN, E.kl(self.URN): self.FOYER, E.kl(self.CHEST): self.CELLAR,
                    E.kl(self.PERSISTENT): self.CELLAR}
        lo = FakeLoadOrder({E.kl(self.FOYER): {E.kl(self.URN): refs[E.kl(self.URN)]} if foyer_refs is None else foyer_refs,
                            E.kl(self.CELLAR): {E.kl(self.CHEST): refs[E.kl(self.CHEST)]}})
        names = {E.kl(self.FOYER): "cells[0] (Foyer)", E.kl(self.CELLAR): "cells[1] (Cellar)"}
        warnings, info = [], {}
        E.catch_all_warnings(lo, [self.MAIN, self.FOYER, self.CELLAR], self.MAIN, ref_cell, refs, self.MASTER, area_masters, names,
                             warnings, info)
        return warnings, info

    def test_area_without_master_is_a_warning(self):
        warnings, _info = self.run_check([("cells[1].master", self.CHEST, self.CELLAR)])
        self.assertEqual(len(warnings), 1)
        self.assertTrue(warnings[0].startswith("cells[0] (Foyer): no master in reach there"))
        self.assertIn('Add "master" to this entry', warnings[0])

    def test_every_area_with_its_master(self):
        warnings, _info = self.run_check([("cells[0].master", self.URN, self.FOYER), ("cells[1].master", self.CHEST, self.CELLAR)])
        self.assertEqual(warnings, [])

    def test_a_persistent_area_master_reaches_every_area(self):
        warnings, _info = self.run_check([("cells[1].master", self.PERSISTENT, self.CELLAR)])
        self.assertEqual(warnings, [])

    def test_an_area_without_containers_is_info(self):
        warnings, info = self.run_check([("cells[1].master", self.CHEST, self.CELLAR)], foyer_refs={})
        self.assertEqual(warnings, [])
        self.assertEqual(info["areasWithoutContainers"], ["cells[0] (Foyer)"])


if __name__ == "__main__":
    unittest.main()
