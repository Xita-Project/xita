"""Synthetic bounds and revision guards for the observed D3D callback walk."""
import hashlib
import struct
import unittest
from unittest.mock import patch
from games.halo2_5849 import prepare_boot


class SyntheticImage:
    def __init__(self):
        self.code = b"synthetic caller"
        self.targets = {slot: 0x1000 + index * 16
                        for index, slot in enumerate(range(0x403AF8, 0x403B70, 4))}
        self.targets[0x403B40] = None  # skipped slot is never inspected as a root
        self.bad_code = None
        self.section_name = "D3D"

    def bytes_at(self, address, length):
        assert address == 0x200 and length == len(self.code)
        return self.code

    def u32(self, slot):
        return self.targets[slot]

    def is_code(self, target):
        return target != self.bad_code

    def section_of(self, target):
        return (0, 0, 0, 0, self.section_name)


class CallbackRoots(unittest.TestCase):
    def test_arena_boot_forwarding_slots_and_guards(self):
        image = SyntheticImage(); image.section_name = ".text"
        image.targets = {0x4576AC: 0x1000, 0x4576B4: 0x1010,
                         0x4576A8: None, 0x4576B0: None, 0x4576B8: None}
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "GAME_ARENA_BOOT_CALLS", (spec,) * 3):
            before = dict(image.targets)
            self.assertEqual(prepare_boot.game_arena_boot_roots(image), {0x1000, 0x1010})
            self.assertEqual(image.targets, before)
            for slot in (0x4576AC, 0x4576B4):
                saved = image.targets[slot]
                for bad in (0, None, 0xDEAD):
                    image.targets[slot] = image.bad_code = bad
                    with self.assertRaisesRegex(ValueError, "title code"):
                        prepare_boot.game_arena_boot_roots(image)
                image.targets[slot] = saved; image.bad_code = None
            image.section_name = "DSOUND"
            with self.assertRaisesRegex(ValueError, "title code"):
                prepare_boot.game_arena_boot_roots(image)
        for which in range(3):
            specs = [spec] * 3; specs[which] = (*spec[:2], "0" * 64)
            with patch.object(prepare_boot, "GAME_ARENA_BOOT_CALLS", specs):
                with self.assertRaisesRegex(ValueError, "fingerprint"):
                    prepare_boot.game_arena_boot_roots(image)

    def test_fixed_startup_pair_and_allocator_slots_only(self):
        image = SyntheticImage(); image.section_name = ".text"
        slots = (0x461DF0, 0x461DF4, 0x45379C, 0x4537AC)
        image.targets = {slot: 0x1000 + n * 16 for n, slot in enumerate(slots)}
        image.targets[0x4798B0] = 0x45378C
        for neighbor in (0x461DEC, 0x461DF8, 0x45378C, 0x453798, 0x4537A0, 0x4537A8, 0x4537B0):
            image.targets[neighbor] = None
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "GAME_FIXED_STARTUP_CALLS", (spec,) * 5):
            before = dict(image.targets)
            self.assertEqual(prepare_boot.game_fixed_startup_roots(image), set(range(0x1000, 0x1040, 16)))
            self.assertEqual(image.targets, before)
            for slot in slots:
                saved = image.targets[slot]
                for bad in (0, None, 0xDEAD):
                    image.targets[slot] = image.bad_code = bad
                    with self.assertRaisesRegex(ValueError, "title code"):
                        prepare_boot.game_fixed_startup_roots(image)
                image.targets[slot] = saved; image.bad_code = None
            image.section_name = "DSOUND"
            with self.assertRaisesRegex(ValueError, "title code"):
                prepare_boot.game_fixed_startup_roots(image)
            image.section_name = ".text"
            for bad in (0, None, 0x453790):
                image.targets[0x4798B0] = bad
                with self.assertRaisesRegex(ValueError, "binding mismatch"):
                    prepare_boot.game_fixed_startup_roots(image)
        for which in range(5):
            specs = [spec] * 5; specs[which] = (*spec[:2], "0" * 64)
            with patch.object(prepare_boot, "GAME_FIXED_STARTUP_CALLS", specs):
                with self.assertRaisesRegex(ValueError, "fingerprint"):
                    prepare_boot.game_fixed_startup_roots(image)

    def test_packed_vector_observed_rows_both_triplets_only(self):
        image = SyntheticImage(); image.section_name = ".text"
        bases = (0x47FB4C, 0x47FBC4, 0x47FC14)
        slots = [base + n * 4 for base in bases for n in range(6)]
        image.targets = {slot: 0x1000 + (n % 6) * 16 for n, slot in enumerate(slots)}
        for base in bases:
            image.targets[base - 4] = None; image.targets[base + 24] = None
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "GAME_PACKED_VECTOR_BINDINGS", (spec, spec)):
            before = dict(image.targets)
            self.assertEqual(prepare_boot.game_packed_vector_roots(image), set(range(0x1000, 0x1060, 16)))
            self.assertEqual(image.targets, before)
            for slot in slots:
                saved = image.targets[slot]
                for invalid in (0, None):
                    image.targets[slot] = invalid
                    with self.assertRaisesRegex(ValueError, "invalid target"):
                        prepare_boot.game_packed_vector_roots(image)
                image.targets[slot] = saved
            image.bad_code = 0x1020
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_packed_vector_roots(image)
            image.bad_code = None; image.section_name = "D3D"
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_packed_vector_roots(image)
        for which in range(2):
            guards = [spec, spec]; guards[which] = (*spec[:2], "0" * 64)
            with patch.object(prepare_boot, "GAME_PACKED_VECTOR_BINDINGS", guards):
                with self.assertRaisesRegex(ValueError, "fingerprint"):
                    prepare_boot.game_packed_vector_roots(image)

    def test_widget_property_dispatch_bounds_nulls_and_revision(self):
        image = SyntheticImage(); image.section_name = ".text"
        image.targets = {slot: 0x1000 + n * 16 for n, slot in enumerate(range(0x470828, 0x4709E8, 4))}
        image.targets[0x470830] = 0
        expected = set(image.targets.values()) - {0}
        image.targets[0x470824] = None; image.targets[0x4709E8] = None
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "GAME_WIDGET_PROPERTY_DISPATCH", spec):
            self.assertEqual(prepare_boot.game_widget_property_roots(image), expected)
            for slot in (0x470828, 0x470888, 0x4709E4):
                saved = image.targets[slot]; image.targets[slot] = None
                with self.assertRaisesRegex(ValueError, "invalid target"):
                    prepare_boot.game_widget_property_roots(image)
                image.targets[slot] = saved
            image.bad_code = image.targets[0x4709E4]
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_widget_property_roots(image)
            image.bad_code = None; image.section_name = "D3D"
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_widget_property_roots(image)
            image.section_name = ".text"; image.code = b"x" * len(image.code)
            with self.assertRaisesRegex(ValueError, "fingerprint"):
                prepare_boot.game_widget_property_roots(image)

    def test_text_widget_and_embedded_member_prefixes(self):
        image = SyntheticImage(); image.section_name = ".text"
        spans = ((0x458940, 0x458984), (0x4588B0, 0x4588BC), (0x458930, 0x45893C))
        image.targets = {slot: 0x1000 + slot for start, end in spans for slot in range(start, end, 4)}
        expected = set(image.targets.values())
        for _, end in spans:
            image.targets[end] = 0
            if end + 4 not in image.targets:
                image.targets[end + 4] = None
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "GAME_TEXT_WIDGET_CONSTRUCTORS", (spec,) * 4):
            self.assertEqual(prepare_boot.game_text_widget_vtable_roots(image), expected)
            for start, end in spans:
                saved = image.targets[start]; image.targets[start] = None
                with self.assertRaisesRegex(ValueError, "invalid target"):
                    prepare_boot.game_text_widget_vtable_roots(image)
                image.targets[start] = saved; image.targets[end] = 0x123456
                with self.assertRaisesRegex(ValueError, "boundary"):
                    prepare_boot.game_text_widget_vtable_roots(image)
                image.targets[end] = 0
            image.code = b"x" * len(image.code)
            with self.assertRaisesRegex(ValueError, "fingerprint"):
                prepare_boot.game_text_widget_vtable_roots(image)

    def test_startup_widget_constructor_and_exact_vtable_bounds(self):
        image = SyntheticImage(); image.section_name = ".text"
        image.targets = {slot: 0x1000 + n * 16 for n, slot in enumerate(range(0x45BC60, 0x45BCD0, 4))}
        expected = set(image.targets.values())
        image.targets[0x45BC5C] = None; image.targets[0x45BCD0] = None
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "GAME_STARTUP_WIDGET_CONSTRUCTORS", (spec, spec)):
            self.assertEqual(prepare_boot.game_startup_widget_vtable_roots(image), expected)
            for slot in (0x45BC60, 0x45BCA8, 0x45BCCC):
                saved = image.targets[slot]
                for invalid in (0, None):
                    image.targets[slot] = invalid
                    with self.assertRaisesRegex(ValueError, "invalid target"):
                        prepare_boot.game_startup_widget_vtable_roots(image)
                image.targets[slot] = saved
            image.section_name = "DSOUND"
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_startup_widget_vtable_roots(image)
            image.section_name = ".text"; image.code = b"x" * len(image.code)
            with self.assertRaisesRegex(ValueError, "fingerprint"):
                prepare_boot.game_startup_widget_vtable_roots(image)

    def test_game_sound_owner_bindings_and_bounded_tables(self):
        image = SyntheticImage(); image.section_name = ".text"
        image.targets = {slot: 0x1000 + n * 16 for n, slot in enumerate(range(0x200, 0x224, 4))}
        expected = set(image.targets.values()); image.targets[0x100] = 0x200
        image.code = bytes(36)
        guard = ((0x100, 0x200, 36, hashlib.sha256(image.code).hexdigest()),)
        with patch.object(prepare_boot, "GAME_SOUND_VTABLES", guard):
            self.assertEqual(prepare_boot.game_sound_owner_roots(image), expected)
            image.targets[0x100] = 0x204
            with self.assertRaisesRegex(ValueError, "binding"):
                prepare_boot.game_sound_owner_roots(image)
            image.targets[0x100] = 0x200; image.section_name = "DSOUND"
            with self.assertRaisesRegex(ValueError, "title code"):
                prepare_boot.game_sound_owner_roots(image)
            image.section_name = ".text"; image.bad_code = 0x1020
            with self.assertRaisesRegex(ValueError, "title code"):
                prepare_boot.game_sound_owner_roots(image)
            image.bad_code = None; image.targets[0x220] = 0
            with self.assertRaisesRegex(ValueError, "title code"):
                prepare_boot.game_sound_owner_roots(image)
            image.code = bytes([1]) * 36
            with self.assertRaisesRegex(ValueError, "fingerprint"):
                prepare_boot.game_sound_owner_roots(image)

    def test_stream_interface_bounds_and_revision(self):
        image = SyntheticImage(); image.section_name = "DSOUND"
        image.targets = {slot: 0x1000 + n * 16 for n, slot in enumerate(range(0x200, 0x21C, 4))}
        image.code = bytes(28)
        guard = (0x200, 28, hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "STREAM_VTABLE", guard):
            self.assertEqual(prepare_boot.audio_stream_roots(image), set(image.targets.values()))
            image.section_name = ".text"
            with self.assertRaisesRegex(ValueError, "DSOUND code"):
                prepare_boot.audio_stream_roots(image)
            image.section_name = "DSOUND"; image.bad_code = 0x1030
            with self.assertRaisesRegex(ValueError, "DSOUND code"):
                prepare_boot.audio_stream_roots(image)
            image.bad_code = None; image.targets[0x204] = 0
            with self.assertRaisesRegex(ValueError, "DSOUND code"):
                prepare_boot.audio_stream_roots(image)
            image.code = bytes([1]) * 28
            with self.assertRaisesRegex(ValueError, "fingerprint"):
                prepare_boot.audio_stream_roots(image)

    def test_resource_lifecycle_three_record_walks(self):
        image = SyntheticImage(); image.section_name = ".text"
        bases = (0x4674A4, 0x4674A8, 0x4674B8)
        image.targets = {base + n * 0x38: 0x1000 + (n * 3 + phase) * 16
                         for phase, base in enumerate(bases) for n in range(3)}
        expected = set(image.targets.values())
        for base in bases:
            image.targets[base + 3 * 0x38] = None
        for n in range(3):
            image.targets[0x467498 + n * 0x38] = None  # metadata, never code
            image.targets[0x4674AC + n * 0x38] = None  # unreviewed field
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "GAME_RESOURCE_WALKS", (spec,) * 3):
            self.assertEqual(prepare_boot.game_resource_callback_roots(image), expected)
            for base in bases:
                last = base + 2 * 0x38; saved = image.targets[last]
                image.targets[last] = 0
                self.assertEqual(prepare_boot.game_resource_callback_roots(image), expected - {saved})
                for bad in (None, 0xDEAD):
                    image.targets[last] = image.bad_code = bad
                    with self.assertRaisesRegex(ValueError, "invalid target"):
                        prepare_boot.game_resource_callback_roots(image)
                image.targets[last] = saved; image.bad_code = None
            image.section_name = "DATA"
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_resource_callback_roots(image)
        for which in range(3):
            specs = [spec] * 3; specs[which] = (0x200, len(image.code), "0" * 64)
            with patch.object(prepare_boot, "GAME_RESOURCE_WALKS", specs):
                with self.assertRaisesRegex(ValueError, "fingerprint"):
                    prepare_boot.game_resource_callback_roots(image)

    def test_map_lifecycle_fields_bounds_nulls_and_revision(self):
        image = SyntheticImage(); image.section_name = ".text"
        bases = (0x440DE0, 0x440DE4, 0x440DE8, 0x440DEC)
        image.targets = {base + n * 0x24: 0x1000 + ((n + phase) % 47) * 16
                         for phase, base in enumerate(bases) for n in range(68)}
        for base in bases:
            image.targets[base] = 0
        # Adjacent descriptor fields and the next record are never inspected.
        for n in range(69):
            for field in (0x440DD8, 0x440DDC, 0x440DF0):
                image.targets[field + n * 0x24] = None
        for base in bases:
            image.targets[base + 68 * 0x24] = None
        expected = {t for t in image.targets.values() if t}
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "GAME_MAP_WALKS", (spec,) * 4):
            self.assertEqual(prepare_boot.game_map_callback_roots(image), expected)
            for base in bases:
                last = base + 67 * 0x24; saved = image.targets[last]
                for bad in (None, 0xDEAD):
                    image.targets[last] = bad; image.bad_code = bad
                    with self.assertRaisesRegex(ValueError, "invalid target"):
                        prepare_boot.game_map_callback_roots(image)
                image.targets[last] = saved; image.bad_code = None
            image.section_name = "DATA"
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_map_callback_roots(image)
        for which in range(4):
            specs = [spec] * 4; specs[which] = (0x200, len(image.code), "0" * 64)
            with patch.object(prepare_boot, "GAME_MAP_WALKS", specs):
                with self.assertRaisesRegex(ValueError, "fingerprint"):
                    prepare_boot.game_map_callback_roots(image)

    def test_remaining_lifecycle_fields_required_disposal_and_guards(self):
        image = SyntheticImage(); image.section_name = ".text"
        bases = (0x440DDC, 0x440DF0, 0x440DF4, 0x440DF8)
        image.targets = {base + n * 0x24: 0x1000 + ((n + phase) % 47) * 16
                         for phase, base in enumerate(bases) for n in range(68)}
        expected = set(image.targets.values())
        # Every unselected field and record69 is poisoned, never scanned.
        for n in range(69):
            for field in (0x440DD8, 0x440DE0, 0x440DE4, 0x440DE8, 0x440DEC):
                image.targets[field + n * 0x24] = None
        for base in bases: image.targets[base + 68 * 0x24] = None
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "GAME_REMAINING_LIFECYCLE_WALKS", (spec,) * 3):
            before = dict(image.targets)
            self.assertEqual(prepare_boot.game_remaining_lifecycle_roots(image), expected)
            self.assertEqual(image.targets, before)
            for base in bases:
                for n in range(68):
                    slot = base + n * 0x24; saved = image.targets[slot]
                    image.targets[slot] = 0
                    if base == 0x440DDC:
                        with self.assertRaisesRegex(ValueError, "invalid target"):
                            prepare_boot.game_remaining_lifecycle_roots(image)
                    else:
                        prepare_boot.game_remaining_lifecycle_roots(image)
                    image.targets[slot] = saved
                last = base + 67 * 0x24; saved = image.targets[last]
                for bad in (None, 0xDEAD):
                    image.targets[last] = bad; image.bad_code = bad
                    with self.assertRaisesRegex(ValueError, "invalid target"):
                        prepare_boot.game_remaining_lifecycle_roots(image)
                image.targets[last] = saved; image.bad_code = None
            image.section_name = "DSOUND"
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_remaining_lifecycle_roots(image)
        for which in range(3):
            specs = [spec] * 3; specs[which] = (*spec[:2], "0" * 64)
            with patch.object(prepare_boot, "GAME_REMAINING_LIFECYCLE_WALKS", specs):
                with self.assertRaisesRegex(ValueError, "fingerprint"):
                    prepare_boot.game_remaining_lifecycle_roots(image)

    def test_mixed_bink_pixel_descriptor(self):
        image = SyntheticImage(); image.section_name = "BINK32"
        offsets = (*range(8, 0x34, 8), *range(0x74, 0xB4, 4))
        image.targets = {0x57A080 + off: 0x1000 + n * 16 for n, off in enumerate(offsets)}
        expected = set(image.targets.values()); self.assertEqual(len(expected), 22)
        for off, value in ((0, 4), (4, 2), (12, 2), (20, 3), (28, 2), (36, 2), (44, 3)):
            image.targets[0x57A080 + off] = value
        # Neither mutable counters nor following descriptor data are inspected.
        image.targets[0x57A0B4] = image.targets[0x57A0F0] = image.targets[0x57A134] = None
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "BINK_PIXEL_DISPATCH", (spec, spec)):
            self.assertEqual(prepare_boot.bink_pixel_callback_roots(image), expected)
            image.targets[0x57A130] = 0
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.bink_pixel_callback_roots(image)
            image.targets[0x57A130] = 0x1000 + 21 * 16
            image.section_name = "BINKDATA"
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.bink_pixel_callback_roots(image)
            image.section_name = "BINK32"; image.bad_code = image.targets[0x57A088]
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.bink_pixel_callback_roots(image)
            image.bad_code = None; image.targets[0x57A084] = 4
            with self.assertRaisesRegex(ValueError, "shape"):
                prepare_boot.bink_pixel_callback_roots(image)
        with patch.object(prepare_boot, "BINK_PIXEL_DISPATCH", ((0x200, len(image.code), "0" * 64),)):
            with self.assertRaisesRegex(ValueError, "fingerprint"):
                prepare_boot.bink_pixel_callback_roots(image)

    def test_two_static_online_interfaces(self):
        image = SyntheticImage(); image.section_name = ".text"
        image.targets = {slot: 0x1000 + n * 16
                         for n, slot in enumerate(range(0x450B44, 0x450B88, 4))}
        expected = set(image.targets.values())
        image.targets.update({0x47708C: 0x450B68, 0x47712C: 0x450B44})
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "GAME_ONLINE_INTERFACE_DISPATCH", spec):
            self.assertEqual(prepare_boot.game_online_interface_roots(image), expected)
            self.assertEqual(len(expected), 17)
            image.targets[0x47712C] = 0x450B48
            with self.assertRaisesRegex(ValueError, "binding .* mismatch"):
                prepare_boot.game_online_interface_roots(image)
            image.targets[0x47712C] = 0x450B44
            image.targets[0x450B84] = None
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_online_interface_roots(image)
        with patch.object(prepare_boot, "GAME_ONLINE_INTERFACE_DISPATCH", (0x200, len(image.code), "0" * 64)):
            with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
                prepare_boot.game_online_interface_roots(image)

    def test_eleven_state_interfaces_bounded_by_constructor(self):
        image = SyntheticImage(); image.section_name = ".text"
        image.targets = {slot: 0x1000 + n * 16
                         for n, slot in enumerate(range(0x450990, 0x450A40, 4))}
        spec = ((0x200, len(image.code), hashlib.sha256(image.code).hexdigest()),)
        with patch.object(prepare_boot, "GAME_STATE_CONSTRUCTORS", spec):
            self.assertEqual(prepare_boot.game_state_vtable_roots(image), set(image.targets.values()))
            self.assertEqual(len(image.targets), 44)
            image.targets[0x450A3C] = None
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_state_vtable_roots(image)
        with patch.object(prepare_boot, "GAME_STATE_CONSTRUCTORS", ((0x200, len(image.code), "0" * 64),)):
            with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
                prepare_boot.game_state_vtable_roots(image)

    def test_four_registered_interfaces_exact_bounds_and_bindings(self):
        image = SyntheticImage(); image.section_name = ".text"; image.targets = {}
        bindings = ((0x417370, 0x462E00, 0x417378), (0x4173E4, 0x462E10, 0x4173E8),
                    (0x41745C, 0x462E28, 0x417460), (0x4174D0, 0x462F30, 0x4174D8))
        expected = set()
        for number, (slot, instance, table) in enumerate(bindings):
            image.targets[slot] = instance; image.targets[instance] = table
            for index in range(27):
                target = 0x1000 + (number * 27 + index) * 16
                image.targets[table + 4 * index] = target; expected.add(target)
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "GAME_INTERFACE_REGISTRATION", spec):
            self.assertEqual(prepare_boot.game_registered_interface_roots(image), expected)
            image.targets[0x462F30] += 4
            with self.assertRaisesRegex(ValueError, "binding"):
                prepare_boot.game_registered_interface_roots(image)
            image.targets[0x462F30] -= 4; image.targets[0x417540] = None
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_registered_interface_roots(image)
        with patch.object(prepare_boot, "GAME_INTERFACE_REGISTRATION", (0x200, len(image.code), "0" * 64)):
            with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
                prepare_boot.game_registered_interface_roots(image)

    def test_allocator_vtable_bounds_and_revision(self):
        image = SyntheticImage(); image.section_name = ".text"
        image.targets = {slot: 0x1000 + n * 16
                         for n, slot in enumerate(range(0x454970, 0x454980, 4))}
        spec = ((0x200, len(image.code), hashlib.sha256(image.code).hexdigest()),)
        with patch.object(prepare_boot, "GAME_ALLOCATOR_CONSTRUCTORS", spec):
            self.assertEqual(prepare_boot.game_allocator_vtable_roots(image), set(image.targets.values()))
            image.targets[0x45497C] = None
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_allocator_vtable_roots(image)
        with patch.object(prepare_boot, "GAME_ALLOCATOR_CONSTRUCTORS", ((0x200, len(image.code), "0" * 64),)):
            with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
                prepare_boot.game_allocator_vtable_roots(image)

    def test_paired_mode_callbacks_and_null_skip(self):
        image = SyntheticImage(); image.section_name = ".text"
        image.targets = {slot: 0x1000 + n * 16
                         for n, slot in enumerate(range(0x453C00, 0x453C40, 4))}
        image.targets[0x453C08] = 0
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "GAME_MODE_WALK", spec):
            self.assertEqual(prepare_boot.game_mode_callback_roots(image), set(image.targets.values()) - {0})
            for invalid in (None, 0xDEAD):
                image.targets[0x453C3C] = invalid; image.bad_code = invalid
                with self.assertRaisesRegex(ValueError, "invalid target"):
                    prepare_boot.game_mode_callback_roots(image)
        with patch.object(prepare_boot, "GAME_MODE_WALK", (0x200, len(image.code), "0" * 64)):
            with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
                prepare_boot.game_mode_callback_roots(image)

    def test_constructor_bounded_dispatch_vtable(self):
        image = SyntheticImage(); image.section_name = ".text"
        image.targets = {slot: 0x1000 + n * 16
                         for n, slot in enumerate(range(0x4599A8, 0x4599DC, 4))}
        spec = ((0x200, len(image.code), hashlib.sha256(image.code).hexdigest()),)
        with patch.object(prepare_boot, "GAME_DISPATCH_CONSTRUCTORS", spec):
            self.assertEqual(prepare_boot.game_dispatch_vtable_roots(image), set(image.targets.values()))
            image.targets[0x4599D8] = None
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_dispatch_vtable_roots(image)
        with patch.object(prepare_boot, "GAME_DISPATCH_CONSTRUCTORS", ((0x200, len(image.code), "0" * 64),)):
            with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
                prepare_boot.game_dispatch_vtable_roots(image)

    def test_game_record_stride_and_bounds(self):
        image = SyntheticImage(); image.section_name = ".text"
        image.targets = {0x440DD8 + n * 0x24: 0x5000 + (n % 49) * 16 for n in range(68)}
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "GAME_INIT_WALK", spec):
            self.assertEqual(len(prepare_boot.game_initialization_roots(image)), 49)
            last = 0x440DD8 + 67 * 0x24
            for bad in (None, 0):
                image.targets[last] = bad
                with self.assertRaisesRegex(ValueError, "invalid target"):
                    prepare_boot.game_initialization_roots(image)
            image.targets[last] = image.bad_code = 0xDEAD
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_initialization_roots(image)
            image.bad_code = None; image.section_name = "DATA"
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.game_initialization_roots(image)
        with patch.object(prepare_boot, "GAME_INIT_WALK", (0x200, len(image.code), "0" * 64)):
            with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
                prepare_boot.game_initialization_roots(image)

    def test_xpp_two_level_walk(self):
        image = SyntheticImage()
        image.section_name = "XPP"
        image.targets = {slot: 0x2000 + n * 24
                         for n, slot in enumerate(range(0x4086D4, 0x4086EC, 4))}
        image.targets.update({0x2004 + n * 24: 0x5000 + (n % 3) * 16 for n in range(6)})
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "XPP_CALLBACK_WALK", spec):
            self.assertEqual(prepare_boot.host_device_callback_roots(image), {0x5000, 0x5010, 0x5020})
            image.targets[0x4086E8] = 0  # original walk skips null descriptors
            self.assertEqual(len(prepare_boot.host_device_callback_roots(image)), 3)
            image.targets[0x4086D4] = 0x2001
            with self.assertRaisesRegex(ValueError, "descriptor"):
                prepare_boot.host_device_callback_roots(image)
            image.targets[0x4086D4] = 0x2000
            image.targets[0x2004] = None
            with self.assertRaisesRegex(ValueError, "callback"):
                prepare_boot.host_device_callback_roots(image)
            image.targets[0x2004] = 0x5000; image.bad_code = 0x5000
            with self.assertRaisesRegex(ValueError, "callback"):
                prepare_boot.host_device_callback_roots(image)
        with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
            with patch.object(prepare_boot, "XPP_CALLBACK_WALK", (0x200, len(image.code), "0" * 64)):
                prepare_boot.host_device_callback_roots(image)

    def test_exact_walk_and_invalid_targets(self):
        image = SyntheticImage()
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "HOST_CALLBACK_WALK", spec):
            roots = prepare_boot.host_channel_callback_roots(image)
            self.assertEqual(len(roots), 29)
            self.assertIn(image.targets[0x403AF8], roots)
            self.assertIn(image.targets[0x403B6C], roots)
            for target in (None, 0):
                image.targets[0x403B6C] = target
                with self.assertRaisesRegex(ValueError, "invalid target"):
                    prepare_boot.host_channel_callback_roots(image)
            image.targets[0x403B6C] = 0xDEAD
            image.bad_code = 0xDEAD
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.host_channel_callback_roots(image)
            image.bad_code = None
            image.section_name = "DATA"
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.host_channel_callback_roots(image)

    def test_reject_changed_caller(self):
        image = SyntheticImage()
        with patch.object(prepare_boot, "HOST_CALLBACK_WALK", (0x200, len(image.code), "0" * 64)):
            with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
                prepare_boot.host_channel_callback_roots(image)


class SingletonImage:
    def __init__(self):
        self.code = b"synthetic complete registration and resolver"
        self.spec = (0x200, len(self.code), hashlib.sha256(self.code).hexdigest())
        self.registrations = [(*self.spec, node, storage, crt)
                              for _, _, _, node, storage, crt in prepare_boot.GAME_SINGLETON_REGISTRATIONS]
        self.words = {0x461E10: None}  # adjacent non-node metadata
        for n, (_, _, _, node, storage, crt) in enumerate(self.registrations):
            self.words.update({crt: 0x200, node: 0x1000 + n * 16, node + 4: 0, node + 8: storage})
        self.data_end = 0x467000; self.storage_end = 0x485000
        self.data_name = ".data"; self.storage_name = ".bss"; self.code_name = ".text"
        self.short_node = None; self.bad_code = None

    def bytes_at(self, address, size):
        if address == 0x200:
            assert size == len(self.code)
            return self.code
        assert size == 12
        return bytes(11 if address == self.short_node else 12)

    def u32(self, address): return self.words[address]
    def is_code(self, address): return 0x1000 <= address < 0x1080 and address != self.bad_code
    def section_of(self, address):
        if 0x461000 <= address < 0x467000:
            return (0x461000, 0, self.data_end - 0x461000, 0, self.data_name)
        if 0x480000 <= address < 0x485000:
            return (0x480000, 0, 0, self.storage_end - 0x480000, self.storage_name)
        return (0x1000, 0, 0x80, 0, self.code_name)


class SingletonRoots(unittest.TestCase):
    def setUp(self):
        self.image = SingletonImage()
        for name, value in (("GAME_SINGLETON_WALK", self.image.spec),
                            ("GAME_SINGLETON_REGISTRATIONS", self.image.registrations)):
            guard = patch.object(prepare_boot, name, value); guard.start(); self.addCleanup(guard.stop)

    def test_eight_creators_bindings_targets_and_no_mutation(self):
        im = self.image; before = dict(im.words)
        self.assertEqual(prepare_boot.game_singleton_creator_roots(im), set(range(0x1000, 0x1080, 16)))
        self.assertEqual(im.words, before)
        for _, _, _, node, storage, crt in im.registrations:
            for field in (crt, node + 4, node + 8):
                saved = im.words[field]; im.words[field] = 0xDEAD
                with self.assertRaisesRegex(ValueError, "binding mismatch"):
                    prepare_boot.game_singleton_creator_roots(im)
                im.words[field] = saved
            saved = im.words[node]
            for bad in (None, 0, 0x3000):
                im.words[node] = bad
                with self.assertRaisesRegex(ValueError, "title code"):
                    prepare_boot.game_singleton_creator_roots(im)
            im.words[node] = saved; im.bad_code = saved
            with self.assertRaisesRegex(ValueError, "title code"):
                prepare_boot.game_singleton_creator_roots(im)
            im.bad_code = None
        im.code_name = "DSOUND"
        with self.assertRaisesRegex(ValueError, "title code"):
            prepare_boot.game_singleton_creator_roots(im)

    def test_node_and_zero_fill_storage_spans(self):
        im = self.image
        im.data_name = ".text"
        with self.assertRaisesRegex(ValueError, "data span"):
            prepare_boot.game_singleton_creator_roots(im)
        im.data_name = ".data"; im.data_end = max(r[3] for r in im.registrations) + 11
        with self.assertRaisesRegex(ValueError, "data span"):
            prepare_boot.game_singleton_creator_roots(im)
        im.data_end = 0x467000; im.short_node = im.registrations[0][3]
        with self.assertRaisesRegex(ValueError, "data span"):
            prepare_boot.game_singleton_creator_roots(im)
        im.short_node = None; im.storage_end = max(r[4] for r in im.registrations) + 3
        with self.assertRaisesRegex(ValueError, "image span"):
            prepare_boot.game_singleton_creator_roots(im)
        im.storage_end = 0x485000; im.storage_name = ".text"
        with self.assertRaisesRegex(ValueError, "image span"):
            prepare_boot.game_singleton_creator_roots(im)

    def test_resolver_and_all_registration_guards(self):
        im = self.image
        with patch.object(prepare_boot, "GAME_SINGLETON_WALK", (*im.spec[:2], "0" * 64)):
            with self.assertRaisesRegex(ValueError, "resolver fingerprint"):
                prepare_boot.game_singleton_creator_roots(im)
        for which in range(8):
            specs = list(im.registrations)
            specs[which] = (*specs[which][:2], "0" * 64, *specs[which][3:])
            with patch.object(prepare_boot, "GAME_SINGLETON_REGISTRATIONS", specs):
                with self.assertRaisesRegex(ValueError, "registration fingerprint"):
                    prepare_boot.game_singleton_creator_roots(im)


class ActionImage:
    def __init__(self):
        self.code = b"synthetic action callers"
        self.words = {slot: 0x467564 + (n % 38) * 16
                      for n, slot in enumerate(range(0x4677C8, 0x4678B8, 4))}
        for n in range(38):
            for field in range(4):
                self.words[0x467564 + n * 16 + field * 4] = 0x1000 + n * 64 + field * 16
        self.bad_code = None
        self.data_name = ".data"
        self.code_name = ".text"
        self.data_end = 0x4677C4
        self.short_record = None

    def u32(self, address):
        return self.words[address]

    def bytes_at(self, address, size):
        if address == 0x200:
            assert size == len(self.code)
            return self.code
        assert size == 16
        data = struct.pack("<4I", *(self.words[address + n * 4] or 0 for n in range(4)))
        return data[:-1] if address == self.short_record else data

    def section_of(self, address):
        if 0x467564 <= address < 0x4677C4:
            return (0x467564, 0, self.data_end - 0x467564, 0, self.data_name)
        return (0x1000, 0, 0x1000, 0, self.code_name)

    def is_code(self, address):
        return 0x1000 <= address < 0x2000 and address != self.bad_code


class ActionRoots(unittest.TestCase):
    def setUp(self):
        self.image = ActionImage()
        self.spec = (0x200, len(self.image.code), hashlib.sha256(self.image.code).hexdigest())
        self.guards = patch.object(prepare_boot, "GAME_ACTION_WALKS", (self.spec,) * 4)
        self.guards.start()
        self.addCleanup(self.guards.stop)

    def test_all_slots_four_fields_duplicates_and_no_mutation(self):
        before = dict(self.image.words)
        expected = set(range(0x1000, 0x1980, 16))
        self.assertEqual(prepare_boot.game_action_callback_roots(self.image), expected)
        self.assertEqual(self.image.words, before)
        # Non-callback neighbors are deliberately invalid and never read.
        self.image.words[0x4677C4] = None
        self.image.words[0x4678B8] = None
        self.assertEqual(prepare_boot.game_action_callback_roots(self.image), expected)
        for slot in range(0x4677C8, 0x4678B8, 4):
            saved = self.image.words[slot]
            self.image.words[slot] = 0
            with self.assertRaisesRegex(ValueError, "data span"):
                prepare_boot.game_action_callback_roots(self.image)
            self.image.words[slot] = saved

    def test_record_arena_alignment_section_and_readable_span(self):
        slot = 0x4677C8
        for bad in (None, 0, 0x467560, 0x467565, 0x467568, 0x4677C4, 0x4677C8, 0xFFFFfff0):
            self.image.words[slot] = bad
            with self.assertRaisesRegex(ValueError, "data span"):
                prepare_boot.game_action_callback_roots(self.image)
        self.image.words[slot] = 0x467564
        self.image.data_name = ".text"
        with self.assertRaisesRegex(ValueError, "data span"):
            prepare_boot.game_action_callback_roots(self.image)
        self.image.data_name = ".data"
        self.image.data_end -= 1
        with self.assertRaisesRegex(ValueError, "data span"):
            prepare_boot.game_action_callback_roots(self.image)
        self.image.data_end += 1
        self.image.short_record = 0x467564
        with self.assertRaisesRegex(ValueError, "data span"):
            prepare_boot.game_action_callback_roots(self.image)

    def test_required_entry_optional_callbacks_and_code_sections(self):
        for field in (4, 8, 12):
            self.image.words[0x467564 + field] = 0
        roots = prepare_boot.game_action_callback_roots(self.image)
        self.assertIn(0x1000, roots)
        for missing in (0x1010, 0x1020, 0x1030): self.assertNotIn(missing, roots)
        for field in (0, 4, 8, 12):
            slot = 0x467574 + field
            saved = self.image.words[slot]
            for bad in (None, 0x3000):
                self.image.words[slot] = bad
                with self.assertRaisesRegex(ValueError, "title code"):
                    prepare_boot.game_action_callback_roots(self.image)
            self.image.words[slot] = saved
            self.image.bad_code = saved
            with self.assertRaisesRegex(ValueError, "title code"):
                prepare_boot.game_action_callback_roots(self.image)
            self.image.bad_code = None
        self.image.code_name = "D3D"
        with self.assertRaisesRegex(ValueError, "title code"):
            prepare_boot.game_action_callback_roots(self.image)
        self.image.code_name = ".text"
        self.image.words[0x467564] = 0
        with self.assertRaisesRegex(ValueError, "title code"):
            prepare_boot.game_action_callback_roots(self.image)

    def test_all_four_original_walk_guards(self):
        for which in range(4):
            specs = [self.spec] * 4
            specs[which] = (*self.spec[:2], "0" * 64)
            with patch.object(prepare_boot, "GAME_ACTION_WALKS", specs):
                with self.assertRaisesRegex(ValueError, "fingerprint"):
                    prepare_boot.game_action_callback_roots(self.image)


class DescriptorImage:
    def __init__(self):
        self.code = b"synthetic descriptor caller"
        self.data = bytearray(0x10000)
        self.parents = [0x461000 + n * 0xC8 for n in range(13)]
        for index, parent in enumerate(self.parents):
            self.write(0x468630 + 4 * index, parent)

    def write(self, address, value):
        struct.pack_into("<I", self.data, address - 0x460000, value)

    def bytes_at(self, address, length):
        if address == 0x200:
            assert length == len(self.code)
            return self.code
        return bytes(self.data[address - 0x460000:address - 0x460000 + length])

    def u32(self, address):
        return struct.unpack_from("<I", self.data, address - 0x460000)[0]

    def section_of(self, address):
        if 0x460000 <= address < 0x470000:
            return (0x460000, 0, 0x10000, 0x10000, ".data", ())
        if 0x1000 <= address < 0x2000:
            return (0x1000, 0, 0x1000, 0x1000, ".text", ("EXECUTABLE",))
        return None

    def is_code(self, address):
        return 0x1000 <= address < 0x2000


class DescriptorRoots(unittest.TestCase):
    def setUp(self):
        self.image = DescriptorImage()
        code = self.image.code
        self.guard = patch.object(prepare_boot, "GAME_DESCRIPTOR_WALK",
                                  (0x200, len(code), hashlib.sha256(code).hexdigest()))
        self.guard.start()
        self.addCleanup(self.guard.stop)

    def chain(self):
        return prepare_boot.game_descriptor_initialization_chain(self.image)

    def test_direct_child_fields_bounds_holes_and_no_recursion(self):
        a, b = self.image.parents[:2]; shared = 0x464000; last = self.image.parents[-1]
        for parent in (a, b, last):
            self.image.write(parent + 0x84, shared)
            self.image.write(parent + 0x8C, 0xDEAD)  # ignored after null at88h
            self.image.write(parent + 0x20, 0xDEAD)  # parent itself not selected
        self.image.write(shared + 0x84, 0xDEAD)  # no recursive child traversal
        self.image.write(shared + 0x20, 0x1100)
        self.image.write(shared + 0x24, 0x1200)
        self.image.write(shared + 0x28, 0x1300)
        self.image.write(shared + 0x2C, 0x1100)  # duplicate
        self.image.write(shared + 0x30, 0xDEAD)  # adjacent field is not code
        spec = (0x200, len(self.image.code), hashlib.sha256(self.image.code).hexdigest())
        before = bytes(self.image.data)
        with patch.object(prepare_boot, "GAME_DESCRIPTOR_CHILD_WALKS", (spec,) * 5):
            self.assertEqual(prepare_boot.game_descriptor_child_roots(self.image), {0x1100, 0x1200, 0x1300})
            self.assertEqual(bytes(self.image.data), before)
            for offset in (0x20, 0x24, 0x28, 0x2C):
                saved = self.image.u32(shared + offset)
                for bad in (0xDEAD, 0x464000):
                    self.image.write(shared + offset, bad)
                    with self.assertRaisesRegex(ValueError, "invalid target"):
                        prepare_boot.game_descriptor_child_roots(self.image)
                self.image.write(shared + offset, 0)
                prepare_boot.game_descriptor_child_roots(self.image)  # original null skip
                self.image.write(shared + offset, saved)
            for i in range(16):
                self.image.write(a + 0x84 + i * 4, shared)
            with self.assertRaisesRegex(ValueError, "not terminated"):
                prepare_boot.game_descriptor_child_roots(self.image)
        for which in range(5):
            specs = [spec] * 5; specs[which] = (0x200, len(self.image.code), "0" * 64)
            with patch.object(prepare_boot, "GAME_DESCRIPTOR_CHILD_WALKS", specs):
                with self.assertRaisesRegex(ValueError, "fingerprint"):
                    prepare_boot.game_descriptor_child_roots(self.image)

    def test_object_child_fields_bounds_and_all_walk_guards(self):
        shared = 0x464000; parent = self.image.parents[-1]
        self.image.write(parent + 0x84, shared)
        self.image.write(parent + 0x8C, 0xDEAD)  # after the null terminator
        self.image.write(shared + 0x84, 0xDEAD)  # no recursive walk
        self.image.write(shared + 0x34, 0xDEAD)
        self.image.write(shared + 0x60, 0xDEAD)
        self.image.write(shared + 0x78, 0xDEAD)
        fields = (0x30, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C,
                  0x64, 0x68, 0x6C, 0x70, 0x74)
        for i, offset in enumerate(fields):
            self.image.write(shared + offset, 0x1100 + i * 16)
            self.image.write(parent + offset, 0xDEAD)  # only children are called
        spec = (0x200, len(self.image.code), hashlib.sha256(self.image.code).hexdigest())
        before = bytes(self.image.data)
        with patch.object(prepare_boot, "GAME_DESCRIPTOR_OBJECT_WALKS", [(*spec, field) for field in fields]):
            expected = set(range(0x1100, 0x1200, 16))
            self.assertEqual(prepare_boot.game_descriptor_object_roots(self.image), expected)
            self.assertEqual(bytes(self.image.data), before)
            for offset in fields:
                saved = self.image.u32(shared + offset)
                self.image.write(shared + offset, 0)
                self.assertEqual(prepare_boot.game_descriptor_object_roots(self.image), expected - {saved})
                for bad in (0xDEAD, shared):
                    self.image.write(shared + offset, bad)
                    with self.assertRaisesRegex(ValueError, "invalid target"):
                        prepare_boot.game_descriptor_object_roots(self.image)
                self.image.write(shared + offset, saved)
            for i in range(16): self.image.write(parent + 0x84 + i * 4, shared)
            with self.assertRaisesRegex(ValueError, "not terminated"):
                prepare_boot.game_descriptor_object_roots(self.image)
        for which in range(len(fields)):
            specs = [(*spec, field) for field in fields]
            specs[which] = (*spec[:2], "0" * 64, fields[which])
            with patch.object(prepare_boot, "GAME_DESCRIPTOR_OBJECT_WALKS", specs):
                with self.assertRaisesRegex(ValueError, "fingerprint"):
                    prepare_boot.game_descriptor_object_roots(self.image)

    def test_map_fields_reuse_linking_and_skip_unvisited_data(self):
        a, b = self.image.parents[:2]; shared = 0x464000
        for parent in (a, b):
            self.image.write(parent + 0x84, shared)
            self.image.write(parent + 0x88, parent)
            self.image.write(parent + 0x14, 0xDEAD)
            self.image.write(parent + 0x20, 0xDEAD)
        self.image.write(shared + 0x84, 0xDEAD)  # no recursive child walk
        self.image.write(a + 0x18, 0x1100)
        self.image.write(b + 0x1C, 0x1100)  # duplicate
        self.image.write(shared + 0x18, 0x1200)
        last = self.image.parents[-1]
        self.image.write(last + 0x1C, 0x1300)
        spec = (0x200, len(self.image.code), hashlib.sha256(self.image.code).hexdigest())
        before = bytes(self.image.data)
        with patch.object(prepare_boot, "GAME_DESCRIPTOR_MAP_WALKS", (spec, spec)):
            self.assertEqual(prepare_boot.game_descriptor_map_roots(self.image), {0x1100, 0x1200, 0x1300})
            self.assertEqual(bytes(self.image.data), before)
            for node, offset in ((a, 0x18), (last, 0x1C)):
                saved = self.image.u32(node + offset)
                for invalid in (0xDEAD, 0x464000):
                    self.image.write(node + offset, invalid)
                    with self.assertRaisesRegex(ValueError, "invalid target"):
                        prepare_boot.game_descriptor_map_roots(self.image)
                self.image.write(node + offset, saved)
            self.image.write(last + 0xC4, a)
            with self.assertRaisesRegex(ValueError, "initial link"):
                prepare_boot.game_descriptor_map_roots(self.image)
            self.image.write(last + 0xC4, 0)
        for which in range(2):
            specs = [spec, spec]; specs[which] = (0x200, len(self.image.code), "0" * 64)
            with patch.object(prepare_boot, "GAME_DESCRIPTOR_MAP_WALKS", specs):
                with self.assertRaisesRegex(ValueError, "fingerprint"):
                    prepare_boot.game_descriptor_map_roots(self.image)

    def test_shared_self_children_and_repeated_callbacks_keep_original_order(self):
        a, b = self.image.parents[:2]
        shared = 0x464000
        for parent in (a, b):
            self.image.write(parent + 0x84, shared)
            self.image.write(parent + 0x88, parent)
            self.image.write(parent + 0x90, 0xDEAD)  # ignored after first null
            self.image.write(parent + 0x10, 0x1000)
        self.image.write(shared + 0x10, 0x1500)
        # Child descriptors are not themselves traversed as parents.
        self.image.write(shared + 0x84, 0xDEAD)
        expected = [(a, 0x1000), (shared, 0x1500), (b, 0x1000)]
        expected += [(parent, 0) for parent in self.image.parents[2:]]
        before = bytes(self.image.data)
        self.assertEqual(self.chain(), expected)
        self.assertEqual(bytes(self.image.data), before)

    def test_full_sixteen_child_bound(self):
        parent = self.image.parents[0]
        children = [0x464000 + n * 0xC8 for n in range(16)]
        for index, child in enumerate(children):
            self.image.write(parent + 0x84 + 4 * index, child)
        self.assertEqual([node for node, _ in self.chain()],
                         [parent] + children + self.image.parents[1:])

    def test_bad_descriptor_spans_and_callback(self):
        for invalid in (0, 0x461001, 0x46FFFC, 0x500000):
            self.image.write(0x468630, invalid)
            with self.assertRaisesRegex(ValueError, "data span"):
                self.chain()
        parent = self.image.parents[0]
        self.image.write(0x468630, parent)
        self.image.write(parent + 0x84, parent + 4)
        with self.assertRaisesRegex(ValueError, "overlap"):
            self.chain()
        self.image.write(parent + 0x84, 0)
        self.image.write(parent + 0x10, 0x464000)
        with self.assertRaisesRegex(ValueError, "invalid initializer"):
            self.chain()

    def test_reject_initial_links_and_changed_caller(self):
        parent = self.image.parents[0]
        self.image.write(parent + 0xC4, parent)
        with self.assertRaisesRegex(ValueError, "initial link"):
            self.chain()
        self.image.write(parent + 0xC4, 0)
        self.image.code = bytes([self.image.code[0] ^ 1]) + self.image.code[1:]
        with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
            self.chain()


if __name__ == "__main__":
    unittest.main()
