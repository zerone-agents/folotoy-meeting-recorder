#!/usr/bin/env python3
"""Package the verified public firmware and its release metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

# Packaging only writes artifacts inside the requested output directory.
sys.dont_write_bytecode = True
from archive_firmware import FULL_BIN, read_artifact, safe_path, verify_archive
from verify_firmware import REQUIRED_IMAGES


def package_release(build_dir: Path, output_dir: Path, version: str, commit: str) -> None:
    if len(version) > 31 or not re.fullmatch(r"(?:0|[1-9][0-9]*)(?:\.(?:0|[1-9][0-9]*)){2}", version):
        raise ValueError("firmware version must have the form MAJOR.MINOR.PATCH and fit in 31 bytes")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("source commit must be a full lowercase Git SHA")

    build_dir = safe_path(build_dir)
    merged = read_artifact(build_dir, FULL_BIN)
    checksum = hashlib.sha256(merged).hexdigest()
    manifest = verify_archive(build_dir / "firmware" / checksum)
    descriptor = manifest["app_descriptor"]
    if descriptor["version"] != version:
        raise ValueError(f"expected embedded version {version}, found {descriptor['version']!r}")

    offsets = manifest["image_offsets"]
    if set(offsets) != set(REQUIRED_IMAGES):
        raise ValueError("release firmware must contain only the bootloader, partition table and application")
    end = 0
    for name, offset in sorted(offsets.items(), key=lambda item: item[1]):
        if merged[end:offset].strip(b"\xff"):
            raise ValueError("release firmware contains data between images")
        end = offset + manifest["files"][name]["size"]
    if merged[end:].strip(b"\xff"):
        raise ValueError("release firmware contains data after the application")

    name = f"meeting-recorder-firmware-v{version}-full.bin"
    metadata = {
        "schema_version": 1,
        "version": version,
        "tag": f"firmware-v{version}",
        "source_commit": commit,
        "target": manifest["target"],
        "idf_version": descriptor["idf_version"],
        "flash_size_bytes": manifest["flash_size_bytes"],
        "app_elf_sha256": manifest["app_elf_sha256"],
        "artifact": {"filename": name, "sha256": checksum, "size": len(merged), "flash_offset": 0},
    }
    output_dir = safe_path(output_dir)
    output_dir.mkdir()
    for filename, data in (
        (name, merged),
        ("SHA256SUMS", f"{checksum}  {name}\n".encode("ascii")),
        ("firmware-release.json", (json.dumps(metadata, indent=2) + "\n").encode("utf-8")),
    ):
        with (output_dir / filename).open("xb") as artifact:
            artifact.write(data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()

    try:
        package_release(args.build_dir, args.output_dir, args.version, args.commit)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
