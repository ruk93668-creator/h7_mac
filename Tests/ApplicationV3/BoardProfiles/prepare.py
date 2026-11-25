"""Prepare a disposable ApplicationV3 test tree with the historical test seams."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory


ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
SOURCE_FOLDERS = ("Board", "CM4", "CM7", "Common", "Drivers", "MDK-ARM", "Middlewares", "Tests")


def prepare(destination):
    destination.mkdir(parents=True, exist_ok=False)
    if (ROOT / ".git").exists():
        with TemporaryDirectory(prefix="appv3-source-") as scratch:
            source_zip = Path(scratch) / "source.zip"
            subprocess.run(["git", "archive", "--format=zip", "-o", str(source_zip), "HEAD"],
                           cwd=ROOT, check=True)
            shutil.unpack_archive(source_zip, destination)
    else:
        for name in SOURCE_FOLDERS:
            shutil.copytree(ROOT / name, destination / name, ignore=shutil.ignore_patterns(
                "__pycache__", "h7_mac_CM7", "h7_mac_CM4", "*.uvguix.*", "python_deps"))
    test_dir = destination / HERE.relative_to(ROOT)
    target_tasks = destination / "CM7/ApplicationV3/Tasks"
    for filename in ("board_smoke.h", "record_board.h"):
        shutil.copyfile(test_dir / filename, target_tasks / filename)

    environment = dict(os.environ, GIT_CEILING_DIRECTORIES=str(destination.parent))
    patch = test_dir / "test_only.patch"
    subprocess.run(["git", "apply", "--check", "--whitespace=nowarn", str(patch)],
                   env=environment, cwd=destination, check=True)
    subprocess.run(["git", "apply", "--whitespace=nowarn", str(patch)],
                   env=environment, cwd=destination, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    arguments = parser.parse_args()
    prepare(arguments.destination.resolve())


if __name__ == "__main__":
    main()
