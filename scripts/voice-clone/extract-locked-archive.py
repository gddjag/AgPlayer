#!/usr/bin/env python3
"""Verify and extract a small locked Runtime build archive without links."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import tarfile
import zipfile


MAX_FILES = 50_000
MAX_FILE_BYTES = 512 * 1024 * 1024
MAX_TOTAL_BYTES = 2 * 1024 * 1024 * 1024
MAX_COMPRESSION_RATIO = 200
WINDOWS_RESERVED = {
    "con",
    "prn",
    "aux",
    "nul",
    *(f"com{i}" for i in range(1, 10)),
    *(f"lpt{i}" for i in range(1, 10)),
}


def safe_relative(name: str, archive_root: str) -> str | None:
    normalized = name.replace("\\", "/")
    if "\\" in name or "\0" in name or normalized.startswith("/"):
        raise ValueError(f"unsafe archive path: {name}")
    parts = pathlib.PurePosixPath(normalized).parts
    if not parts or parts[0] != archive_root:
        raise ValueError(f"archive entry is outside the locked root: {name}")
    relative_parts = parts[1:]
    if not relative_parts:
        return None
    for part in relative_parts:
        if part in {"", ".", ".."} or part.endswith((" ", ".")) or ":" in part:
            raise ValueError(f"unsafe archive component: {name}")
        if any(ord(char) < 32 for char in part):
            raise ValueError(f"control character in archive path: {name}")
        stem = part.split(".", 1)[0].casefold()
        if stem in WINDOWS_RESERVED:
            raise ValueError(f"Windows device name in archive path: {name}")
    return "/".join(relative_parts)


def read_zip(path: pathlib.Path, archive_root: str) -> dict[str, bytes]:
    files: dict[str, bytes] = {}
    folded: set[str] = set()
    total = 0
    with zipfile.ZipFile(path, "r") as archive:
        if len(archive.infolist()) > MAX_FILES * 2:
            raise ValueError("archive entry limit exceeded")
        for info in archive.infolist():
            relative = safe_relative(info.filename, archive_root)
            unix_type = (info.external_attr >> 16) & 0xF000
            if unix_type == 0xA000 or (info.external_attr & 0x400):
                raise ValueError(f"link or reparse entry is forbidden: {info.filename}")
            if info.is_dir():
                continue
            if relative is None or info.flag_bits & 0x1:
                raise ValueError(f"invalid or encrypted ZIP entry: {info.filename}")
            if info.compress_type not in {zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED}:
                raise ValueError(f"unsupported ZIP method: {info.filename}")
            if info.file_size > MAX_FILE_BYTES:
                raise ValueError(f"archive file limit exceeded: {info.filename}")
            if info.compress_size and info.file_size / info.compress_size > MAX_COMPRESSION_RATIO:
                raise ValueError(f"archive compression ratio exceeded: {info.filename}")
            key = relative.casefold()
            if key in folded:
                raise ValueError(f"duplicate Windows archive path: {relative}")
            data = archive.read(info)
            if len(data) != info.file_size:
                raise ValueError(f"truncated ZIP entry: {info.filename}")
            total += len(data)
            if total > MAX_TOTAL_BYTES:
                raise ValueError("archive expanded-size limit exceeded")
            folded.add(key)
            files[relative] = data
    return files


def read_tar_gz(path: pathlib.Path, archive_root: str) -> dict[str, bytes]:
    files: dict[str, bytes] = {}
    folded: set[str] = set()
    total = 0
    with tarfile.open(path, "r:gz") as archive:
        members = archive.getmembers()
        if len(members) > MAX_FILES * 2:
            raise ValueError("archive entry limit exceeded")
        for member in members:
            relative = safe_relative(member.name, archive_root)
            if member.isdir():
                continue
            if relative is None or not member.isfile():
                raise ValueError(f"special TAR entry is forbidden: {member.name}")
            if member.size > MAX_FILE_BYTES:
                raise ValueError(f"archive file limit exceeded: {member.name}")
            key = relative.casefold()
            if key in folded:
                raise ValueError(f"duplicate Windows archive path: {relative}")
            stream = archive.extractfile(member)
            if stream is None:
                raise ValueError(f"unreadable TAR entry: {member.name}")
            data = stream.read(MAX_FILE_BYTES + 1)
            if len(data) != member.size:
                raise ValueError(f"truncated TAR entry: {member.name}")
            total += len(data)
            if total > MAX_TOTAL_BYTES:
                raise ValueError("archive expanded-size limit exceeded")
            folded.add(key)
            files[relative] = data
    return files


def tree_digest(files: dict[str, bytes]) -> tuple[int, str]:
    digest = hashlib.sha256()
    total = 0
    for name in sorted(files):
        data = files[name]
        total += len(data)
        file_hash = hashlib.sha256(data).hexdigest()
        digest.update(f"{name}\t{len(data)}\t{file_hash}\n".encode("utf-8"))
    return total, digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", required=True, type=pathlib.Path)
    parser.add_argument("--format", required=True, choices=("zip", "tar.gz"))
    parser.add_argument("--archive-root", required=True)
    parser.add_argument("--file-count", required=True, type=int)
    parser.add_argument("--expanded-bytes", required=True, type=int)
    parser.add_argument("--tree-sha256", required=True)
    parser.add_argument("--destination", required=True, type=pathlib.Path)
    args = parser.parse_args()

    if not re.fullmatch(r"[0-9a-f]{64}", args.tree_sha256):
        raise ValueError("tree SHA-256 is invalid")
    if args.destination.exists():
        raise ValueError("destination already exists")
    files = (
        read_zip(args.archive, args.archive_root)
        if args.format == "zip"
        else read_tar_gz(args.archive, args.archive_root)
    )
    expanded_bytes, actual_tree = tree_digest(files)
    if len(files) != args.file_count or expanded_bytes != args.expanded_bytes:
        raise ValueError("archive file-count or expanded-size mismatch")
    if actual_tree != args.tree_sha256:
        raise ValueError("archive normalized tree SHA-256 mismatch")

    args.destination.mkdir(parents=False)
    for relative, data in files.items():
        target = args.destination.joinpath(*pathlib.PurePosixPath(relative).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as stream:
            stream.write(data)
    print(f"LOCKED_ARCHIVE_READY:{len(files)}:{expanded_bytes}:{actual_tree}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
