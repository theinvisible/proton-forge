#!/usr/bin/env python3
"""Regenerate the ZIP fixtures for tst_gogzip.

Run from the repository root:

    python3 tests/steam-lab/fixtures/gog/make-zip-fixtures.py

Nothing here is captured from GOG — a real Linux installer is several gigabytes
and carries the game. These are hand-built archives that reproduce the three
shapes the reader has to cope with, and only those:

  sfx-installer.sh   a shell header with a ZIP appended, exactly how GOG ships
                     Linux builds. Every offset inside is relative to where the
                     ZIP starts, not to the start of the file.
  zip64.zip          the ZIP64 records, with the classic EOCD holding the
                     0xFFFF/0xFFFFFFFF sentinels. Real installers are over 4 GB;
                     a fixture cannot be, so the records are written by hand.
  multipart.zip      an EOCD claiming the central directory lives on another
                     disk, which is what a split .sh + .bin installer looks like.
"""

import pathlib
import struct
import zipfile

HERE = pathlib.Path(__file__).resolve().parent

SHELL_HEADER = b"""#!/bin/sh
# GOG.com installer (fixture). The payload is the ZIP appended below; this
# header is what makes every offset in it relative rather than absolute.
echo "This is a self-extracting installer."
exit 0
# --- payload follows ---
"""

# Deliberately compressible, so the deflated entry is genuinely smaller than its
# input and a size mix-up shows up as wrong bytes rather than by luck.
GAME_BINARY = b"ELF" + b"\x90" * 4000
START_SH = b"#!/bin/sh\nexec ./game/bin/game \"$@\"\n"


def build_payload() -> bytes:
    """A ZIP laid out like a GOG Linux installer's."""
    scratch = HERE / "_scratch.zip"
    with zipfile.ZipFile(scratch, "w", zipfile.ZIP_DEFLATED) as z:
        # Directory entry, as the installers emit them.
        z.writestr(zipfile.ZipInfo("data/noarch/"), b"")

        # The launcher: executable, and what Game::executablePath ends up as.
        launcher = zipfile.ZipInfo("data/noarch/start.sh")
        launcher.external_attr = (0o100755 << 16)
        launcher.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(launcher, START_SH)

        # A deflated payload file.
        binary = zipfile.ZipInfo("data/noarch/game/bin/game")
        binary.external_attr = (0o100755 << 16)
        binary.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(binary, GAME_BINARY)

        # A stored (uncompressed) entry — method 0 is a separate code path.
        stored = zipfile.ZipInfo("data/noarch/game/data/stored.dat")
        stored.external_attr = (0o100644 << 16)
        stored.compress_type = zipfile.ZIP_STORED
        z.writestr(stored, b"stored bytes, not deflated")

        # A symlink: mode 0120000, target as the content.
        link = zipfile.ZipInfo("data/noarch/game/latest")
        link.external_attr = (0o120777 << 16)
        z.writestr(link, b"bin/game")

        # Outside data/noarch — the extractor must skip these.
        z.writestr("scripts/postinst.sh", b"#!/bin/sh\necho no\n")
        z.writestr("meta/gameinfo", b"Fixture Game\n1.0\n")

        # A traversal attempt. ZipReader::safeName has to refuse it.
        z.writestr("data/noarch/../../escaped.txt", b"should never be written")

    payload = scratch.read_bytes()
    scratch.unlink()
    return payload


def write_sfx(payload: bytes) -> None:
    (HERE / "sfx-installer.sh").write_bytes(SHELL_HEADER + payload)


def write_zip64(payload: bytes) -> None:
    """Rewrite a normal archive's tail as ZIP64.

    Python only emits ZIP64 records when it has to, and making it have to means
    a >4 GB fixture. So the classic EOCD is replaced by the three-record form a
    real ZIP64 archive ends with: the ZIP64 EOCD record, its locator, and a
    classic EOCD whose fields are all sentinels.
    """
    eocd_pos = payload.rindex(struct.pack("<I", 0x06054B50))
    eocd = payload[eocd_pos:]
    entries = struct.unpack("<H", eocd[10:12])[0]
    cd_size = struct.unpack("<I", eocd[12:16])[0]
    cd_offset = struct.unpack("<I", eocd[16:20])[0]

    body = payload[:eocd_pos]

    zip64_eocd = struct.pack(
        "<IQHHIIQQQQ",
        0x06064B50,   # signature
        44,           # size of the remainder of this record
        45, 45,       # version made by / needed
        0, 0,         # this disk / disk with the central directory
        entries, entries,
        cd_size, cd_offset,
    )
    locator = struct.pack("<IIQI", 0x07064B50, 0, len(body), 1)
    # Sentinels everywhere, so a reader that ignores the ZIP64 records reads
    # nonsense rather than something plausible.
    classic = struct.pack("<IHHHHIIH", 0x06054B50, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
                          0xFFFFFFFF, 0xFFFFFFFF, 0)

    (HERE / "zip64.zip").write_bytes(body + zip64_eocd + locator + classic)


def write_multipart(payload: bytes) -> None:
    """An EOCD that says the central directory is on another disk."""
    eocd_pos = payload.rindex(struct.pack("<I", 0x06054B50))
    eocd = bytearray(payload[eocd_pos:])
    eocd[4:6] = struct.pack("<H", 1)    # this disk
    eocd[6:8] = struct.pack("<H", 0)    # the central directory started on disk 0
    (HERE / "multipart.zip").write_bytes(payload[:eocd_pos] + bytes(eocd))


def main() -> None:
    payload = build_payload()
    write_sfx(payload)
    write_zip64(payload)
    write_multipart(payload)
    for name in ("sfx-installer.sh", "zip64.zip", "multipart.zip"):
        print(f"  {name}: {(HERE / name).stat().st_size} bytes")


if __name__ == "__main__":
    main()
