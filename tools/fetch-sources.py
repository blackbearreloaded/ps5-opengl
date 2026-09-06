#!/usr/bin/env python3
"""Fetch immutable public sources; never reset an existing working tree."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
PINS = json.loads((ROOT / "dependencies.json").read_text())


def git(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *args], text=True).strip()


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify_psbc():
    path = ROOT / "third_party/opengnm-psbc"
    pin = PINS["psbc_patch"]
    if digest(ROOT / pin["path"]) != pin["sha256"]:
        raise ValueError("compiler patch hash mismatch")
    if git(path, "write-tree") != pin["patched_tree"]:
        raise ValueError("compiler source tree mismatch; run make source-fetch")
    git(path, "diff", "--exit-code", "--quiet")
    print("PSBC: exact validated source tree verified", flush=True)


def fetch_repo(name, pin):
    path = ROOT / "third_party" / name
    if not path.exists():
        path.mkdir()
        git(path, "init", "--quiet")
        git(path, "remote", "add", "origin", pin["url"])
        git(path, "fetch", "--depth=1", "origin", pin["revision"])
        git(path, "checkout", "--quiet", "--detach", "FETCH_HEAD")
    revision = git(path, "rev-parse", "HEAD")
    allowed = {pin["revision"]}
    if name == "opengnm-psbc":
        allowed.add(PINS["psbc_patch"]["research_commit"])
    if revision not in allowed:
        raise ValueError(f"{name}: unexpected checkout; refusing to replace it")
    git(path, "diff", "--exit-code", "--quiet")
    if name == "opengnm-psbc":
        patch = PINS["psbc_patch"]
        if git(path, "write-tree") != patch["patched_tree"]:
            git(path, "diff", "--cached", "--exit-code", "--quiet")
            if digest(ROOT / patch["path"]) != patch["sha256"]:
                raise ValueError("compiler patch checksum mismatch")
            git(path, "apply", "--index", str(ROOT / patch["path"]))
        verify_psbc()
    else:
        git(path, "diff", "--cached", "--exit-code", "--quiet")
    print(f"{name}: pinned {revision}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cts", action="store_true", help="also fetch the optional CTS source")
    parser.add_argument("--verify-psbc", action="store_true", help="offline compiler-source check only")
    args = parser.parse_args()
    if args.verify_psbc:
        verify_psbc()
        return
    sources = ROOT / "third_party"
    sources.mkdir(exist_ok=True)
    for name, pin in PINS["repositories"].items():
        if not pin.get("optional") or args.cts:
            fetch_repo(name, pin)
    mesa = PINS["mesa"]
    archive = sources / f"mesa-{mesa['version']}.tar.xz"
    if not archive.exists():
        temporary = archive.with_suffix(".download")
        urllib.request.urlretrieve(mesa["url"], temporary)
        if digest(temporary) != mesa["sha256"]:
            raise ValueError("Mesa download checksum mismatch; not promoted")
        temporary.replace(archive)
    if digest(archive) != mesa["sha256"]:
        raise ValueError("existing Mesa archive checksum mismatch; file preserved")
    directory = sources / f"mesa-{mesa['version']}"
    if not directory.exists():
        with tarfile.open(archive) as stream:
            stream.extractall(sources, filter="data")
    if (directory / "VERSION").read_text().strip() != mesa["version"]:
        raise ValueError("unexpected Mesa source version")
    print("Mesa: archive verified; sources ready for the recorded platform patch")


if __name__ == "__main__":
    main()
