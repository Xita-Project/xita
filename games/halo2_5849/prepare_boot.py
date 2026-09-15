#!/usr/bin/env python3
"""Prepare private, revision-checked Halo 2 startup artifacts from an owned XBE."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from recompiler.core.profile import load_profile
from recompiler.xita_recomp import Image, KERNEL_DATA_EXPORTS, KERNEL_EXPORTS
from games.halo2_5849.hooks import (reviewed_sparse_jump_roots, reviewed_widget_kind_roots,
                                  reviewed_widget_field_roots)


HOST_CALLBACK_WALK = (0x3FBA54, 135, "e0cc1649c0b744615b3de0f5b2446411bb408d0b3ce4d1d3da59980219abc70c")
STREAM_VTABLE = (0x417170, 28, "72cb68310880069f79f94d33bfd78c04aae9b0b48e2d4b623612bd88cd1a53c9")
GAME_SOUND_VTABLES = (
    (0x47F0D0, 0x45711C, 36, "3390e861a1a9ebe7c9da27fbef73da9982814ad5696894b63d9aed83c665c38b"),
    (0x47F088, 0x457140, 28, "cfe13e6e922ae34f6ec6ffdfae387fecb3c865fe164f75f1be70c9004d969120"),
    (0x47F0F0, 0x45715C, 36, "5176d30bf8e4cea56c2a70787eed63a1704718b78f13a40bcd04edd9074be25e"),
)


def game_sound_owner_roots(image):
    """Original stream-format/selected sound owners observed in initialization."""
    roots = set()
    for owner, address, length, digest in GAME_SOUND_VTABLES:
        if image.u32(owner) != address:
            raise ValueError("Halo 2 game sound owner binding mismatch")
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 game sound owner vtable fingerprint mismatch")
        for slot in range(address, address + length, 4):
            target = image.u32(slot)
            section = image.section_of(target) if target else None
            if not target or not image.is_code(target) or not section or section[4] != ".text":
                raise ValueError("Halo 2 game sound owner target is not title code")
            roots.add(target)
    return roots


def audio_stream_roots(image):
    """Seven original stream interface slots; unknown APIs retain strict guards."""
    address, length, digest = STREAM_VTABLE
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 stream vtable fingerprint mismatch")
    roots = set()
    for slot in range(address, address + length, 4):
        target = image.u32(slot)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != "DSOUND":
            raise ValueError("Halo 2 stream vtable target is not DSOUND code")
        roots.add(target)
    return roots


XPP_CALLBACK_WALK = (0x408C72, 36, "9234a2afaedda5206ca55c2bf3f0269b70b1b0345091581c639ee86230cd3756")
GAME_INIT_WALK = (0x137C84, 19, "44c1c20adf4bb014714a0825d601e592e668699e31671c7676a674951946da02")
GAME_MAP_WALKS = (
    (0x137CC5, 34, "c8aab7f2f289627cdbf8915607639cd18538016fe0b7f4e3103dd4bbef21eeff"),
    (0x137D0D, 24, "9cb66f3dce939462fe6ed66f2a48cf29c9f7984c06084164cdcaef1866c90598"),
    (0x137D7B, 28, "a96f07c6fb9bebebfe3c9c11c3991c16c39ea99e73e03f0e281734cf1549cf06"),
    (0x137DA1, 24, "602be11a670b15871af4b680707d5daef495dfe3c96e1397204b8e5f580dd7d2"),
)
GAME_REMAINING_LIFECYCLE_WALKS = (
    (0x12B690, 82, "48f5b814b9988bc9a84a51edb0f8e887100bb821037c4fa9789297b2cd43eeaf"),
    (0x11C1B0, 456, "8cfd851b1f5935613673c08ab78bd6e32cf9061b28c7fc41a3f55850cb9d6035"),
    (0x138C10, 411, "0d8b69bf6eae66d95d49af226c4df2f8cb4a74f75262226da3d72fa344f6ccf4"),
)
GAME_RESOURCE_WALKS = (
    (0xD48A1, 29, "0791d3646b316f6c184aabc27feed7cb52baae825f512b69619b288d7e6e06c2"),
    (0xD48D2, 28, "0590129ef3b973f39b577ebb22e78a02e28b830fd15d1810386c95aab9c7738d"),
    (0xD4DB7, 25, "1bfc7777b7aa974c521ed9f9a02992ddd431597af8c68e9dc9d1070988637ef8"),
)
GAME_DESCRIPTOR_WALK = (0x1088E0, 124, "c1bf2193fbf5a7f7a8d0de9fffaaf9ee5cbe12ced08b39059af29896540348ec")
GAME_DESCRIPTOR_MAP_WALKS = (
    (0xB69D8, 31, "4850979303eb86790589e6a32dd3bcce89e4d8f58d809678e55869c4f3e51d67"),
    (0xB6AB7, 31, "ea694745b9044ee364ab532d8e5b9ff513a30b50df1127666c9adab5beb432aa"),
)
GAME_DESCRIPTOR_CHILD_WALKS = (
    (0x1088A0, 54, "d6eb603c5f02b788e35ca7cf7c3d9deb074bf7de9ec5713dd13f713a0e296db3"),
    (0x108960, 101, "7ea8925cd8f074e3813192c97d502b93db4457e76596f1aca5d36acb5bd8439c"),
    (0x1089D0, 101, "49878cdd6bea74a1154ff2dc9294513ada0489a089e7cc09c892790dc6d03fa3"),
    (0x108A40, 66, "b80567cf17df1eb4cf294790c5ef58f37dec29e321ff31a4743b8287ae436101"),
    (0x108A90, 122, "afa08763dab7cffe0330b76bdf026aec29abfe8d8f9ac67117d07b7f13c4b3c3"),
)
GAME_DESCRIPTOR_OBJECT_WALKS = (
    (0x108B10, 105, "261722d636dcff18acdce3c501d083797986c3504cf426dd9cd30ea47360005d", 0x30),
    (0x108B80, 101, "46dfd64e84599b7b90a05c402b6ec2cb1f627b6c04b4b57c620a249b5a8d6c2e", 0x38),
    (0x108BF0, 101, "d1009849ca1aabae6081d6858ccd634538c9535a9295742bc4788ecf7143d247", 0x3C),
    (0x108C60, 110, "16cbad1fedde74fbfe563a6d1ce08801832558d5c8fd29098997f26b5fa746ce", 0x40),
    (0x108CD0, 81, "b3af928d6accc978775d53f1d4759e1f47f5ad44b1fed48f5f0a90d953102538", 0x44),
    (0x108D30, 81, "a907ee29e25bd27db17da3a093a958cd8fe61f10d04afb5e10f750c3a76737a3", 0x48),
    (0x108D90, 117, "6dd300c5fd7385d636ebeec940ed433fa0067d39a7307bc85d0c07bd15cd43fb", 0x4C),
    (0x108E10, 101, "64c1a3061d9e139b33cee350f849ca045d8f5f01dd21ce6a2e537812d527e011", 0x50),
    (0x108E80, 101, "b9c77763366bc91b115c78332c2200c3fc3af8128eca1abf5e61bf2f1d9afe92", 0x54),
    (0x108EF0, 105, "bf94425ef9deaf4e5522c0bc751c048ec97c3a45404fb81e89be28fd47056bdf", 0x58),
    (0x108F60, 105, "f120124e8a08da4a3a6fc92abcef5a78250efdb1775058f039ecc2354694eb5b", 0x5C),
    (0x108FD0, 114, "bb174c0f12b280811288ac9a93a8fd15e24bc04546c4ec8ac160b96f2d49b1e1", 0x64),
    (0x109290, 100, "c3b23ff93c8be4da1da58815fa9eba30130233d88bc392525625b62002eee686", 0x68),
    (0x109050, 115, "fe250c4ad31ff7e8f84b0784fac61f03bea2c9a2482455ef28d5af23d995b85e", 0x6C),
    (0x1090D0, 105, "b8d52ae03c3f06fe935b51d94ecab2e378e3515ee6492b65fa8b82fbaac06ab1", 0x70),
    (0x109140, 110, "6e5355dcb984bc4ce4fdb2264bbedb8ffcae2f46b3b5afb73279b7d4e1438a0e", 0x74),
)
GAME_PAIR_LISTENER_CALLS = (
    (0x3101c0, 1850, "42a213f2b6c96033db532ebef5e5e9dc5e4dbc4d8bed28d18a7475214b5a499b"),
    (0x30e4b0, 303, "a3e5389478a81f59a5c47b1f594afeeb2ffa28b9249b9289f823cd3c993672b6"),
    (0x30d1b0, 71, "a5dad4dc610aaf7f391f9b57c49cb34a49c3a51f32532f2d9e24b3112cbd58d0"),
    (0x30d220, 10, "1b29e5c75b1678cc4c4a0a94119b5ae94b2f2fa3ac98f07ded2d28a6b207ec04"),
    (0x3728c0, 378, "b7c25a0fe7b69cda2e84c1d0505a4a9cf787135741f36f5ed16f26247a72cf5e"),
    (0x372a40, 56, "79e0a0f3f0b3b7d66891947bede9ed32c2f156ba3275525d9aa855ecdfc2a2b5"),
    (0x315e70, 122, "a154d313fe2c59a1f44ecb703904ca5da0f506717eb9266d2f1bee09cdbab7f8"),
    (0x315ef0, 97, "70bf5dd4e2482460054391d1f66517201465ae6a30608a5e3a9a2988c604b85d"),
    (0x30b480, 135, "27f37fe29acb4f054753b427d91a87819d3adc5e12ef85fa3b9e94fa63534efb"),
    (0x30b2a0, 146, "6f91a41740a5b74a4683c597e195e9ff96dc9828b5636a73d4059a497176cb36"),
    (0x30b0a0, 104, "f2fb3a2223dd01f25a2d15a7e8ec09df6389e0a3d96763d07f1f9530f72d2e14"),
    (0x72c70, 3, "e598d0c3ba86d917b177d7adde0556aa99bc355543c57aee0a3e50b684dd7e99"),
)
GAME_BOUNDS_INSERT_CALLS = (
    (0x30E4B0, 303, "a3e5389478a81f59a5c47b1f594afeeb2ffa28b9249b9289f823cd3c993672b6"),
    (0x30B480, 135, "27f37fe29acb4f054753b427d91a87819d3adc5e12ef85fa3b9e94fa63534efb"),
    (0x30B110, 22, "479f7f3051942acc4986a419943e7766242d5c4a1f4d72b3b41ed6907dd26b36"),
    (0x3101C0, 1850, "42a213f2b6c96033db532ebef5e5e9dc5e4dbc4d8bed28d18a7475214b5a499b"),
    (0x2E4F70, 125, "761c48f642757d1669faf4b81c1e553bddd093395c5c494195a60bc00acef774"),
    (0x2E1D10, 1152, "a78d9ceed5f2f0e55eaa1e3d3049c94a7dd54b5948ed3cba500aaba39596412f"),
)
GAME_MEMBER_QUERY_CALLS = (
    (0x316C90, 81, "c208200cd4add2e249bd6d4e742807b62b241778847f0b653c9828bd6a3e8bb1"),
    (0x316000, 82, "06d552d68c397980a59ed4417a66ac7b6dbb8e9e5329183542151a88434200be"),
    (0x316A20, 257, "0565bb2f3d1512723b4b501713774a70f4b94dd5b2dd7861cc644337b1f4f6ac"),
    (0x3171A0, 194, "6104830a51dbef00c8170a8e1f5ec8fcc053f05136ed457d96d0a94f5e70e929"),
    (0x31AAC0, 6, "c5264b41ff2cd91ca34fc5b460d7a9373bf0f98ec1b6f41fec25d8e1abff46b4"),
)
GAME_MEMBER_QUERY_CTORS = (
    (0x311890, 130, "c3bde928130529e6c0e71dcb207963dbe5b4070ce2d07134b3ba5acc3e0b28ff", 0x4143A8),
    (0x311920, 32, "054f3a7fcdacc140ce06b0282e6b92c0ef3bc39c075f4c03d7a0929e848a07a1", 0x414428),
    (0x3193C0, 102, "3a2850468a44321a657945717d1af0d6776aa851561a33efbbb6e502fa1bbb2d", 0x415130),
    (0x31A6B0, 96, "e0eeb32fff30ccddff70bbbaf37abdc576d24051b24e499087a76e3060d97898", 0x4151B0),
    (0x31A870, 32, "444c41ce48aa0e05bbea08a6d5a3b9378949d1d2005591417cb9395007a116c4", 0x415230),
    (0x31AD80, 109, "cea965d9cbe1e8d841b8a813ed95a5e864c32f26397b5b1d234d7d665aaa8587", 0x4152B0),
)
GAME_BOOT_FACTORY_CALLS = (
    (0x2D8780, 259, "1654647adb4f74b28facc24dd59797bbe9924e99cdda959a70a9f0f6e7c816c2"),
    (0x2D7570, 224, "c2872aa7f20959a5ff9efe644436e8084041b15ccdc5a1ebf3639c19cb997ef4"),
    (0x2D76F0, 36, "4a2f1921e57804b3624ba04cc1398b0e15a8bc564e07b739a5480d6861287639"),
    (0x2D73F0, 101, "58b873cd65b14edad3b22cbf5db8554d110eb876b8010e31e248a21d217d9df1"),
    (0x2D7460, 101, "6cff4d98fd5f8cc3356c6df8c7e35346e336f8e7ad039c63a20041702cbadb22"),
    (0x2D74D0, 101, "a8518be520afdb5a7ae2ac804d046291b1343da0ad43e015d6108aec80c716b7"),
    (0x2D7670, 40, "7e9d098ad5485d0e85b1d7258d4f340820bf6de307be86c5bc1d35761b8c1a11"),
    (0x2D7720, 40, "6b3e5d3cf6d878be4406389abf8cce2966f1acca9dcc7102efc16d33136ae476"),
    (0x2DA270, 40, "c3d27d90626c6ade01f275b2924c510e493adf79ef4468532abe7940e17e6c3a"),
    (0x2D84E0, 40, "7e9d098ad5485d0e85b1d7258d4f340820bf6de307be86c5bc1d35761b8c1a11"),
    (0x2D8E70, 114, "1d9c91e2ac71f9c7781f18738d093adc2d5147f844531dac1314ef92f924a61e"),
    (0x2D9090, 22, "8ca74b5e1f1560d65bc1fa8fc1cff3ab46e43ea039f44a8275f00cc3e269a022"),
    (0x147320, 129, "1ca08108d766acef4f3b29045d57558acb930280a992c97938522bcb5f1d7fd6"),
    (0x22C320, 13, "8df868c5db9147d935b75030b37dd4604e925bffabdef023d78cbae2961c5583"),
)
GAME_ARENA_BOOT_CALLS = (
    (0x146A20, 146, "75f53bc57da723bcf62196fb9e3e3632d6a1c4dc842d5caf8e46fbe79dc34b73"),
    (0x1472C0, 82, "cf2b2e35cb786b94043345fb459404465072170134b691332a57fe5a760bbe70"),
    (0x22C300, 20, "b80aaf8d178411bebef13a252b87a31d9c5882ad7fe51dd28071fc6dc27026a8"),
)
GAME_SINGLETON_WALK = (0x2D7CA0, 295, "3e9e43cc8881a31c581803d5b1273e856d95098046427ca21d86579036328fe7")
GAME_SINGLETON_REGISTRATIONS = (
    (0x378ad0, 21, "29f3d937141478c3269a41fd0e98b088c040b7d994d24b89c4fa7eac096cefc2", 0x461de4, 0x48011c, 0x46119c),
    (0x378af0, 21, "08c19f745b3271135c7abc1fa9f307e3377de97de628170e09ce9e95d7b97502", 0x461df8, 0x480148, 0x4611a0),
    (0x378b10, 21, "53fea3995eaa81d6fd2e0a5bddfabd74bb5f4327f46d6b1e538ae7d4cfd9c094", 0x461e04, 0x480160, 0x4611a4),
    (0x378ba0, 21, "65a580047695366bff6365feb4e88268c67dac40a6c06f2dcfd7d3bd6c9df2a2", 0x461e7c, 0x4801d4, 0x4611b0),
    (0x378c00, 21, "2892764c3d510b7e6071a2da50c4698d43a0e16ee902e5b07769c338d3041f89", 0x461ee0, 0x4802a4, 0x4611b8),
    (0x379630, 21, "5d87678759032f5c183c30b05d76df1234129ab3721c0a673eb1516c4d0bda88", 0x466dd8, 0x484b38, 0x46129c),
    (0x379650, 21, "d1a6eb61d8f320b979e78672dfaa91b4f167c249859951ff6951174cefbe053e", 0x466de4, 0x484b40, 0x4612a0),
    (0x379670, 21, "6ef48a0b1e2d0d5abe7a4d2685d50563e30c204c0e8c17fe8c7c1163bb456109", 0x466df0, 0x484b44, 0x4612a4),
)
GAME_FIXED_STARTUP_CALLS = (
    (0x1C2695, 6, "9a975fa7706c2abfb3aedac6efe53ace2da0647b79028bf3fd1181c5e82b6029"),
    (0x1C2862, 6, "2ee5d11f38026183c4cded56144a85988d8437d36cee1443eea0a3a78e0b8692"),
    (0x1473B0, 6, "800c60f4c2941676ba2f79b9e737973518adb9da705961d1007d2079fea96a8d"),
    (0x2D8780, 259, "1654647adb4f74b28facc24dd59797bbe9924e99cdda959a70a9f0f6e7c816c2"),
    (0x2D6FE0, 63, "4160dcd7eb24ef4504ecc06f739483dcacbd55b608f2b7d2598fa4ccb76f86d2"),
)
GAME_ACTION_WALKS = (
    (0xE6830, 137, "74cc8d6fbd2cf6c4f8ecb86836d10796283f833809d7f89d09145e81b07b9125"),
    (0xE6900, 88, "8fe963a2c722ff6daee79d421a47125d2f65f32398eda43ef7738384d624a661"),
    (0xE6960, 84, "fcd6bd4ff031fe366f720daea5bcec15ed708b7220e290716b5bacc49e7cbbe9"),
    (0xE69C0, 84, "6df98094d9647e78eded72ec43008f54445f6bb2a6c0443948a0a36a0f82d9b7"),
)
GAME_PACKED_VECTOR_BINDINGS = (
    (0x279BA2, 45, "ae5b4f05401786d52eb8183057ed4ce7f7ed8b9c4b38b3d45510b1d9a291b6fa"),
    (0x279C6F, 70, "d9fe85669bb7394f774d95bbce234fa3950a02301b12e6b67ca0962456a6f2c5"),
)
GAME_MODE_WALK = (0x18EF00, 152, "c499facfbe49993ebd3e15bb55a4f65adafb4bfd53eb99474ba7bb96ad3f8102")
GAME_INTERFACE_REGISTRATION = (0x3769F0, 45, "46e548c6c8f362dc1ba57b6f7581a1b2c0bffb4b2cb9c2e812dcc7b9544b1611")
GAME_ONLINE_INTERFACE_DISPATCH = (0x59949, 41, "c68f2b75408f155325c537d83f024b3ff580f096399c7606ca83a739654555a1")
BINK_PIXEL_DISPATCH = (
    (0x3EDB70, 80, "67b6419123998c1cce93a7f29fb8630684b4f2813e8ab27d5cc6c1cc884535e0"),
    (0x3E97E0, 1013, "57ec72883b4d8abe0ef2170d0e79202c3a5b70a7da216051a2d25e1d3fa72db0"),
)
GAME_DISPATCH_CONSTRUCTORS = (
    (0x23546B, 27, "cbd17bebf8667c708be45cbe65a4fc6dfc672448edf8c83151e4173d16e69e71"),
    (0x234E43, 33, "84924bde2768f01fd262d3d0cd0916038e22c201d8200008e05c9cdff85bd95f"),
)
GAME_ALLOCATOR_CONSTRUCTORS = (
    (0x81EC2, 6, "12ddc689a0e651bd82e523e0305410591c34c0ecd1da8fda19d119363ba1520f"),
    (0x32A871, 6, "090c672f0e3b32b564eec33dbae02580b53d91bf1c9d4e1eb0b378b0d2faa881"),
)
GAME_STATE_CONSTRUCTORS = (
    (0x376A20, 10, "ea990a0b2671f169030c7ed2bac29c98c5c5e42fd4bafcb4d5fcf86da5ff161c"),
    (0x58E70, 105, "9195f198f6de23c02ee21bf68539b3898e5bad37deff50056be9c0b6b35b1e29"),
)
GAME_STARTUP_WIDGET_CONSTRUCTORS = (
    (0x2B7269, 32, "80197ecb7c788fcdef80d420ace1c23bb3b5b063704d5a2de1b3f0a9a663e90c"),
    (0x2B7388, 6, "9a1381c2d5cae2abe6b261ed8da05f4b4ffb50cd3f83001353654c83aab7ad4c"),
)
GAME_TEXT_WIDGET_CONSTRUCTORS = (
    (0x22F561, 30, "f3dbfe2397a2ad7ad4d86f0fbba196e80b76143b455fc7890bb58ee932689ecd"),
    (0x22F583, 30, "0af4f3515f9b6b34f00727c0be7369e6e47f14a998372d1a5495982d9ce851e0"),
    (0x22F4FA, 19, "a27a5c5dad3bab9e240ea5d61bb3be35633de873c4e7f8c9de1251d55fd80775"),
    (0x22F532, 19, "2ec44c870c40e778e72b6f3d4a7a2b37fa8ab37ae974256d86e49fe9920be02e"),
)
GAME_WIDGET_PROPERTY_DISPATCH = (0x2373BE, 56, "863a8fd2961e9fd5ceab6711205222d5cf403957a5512768286bceb656ca4c61")


def game_mode_callback_roots(image):
    """Native47: eight paired callbacks, two stride-eight walks of 40h bytes."""
    address, length, digest = GAME_MODE_WALK
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 mode callback walk fingerprint mismatch")
    roots = set()
    for slot in range(0x453C00, 0x453C40, 4):
        target = image.u32(slot)
        if target == 0:
            continue
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError(f"Halo 2 mode callback slot {slot:#x} has invalid target")
        roots.add(target)
    return roots


def _constructor_vtable_roots(image, constructors, start, end):
    for address, length, digest in constructors:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 dispatch constructor fingerprint mismatch")
    return _code_vtable_roots(image, start, end)


def _code_vtable_roots(image, start, end):
    roots = set()
    for slot in range(start, end, 4):
        target = image.u32(slot)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError(f"Halo 2 dispatch vtable slot {slot:#x} has invalid target")
        roots.add(target)
    return roots


def game_registered_interface_roots(image):
    """Native51: four static interface objects registered by the same initializer."""
    address, length, digest = GAME_INTERFACE_REGISTRATION
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 interface registration fingerprint mismatch")
    roots = set()
    for slot, instance, table in ((0x417370, 0x462E00, 0x417378),
                                  (0x4173E4, 0x462E10, 0x4173E8),
                                  (0x41745C, 0x462E28, 0x417460),
                                  (0x4174D0, 0x462F30, 0x4174D8)):
        if image.u32(slot) != instance or image.u32(instance) != table:
            raise ValueError(f"Halo 2 interface binding {slot:#x} mismatch")
        roots.update(_code_vtable_roots(image, table, table + 27 * 4))
    return roots


def game_dispatch_vtable_roots(image):
    """Native46 object vtable, bounded by two original constructor assignments."""
    return _constructor_vtable_roots(image, GAME_DISPATCH_CONSTRUCTORS, 0x4599A8, 0x4599DC)


def game_allocator_vtable_roots(image):
    """Native48: allocator at 454970, followed by the table assigned at 32A871."""
    return _constructor_vtable_roots(image, GAME_ALLOCATOR_CONSTRUCTORS, 0x454970, 0x454980)


def game_state_vtable_roots(image):
    """Native58: one constructor assigns eleven adjacent four-method state tables."""
    return _constructor_vtable_roots(image, GAME_STATE_CONSTRUCTORS, 0x450990, 0x450A40)


def game_startup_widget_vtable_roots(image):
    """Native146: constructor 2B7269 assigns the table used at 234E2C.

    The adjacent table is independently assigned at 2B7388. Include only the
    intervening 28 executable slots, including observed slot48h -> 2B7289.
    """
    return _constructor_vtable_roots(image, GAME_STARTUP_WIDGET_CONSTRUCTORS, 0x45BC60, 0x45BCD0)


def game_text_widget_vtable_roots(image):
    """Native147: outer getter returns member+74h; caller invokes member slot4.

    The two original factory branches construct the same outer interface and
    one of two three-method text members. Root only their executable prefixes;
    the following zero words and adjacent object tables remain untouched.
    """
    roots = _constructor_vtable_roots(image, GAME_TEXT_WIDGET_CONSTRUCTORS, 0x458940, 0x458984)
    for start, end in ((0x4588B0, 0x4588BC), (0x458930, 0x45893C)):
        roots.update(_code_vtable_roots(image, start, end))
    if any(image.u32(end) != 0 for end in (0x458984, 0x4588BC, 0x45893C)):
        raise ValueError("Halo 2 text interface boundary mismatch")
    return roots


def game_widget_property_roots(image):
    """Native148: original caller checks 0 <= index < 70h and skips nulls.

    Each nonnull entry supplies the original property kind/offset/scale. Keep
    the table's explicit holes and execute the callbacks without replacement.
    """
    address, length, digest = GAME_WIDGET_PROPERTY_DISPATCH
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 widget property dispatch fingerprint mismatch")
    roots = set()
    for slot in range(0x470828, 0x4709E8, 4):
        target = image.u32(slot)
        if target == 0:
            continue
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError(f"Halo 2 widget property slot {slot:#x} has invalid target")
        roots.add(target)
    return roots


def game_online_interface_roots(image):
    """Native59: the same original update calls two statically bound interfaces."""
    address, length, digest = GAME_ONLINE_INTERFACE_DISPATCH
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 online interface dispatch fingerprint mismatch")
    for instance, table in ((0x47708C, 0x450B68), (0x47712C, 0x450B44)):
        if image.u32(instance) != table:
            raise ValueError(f"Halo 2 online interface binding {instance:#x} mismatch")
    return _code_vtable_roots(image, 0x450B44, 0x450B88)


def bink_pixel_callback_roots(image):
    """Native68: the original wrapper passes this mixed converter descriptor.

    3E97E0 copies selected function fields into the converter dispatch globals.
    Six scalar/function pairs and sixteen later function words are distinct
    from the intervening mutable selection counters; those are never roots.
    """
    for address, length, digest in BINK_PIXEL_DISPATCH:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 Bink pixel dispatch fingerprint mismatch")
    base = 0x57A080
    for offset, value in ((0, 4), (4, 2), (12, 2), (20, 3), (28, 2), (36, 2), (44, 3)):
        if image.u32(base + offset) != value:
            raise ValueError("Halo 2 Bink pixel descriptor shape mismatch")
    roots = set()
    for offset in (*range(8, 0x34, 8), *range(0x74, 0xB4, 4)):
        target = image.u32(base + offset)
        section = image.section_of(target) if target else None
        if (not target or not image.is_code(target) or not section or
                section[4] not in {"BINK32", "BINK32M", "BINK32X2", "BINK32MX"}):
            raise ValueError(f"Halo 2 Bink pixel callback {base + offset:#x} has invalid target")
        roots.add(target)
    return roots


def game_descriptor_initialization_chain(image):
    """Native45: reproduce the bounded original descriptor linking algorithm.

    Children are not recursively traversed. A shared/self child is appended
    only while its current next pointer is zero; later parent writes can
    replace that pointer. Do not substitute ordinary graph deduplication.
    This extraction supports the pinned image's initially unlinked records.
    It never changes the image or the original guest list construction.
    """
    address, length, digest = GAME_DESCRIPTOR_WALK
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 descriptor walk fingerprint mismatch")
    nodes = {}

    def validate_node(node):
        if node in nodes:
            return
        section = image.section_of(node) if node else None
        if (not node or node & 3 or not section or section[4] != ".data" or
                node + 0xC8 > section[0] + section[2] or
                len(image.bytes_at(node, 0xC8)) != 0xC8):
            raise ValueError(f"Halo 2 descriptor {node!r} has invalid data span")
        if any(node < other + 0xC8 and other < node + 0xC8 for other in nodes):
            raise ValueError("Halo 2 descriptor records overlap")
        if image.u32(node + 0xC4) != 0:
            raise ValueError("Halo 2 descriptor has unsupported initial link")
        target = image.u32(node + 0x10)
        section = image.section_of(target) if target else None
        if target is None or (target and (not image.is_code(target) or not section or section[4] != ".text")):
            raise ValueError(f"Halo 2 descriptor {node:#x} has invalid initializer")
        nodes[node] = target

    parents = []
    children = {}
    for slot in range(0x468630, 0x468664, 4):
        parent = image.u32(slot)
        validate_node(parent)
        parents.append(parent)
        children[parent] = []
        for index in range(16):
            child = image.u32(parent + 0x84 + 4 * index)
            if child == 0:
                break
            validate_node(child)
            children[parent].append(child)
    links = {node: 0 for node in nodes}
    links[0] = 0  # synthetic head slot, corresponding to guest [4E0330]
    tail = 0
    for parent in parents:
        links[tail] = parent
        tail = parent
        for child in children[parent]:
            if links[child] == 0:
                links[tail] = child
                tail = child
    links[tail] = 0
    chain, seen = [], set()
    node = links[0]
    while node:
        if node in seen or node not in nodes:
            raise ValueError("Halo 2 descriptor chain is cyclic or invalid")
        seen.add(node)
        chain.append((node, nodes[node]))
        node = links[node]
    return chain


def game_initialization_roots(image):
    """Native41: 68 record callbacks, ESI=0..0x990 in steps of 0x24."""
    address, length, digest = GAME_INIT_WALK
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 game initialization walk fingerprint mismatch")
    roots = set()
    for slot in range(0x440DD8, 0x440DD8 + 0x990, 0x24):
        target = image.u32(slot)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError(f"Halo 2 game initialization slot {slot:#x} has invalid target {target!r}")
        roots.add(target)
    return roots


def game_descriptor_map_roots(image):
    """Native85: map setup/cleanup follow the existing chain via node+C4.

    Reuse the original linking algorithm, including shared/self children.
    Only callback fields18/1C are read; the original loops skip nulls.
    """
    for address, length, digest in GAME_DESCRIPTOR_MAP_WALKS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 descriptor map walk fingerprint mismatch")
    roots = set()
    for node, _ in game_descriptor_initialization_chain(image):
        for offset in (0x18, 0x1C):
            target = image.u32(node + offset)
            if target == 0:
                continue
            section = image.section_of(target) if target else None
            if not target or not image.is_code(target) or not section or section[4] != ".text":
                raise ValueError(f"Halo 2 descriptor map callback {node + offset:#x} has invalid target")
            roots.add(target)
    return roots


def game_descriptor_child_roots(image):
    """Native153: four callbacks walk each catalog parent's direct children.

    Validate the existing descriptor spans/link shape, but do not filter by
    the linked initialization chain: these callers use the child arrays.
    Require their null terminator before the mutable link field at C4h.
    """
    for address, length, digest in GAME_DESCRIPTOR_CHILD_WALKS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 descriptor child walk fingerprint mismatch")
    return descriptor_child_field_roots(image, (0x20, 0x24, 0x28, 0x2C))


def game_descriptor_object_roots(image):
    """Native156: original object dispatch uses the same direct child arrays.

    Preserve original arguments and AL aggregation in translated callers.
    Only fields proven by the complete fingerprinted walks are code.
    """
    for address, length, digest, _offset in GAME_DESCRIPTOR_OBJECT_WALKS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 descriptor object walk fingerprint mismatch")
    return descriptor_child_field_roots(image, tuple(spec[3] for spec in GAME_DESCRIPTOR_OBJECT_WALKS))


def descriptor_child_field_roots(image, offsets):
    """Share checked catalog/child bounds, without recursively following children."""
    game_descriptor_initialization_chain(image)
    roots = set()
    for slot in range(0x468630, 0x468664, 4):
        parent = image.u32(slot)
        for index in range(16):
            child = image.u32(parent + 0x84 + index * 4)
            if child == 0:
                break
            for offset in offsets:
                target = image.u32(child + offset)
                if target == 0:
                    continue
                section = image.section_of(target) if target else None
                if not target or not image.is_code(target) or not section or section[4] != ".text":
                    raise ValueError(f"Halo 2 descriptor child callback {child + offset:#x} has invalid target")
                roots.add(target)
        else:
            raise ValueError("Halo 2 descriptor child array is not terminated before its link")
    return roots


def game_pair_listener_roots(image):
    """Native167: original predicate and constructor-bound pair listeners.

    Keep the guest predicate, matrix routing, member bookkeeping and tailcalls.
    The default listener is an existing original ret4 body, not a host stub.
    """
    for address, length, digest in GAME_PAIR_LISTENER_CALLS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 pair listener fingerprint mismatch")
    roots = set()
    for slot in (0x4138C8, 0x41388C, 0x413890, 0x4137E8, 0x4137EC,
                 0x43E538, 0x43E53C):
        target = image.u32(slot)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError("Halo 2 pair listener target is not title code")
        roots.add(target)
    return roots


def game_bounds_insert_roots(image):
    """Native166: original stored bounds getter and following index insertion."""
    for address, length, digest in GAME_BOUNDS_INSERT_CALLS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 bounds insertion fingerprint mismatch")
    roots = set()
    for slot in (0x4137E4, 0x412604):
        target = image.u32(slot)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError("Halo 2 bounds insertion target is not title code")
        roots.add(target)
    return roots


def game_member_query_roots(image):
    """Native165 member query, original aggregate counter and update slots.

    Six constructor-bound tables share the queried getter. Do not infer one
    concrete runtime type from that shared address or scan adjacent methods.
    """
    for address, length, digest in GAME_MEMBER_QUERY_CALLS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 member query caller fingerprint mismatch")
    slots = [0x414FE0]  # Aggregate offset10h: add, or original negated removal.
    for address, length, digest, vtable in GAME_MEMBER_QUERY_CTORS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 member query constructor fingerprint mismatch")
        slots.extend(vtable + offset for offset in (4, 8, 0x14))
    roots = set()
    for slot in slots:
        target = image.u32(slot)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError("Halo 2 member query target is not title code")
        roots.add(target)
    return roots


def game_boot_factory_roots(image):
    """Native163: original startup factory, owned objects and arena free path.

    Keep original reference counts, destructor flags and allocator metadata.
    Only the observed factory method and proven deletion slots become roots.
    """
    for address, length, digest in (*GAME_BOOT_FACTORY_CALLS, *GAME_ARENA_BOOT_CALLS):
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 boot factory call fingerprint mismatch")
    roots = set()
    for slot in (0x411D20, 0x411D2C, 0x411D30, 0x411D3C, 0x411DA4,
                 0x4537A0, 0x4576B0, 0x4576B8):
        target = image.u32(slot)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError("Halo 2 boot factory target is not title code")
        roots.add(target)
    return roots


def game_arena_boot_roots(image):
    """Native162: original arena wrapper forwards to its aligned allocator."""
    for address, length, digest in GAME_ARENA_BOOT_CALLS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 arena boot call fingerprint mismatch")
    roots = set()
    for slot in (0x4576AC, 0x4576B4):  # arena vtable offsets8 and10h only
        target = image.u32(slot)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError("Halo 2 arena boot target is not title code")
        roots.add(target)
    return roots


def game_singleton_creator_roots(image):
    """Eight CRT-registered creator nodes used by original resolver2D7CA0.

    Root creators only. Original registration order, pending/retry handling,
    storage writes and object lifetime remain in translated code.
    """
    address, length, digest = GAME_SINGLETON_WALK
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 singleton resolver fingerprint mismatch")
    roots = set()
    for address, length, digest, node, storage, crt_slot in GAME_SINGLETON_REGISTRATIONS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 singleton registration fingerprint mismatch")
        if image.u32(crt_slot) != address:
            raise ValueError("Halo 2 singleton CRT binding mismatch")
        section = image.section_of(node)
        if (not section or section[4] != ".data" or node & 3 or
                node + 12 > section[0] + section[2] or len(image.bytes_at(node, 12)) != 12):
            raise ValueError("Halo 2 singleton node has invalid data span")
        if image.u32(node + 4) != 0 or image.u32(node + 8) != storage:
            raise ValueError("Halo 2 singleton initial node binding mismatch")
        section = image.section_of(storage)
        if (storage & 3 or not section or section[4] not in (".data", ".bss") or
                storage + 4 > section[0] + max(section[2], section[3])):
            raise ValueError("Halo 2 singleton storage has invalid image span")
        target = image.u32(node)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError("Halo 2 singleton creator is not title code")
        roots.add(target)
    return roots


def game_fixed_startup_roots(image):
    """Native161 reaches a fixed initializer and its paired disposal callback.

    The initializer's original getter selects static allocator4798B0. Only
    its allocation slot10h and release slot20h are proven here; other virtual
    calls retain checked dispatch. No callback or allocator is replaced.
    """
    for address, length, digest in GAME_FIXED_STARTUP_CALLS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 fixed startup call fingerprint mismatch")
    if image.u32(0x4798B0) != 0x45378C:
        raise ValueError("Halo 2 fixed startup allocator binding mismatch")
    roots = set()
    for slot in (0x461DF0, 0x461DF4, 0x45379C, 0x4537AC):
        target = image.u32(slot)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != ".text":
            raise ValueError("Halo 2 fixed startup target is not title code")
        roots.add(target)
    return roots


def game_action_callback_roots(image):
    """Native158: the original 60-entry action table shares four-word records.

    E6830 proves indices 0..59 and optional field4; E6900 calls field0,
    E6960 field8, and E69C0 fieldC. Keep their original AL/bit handling.
    The owned records occupy a separate bounded, aligned data arena.
    """
    for address, length, digest in GAME_ACTION_WALKS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 action callback walk fingerprint mismatch")
    records = set()
    for slot in range(0x4677C8, 0x4678B8, 4):
        record = image.u32(slot)
        section = image.section_of(record) if record else None
        if (not record or not 0x467564 <= record < 0x4677C4 or
                (record - 0x467564) % 16 or not section or section[4] != ".data" or
                record + 16 > section[0] + section[2] or
                len(image.bytes_at(record, 16)) != 16):
            raise ValueError("Halo 2 action callback record has invalid data span")
        records.add(record)
    roots = set()
    for record in records:
        for offset in (0, 4, 8, 12):
            target = image.u32(record + offset)
            if offset and target == 0:
                continue
            section = image.section_of(target) if target else None
            if not target or not image.is_code(target) or not section or section[4] != ".text":
                raise ValueError("Halo 2 action callback target is not title code")
            roots.add(target)
    return roots


def game_packed_vector_roots(image):
    """Native154/155 reach format 1 and the shared format 4/6 callbacks.

    Original binders select either triplet at +0/+12; both are proven for
    these rows. Do not infer other format rows or treat metadata as code.
    The whole-image revision gate also protects the observed row contents.
    """
    for address, length, digest in GAME_PACKED_VECTOR_BINDINGS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 packed vector binding fingerprint mismatch")
    roots = set()
    for base in (0x47FB4C, 0x47FBC4, 0x47FC14):
        for slot in range(base, base + 24, 4):
            target = image.u32(slot)
            section = image.section_of(target) if target else None
            if not target or not image.is_code(target) or not section or section[4] != ".text":
                raise ValueError("Halo 2 packed vector callback has invalid target")
            roots.add(target)
    return roots


def game_map_callback_roots(image):
    """Native83: four per-map lifecycle fields in the same 68-record table.

    Two forward walks cover ESI=0..0x990, stride0x24; their paired reverse
    walks cover the same records. Each original walk skips null callbacks.
    Other descriptor fields are not interpreted as code, and the original
    order, calls and state writes remain in generated code.
    """
    for address, length, digest in GAME_MAP_WALKS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 map callback walk fingerprint mismatch")
    roots = set()
    for base in (0x440DE0, 0x440DE4, 0x440DE8, 0x440DEC):
        for slot in range(base, base + 0x990, 0x24):
            target = image.u32(slot)
            if target == 0:
                continue
            section = image.section_of(target) if target else None
            if not target or not image.is_code(target) or not section or section[4] != ".text":
                raise ValueError(f"Halo 2 map callback slot {slot:#x} has invalid target")
            roots.add(target)
    return roots


def game_remaining_lifecycle_roots(image):
    """Native159 reaches field18 of the existing 68-record lifecycle table.

    Complete callers also prove the reverse required disposal field4 and
    optional mask-change fields1C/20. Preserve all original transitions.
    """
    for address, length, digest in GAME_REMAINING_LIFECYCLE_WALKS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 remaining lifecycle walk fingerprint mismatch")
    roots = set()
    for base in (0x440DDC, 0x440DF0, 0x440DF4, 0x440DF8):
        for slot in range(base, base + 0x990, 0x24):
            target = image.u32(slot)
            if base != 0x440DDC and target == 0:
                continue
            section = image.section_of(target) if target else None
            if not target or not image.is_code(target) or not section or section[4] != ".text":
                raise ValueError(f"Halo 2 remaining lifecycle slot {slot:#x} has invalid target")
            roots.add(target)
    return roots


def game_resource_callback_roots(image):
    """Native84: three original three-record walks, each at stride0x38.

    Only the called fields at 4674A4, 4674A8 and 4674B8 are followed. The
    metadata, other lifecycle fields and following records are not scanned.
    """
    for address, length, digest in GAME_RESOURCE_WALKS:
        if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
            raise ValueError("Halo 2 resource callback walk fingerprint mismatch")
    roots = set()
    for base in (0x4674A4, 0x4674A8, 0x4674B8):
        for index in range(3):
            slot = base + index * 0x38
            target = image.u32(slot)
            if target == 0:
                continue
            section = image.section_of(target) if target else None
            if not target or not image.is_code(target) or not section or section[4] != ".text":
                raise ValueError(f"Halo 2 resource callback slot {slot:#x} has invalid target")
            roots.add(target)
    return roots


def host_device_callback_roots(image):
    """Native37 XPP dispatch: six descriptor slots, initialization at +4."""
    address, length, digest = XPP_CALLBACK_WALK
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 XPP callback walk fingerprint mismatch")
    roots = set()
    for slot in range(0x4086D4, 0x4086EC, 4):
        descriptor = image.u32(slot)
        if descriptor == 0:
            continue
        section = image.section_of(descriptor) if descriptor else None
        if not descriptor or descriptor & 3 or not section or section[4] != "XPP":
            raise ValueError(f"Halo 2 XPP descriptor {slot:#x} is invalid")
        target = image.u32(descriptor + 4)
        section = image.section_of(target) if target else None
        if not target or not image.is_code(target) or not section or section[4] != "XPP":
            raise ValueError(f"Halo 2 XPP callback {descriptor:#x} is invalid")
        roots.add(target)
    return roots


def host_channel_callback_roots(image):
    """Exact default-state callback walk observed at 3FBACA in native attempt 25.

    EBX traverses E4..294 and ESI=EBX-170. The indirect call runs only for
    ESI>=B0 and EBX!=268, with EBP=403A48. This excludes slot403B40.
    The enclosing caller already has the whole-image revision gate in main().
    """
    address, length, digest = HOST_CALLBACK_WALK
    if hashlib.sha256(image.bytes_at(address, length)).hexdigest() != digest:
        raise ValueError("Halo 2 default-state callback walk fingerprint mismatch")
    roots = set()
    for slot in range(0x403AF8, 0x403B70, 4):
        if slot == 0x403B40:
            continue
        target = image.u32(slot)
        section = image.section_of(target) if target is not None else None
        if not target or not image.is_code(target) or not section or section[4] != "D3D":
            raise ValueError(f"Halo 2 default-state callback {slot:#x} has invalid target {target!r}")
        roots.add(target)
    return roots


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xbe", type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "local/halo2_5849/boot")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--graphics", action="store_true", help="enable the strict diagnostic NV2A bus adapter")
    mode.add_argument("--host-channel", action="store_true", help="enable the experimental synchronous command consumer (requires HOST_CHANNEL=1)")
    audio = parser.add_mutually_exclusive_group()
    audio.add_argument("--audio-unavailable", action="store_true", help="diagnostic only: DirectSoundCreate returns DSERR_NODRIVER (requires --host-channel)")
    audio.add_argument("--audio-host", action="store_true", help="bounded real-output audio device adapter (requires --host-channel and AUDIO_HOST=1)")
    args = parser.parse_args()
    if args.audio_unavailable and not args.host_channel:
        parser.error("--audio-unavailable requires --host-channel")
    if args.audio_host and not args.host_channel:
        parser.error("--audio-host requires --host-channel")
    image = Image(str(args.xbe))
    load_profile("halo2_5849").validate_image(image)
    # The observed XAPI initializer at 0x2D1D15 calls the first table, and its
    # following initializer at 0x2D1CBD calls the other two. These are bounded
    # direct table walks in this hash-pinned image, not a general data scan.
    roots = {image.entry}
    for start, end in ((0x461180, 0x46118C), (0x461814, 0x46182C), (0x461190, 0x461810), (0x461FA8, 0x461FAC)):
        for slot in range(start, end, 4):
            target = image.u32(slot)
            if target in (0, 0xFFFFFFFF):
                continue
            if target is None or not image.is_code(target):
                raise ValueError(f"Initializer slot {slot:#x} has invalid target {target!r}")
            roots.add(target)
    if args.host_channel:
        descriptor_chain = game_descriptor_initialization_chain(image)
        roots.update(target for _, target in descriptor_chain if target)
        roots.update(game_dispatch_vtable_roots(image))
        roots.update(game_mode_callback_roots(image))
        roots.update(game_allocator_vtable_roots(image))
        roots.update(game_registered_interface_roots(image))
        roots.update(game_state_vtable_roots(image))
        roots.update(game_startup_widget_vtable_roots(image))
        roots.update(game_text_widget_vtable_roots(image))
        roots.update(game_widget_property_roots(image))
        roots.update(game_online_interface_roots(image))
        roots.update(bink_pixel_callback_roots(image))
        roots.update(reviewed_sparse_jump_roots(image))
        roots.update(reviewed_widget_kind_roots(image))
        roots.update(reviewed_widget_field_roots(image))
        # Native49: 1A474C passes the global arena object 47D924 to 18E1F0.
        # Its stored vtable is 4508FC: allocate/free, followed by string data.
        if image.u32(0x47D924) != 0x4508FC:
            raise ValueError("Halo 2 global arena vtable binding mismatch")
        for slot in (0x4508FC, 0x450900):
            target = image.u32(slot)
            if not target or not image.is_code(target):
                raise ValueError(f"Halo 2 global arena slot {slot:#x} is not code")
            roots.add(target)
        roots.update(host_channel_callback_roots(image))
        # Native attempt 35: application creator 0x120A90 pushes 0x120C30
        # at 0x120B0E and calls XAPI thread creation at 0x120B3D. The native
        # worker dispatch reaches that exact entry. Whole-image gate above.
        roots.add(0x120C30)
        # Attempt 36 dispatches the allocator's slot zero at 0x453308.
        # The two-slot vtable contains allocate/free; the following bytes are
        # string data. Keep the whole-image revision guard and exact bounds.
        roots.update(image.u32(slot) for slot in (0x453308, 0x45330C))
        roots.update(host_device_callback_roots(image))
        # Attempt 39 reaches the sound object's four-slot vtable at 0x4170E4:
        # destructor, AddRef, Release, delete helper. Slot one is the observed
        # indirect dispatch from 0x37B17B. The following words are data.
        roots.update(image.u32(slot) for slot in range(0x4170E4, 0x4170F4, 4))
        roots.update(game_initialization_roots(image))
        roots.update(game_map_callback_roots(image))
        roots.update(game_remaining_lifecycle_roots(image))
        roots.update(game_resource_callback_roots(image))
        roots.update(game_descriptor_map_roots(image))
        roots.update(game_descriptor_child_roots(image))
        roots.update(game_descriptor_object_roots(image))
        roots.update(game_packed_vector_roots(image))
        roots.update(game_action_callback_roots(image))
        roots.update(game_fixed_startup_roots(image))
        roots.update(game_arena_boot_roots(image))
        roots.update(game_boot_factory_roots(image))
        roots.update(game_member_query_roots(image))
        roots.update(game_bounds_insert_roots(image))
        roots.update(game_pair_listener_roots(image))
        roots.update(game_singleton_creator_roots(image))
        # Native42: 0x66305 calls [ [0x477058] + 0x10 ]; the pinned record
        # is 0x467140, whose callback is 0x662E0 (ten-byte original body).
        roots.add(image.u32(image.u32(0x477058) + 0x10))
        # Native43: arena allocator call 0x14B4C2 uses the two-slot table
        # at 0x453498 (allocate/free); the following word begins string data.
        roots.update(image.u32(slot) for slot in (0x453498, 0x45349C))
        # Native44: D486D..D4885 selects three records at 4674A0, stride38h.
        # The loop calls only each record's first field and skips nulls.
        for slot in (0x4674A0, 0x4674D8, 0x467510):
            target = image.u32(slot)
            if target:
                if not image.is_code(target):
                    raise ValueError(f"Halo 2 resource initializer {slot:#x} is not code")
                roots.add(target)
    output = args.out.resolve()
    generated = output / "generated"
    profile = str(Path(__file__).with_name("graphics-profile.json")) if args.graphics else "halo2_5849"
    if args.host_channel:
        profile = str(Path(__file__).with_name("host-channel-profile.json"))
    if args.audio_unavailable:
        profile = str(Path(__file__).with_name("audio-unavailable-profile.json"))
    if args.audio_host:
        profile = str(Path(__file__).with_name("audio-host-profile.json"))
        # Header AddRef/Release are reached through the exact original vtable.
        roots.update((0x37A14F, 0x37C70F))
        roots.update(audio_stream_roots(image))
        roots.update(game_sound_owner_roots(image))
    subprocess.run([sys.executable, "-m", "recompiler", str(args.xbe.resolve()),
                    "--profile", profile, "--no-data-roots", "--trace-calls", "--trace-funcs",
                    "--files", "128", "-o", str(generated),
                    "--roots", f"{image.entry:X}", *(f"{root:X}" for root in sorted(roots - {image.entry}))], cwd=ROOT, check=True)
    # xv_game_main is the startup entry in this entry-only diagnostic target;
    # boot.c only uses xv_entry_point. No game-main boundary is asserted here.
    (output / "startup-roots.json").write_text(json.dumps(sorted(roots), indent=2) + "\n")
    if args.host_channel:
        (output / "descriptor-initializers.json").write_text(json.dumps(descriptor_chain, indent=2) + "\n")
    # Generated compatibility stubs return success. This diagnostic target
    # deliberately does not link them: absent kernel implementations halt.
    lines = ['#include "xv_x86rt.h"', 'void xv_boot_missing_kernel(xctx *c, const char *name);']
    for ordinal in sorted(set(image.kernel_imports().values()) - KERNEL_DATA_EXPORTS):
        name = KERNEL_EXPORTS[ordinal]
        lines += [f'void xk_{name}(xctx *c) __attribute__((weak));',
                  f'void xk_{name}(xctx *c) {{ xv_boot_missing_kernel(c, "{name}"); }}']
    (generated / "strict-kernel.c").write_text("\n".join(lines) + "\n")
    manifest = output / "manifest.json"
    manifest.write_text(json.dumps(image.m, indent=2) + "\n")
    subprocess.run([sys.executable, str(ROOT / "recompiler/xbe_image.py"), str(args.xbe.resolve()),
                    str(manifest), str(output / "halo2_image.bin")], check=True)


if __name__ == "__main__":
    main()
