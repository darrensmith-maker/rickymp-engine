#!/usr/bin/env python3
"""Restore tracked file mtimes from Git for reliable cached Ninja builds.

A fresh GitHub Actions checkout gives every source file a new filesystem mtime.
When a Ninja build directory is restored from cache, those fresh mtimes make all
sources look newer than their cached object files. This utility restores each
tracked file to the timestamp of its most recent commit and can additionally
mark files changed since the cached revision as freshly modified.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import time
from typing import Iterable


_MARKER = "@@ARENAMP_COMMIT_TIME@@"


def run_git(repo: Path, *args: str, check: bool = True) -> bytes:
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if check and result.returncode != 0:
        message = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"git {' '.join(args)} failed in {repo}: {message}")
    return result.stdout


def tracked_files(repo: Path) -> set[str]:
    output = run_git(repo, "ls-files", "-z")
    return {
        item.decode("utf-8", errors="surrogateescape")
        for item in output.split(b"\0")
        if item
    }


def latest_commit_mtimes(repo: Path, tracked: set[str]) -> dict[str, int]:
    mtimes: dict[str, int] = {}
    current_timestamp: int | None = None
    output = run_git(
        repo,
        "log",
        f"--format={_MARKER}%ct",
        "--name-only",
        "--no-renames",
        "--",
        ".",
    ).decode("utf-8", errors="surrogateescape")

    for raw_line in output.splitlines():
        line = raw_line.rstrip("\r")
        if line.startswith(_MARKER):
            current_timestamp = int(line[len(_MARKER) :])
            continue
        if not line or current_timestamp is None:
            continue
        if line in tracked and line not in mtimes:
            mtimes[line] = current_timestamp
            if len(mtimes) == len(tracked):
                break

    fallback = int(
        run_git(repo, "log", "-1", "--format=%ct").decode("ascii").strip()
        or time.time()
    )
    for relative_path in tracked:
        mtimes.setdefault(relative_path, fallback)
    return mtimes


def set_mtime(path: Path, timestamp: int) -> bool:
    try:
        if path.exists() or path.is_symlink():
            os.utime(path, (timestamp, timestamp), follow_symlinks=False)
            return True
    except (NotImplementedError, OSError):
        # Windows checkouts normally contain regular files, but fall back to
        # following the link if the platform cannot update a symlink itself.
        try:
            os.utime(path, (timestamp, timestamp))
            return True
        except OSError:
            return False
    return False


def configured_submodules(repo: Path) -> list[Path]:
    gitmodules = repo / ".gitmodules"
    if not gitmodules.is_file():
        return []

    output = run_git(
        repo,
        "config",
        "--file",
        ".gitmodules",
        "--get-regexp",
        r"^submodule\..*\.path$",
        check=False,
    ).decode("utf-8", errors="surrogateescape")

    result: list[Path] = []
    for line in output.splitlines():
        parts = line.split(maxsplit=1)
        if len(parts) != 2:
            continue
        candidate = repo / parts[1]
        if not candidate.exists():
            continue
        probe = run_git(candidate, "rev-parse", "--is-inside-work-tree", check=False)
        if probe.strip() == b"true":
            result.append(candidate)
    return result


def restore_repo(repo: Path) -> int:
    tracked = tracked_files(repo)
    mtimes = latest_commit_mtimes(repo, tracked)
    updated = 0
    for relative_path, timestamp in mtimes.items():
        if set_mtime(repo / relative_path, timestamp):
            updated += 1

    for submodule in configured_submodules(repo):
        updated += restore_repo(submodule)
    return updated


def changed_paths(repo: Path, revision: str) -> Iterable[Path]:
    output = run_git(repo, "diff", "--name-only", "-z", f"{revision}..HEAD", "--")
    for raw_path in output.split(b"\0"):
        if raw_path:
            yield repo / raw_path.decode("utf-8", errors="surrogateescape")


def mark_changed(repo: Path, revision: str) -> int:
    now = int(time.time())
    updated = 0
    for path in changed_paths(repo, revision):
        if path.is_dir():
            # A changed gitlink means the checked-out submodule revision moved.
            # Mark all of its tracked files as new relative to cached objects.
            try:
                files = tracked_files(path)
            except RuntimeError:
                files = set()
            for relative_path in files:
                if set_mtime(path / relative_path, now):
                    updated += 1
        elif set_mtime(path, now):
            updated += 1
    return updated


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--repository",
        type=Path,
        default=Path.cwd(),
        help="Git worktree to process (default: current directory)",
    )
    parser.add_argument(
        "--touch-changed-since",
        metavar="REVISION",
        help="After restoring Git mtimes, give files changed since REVISION a fresh mtime",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo = args.repository.resolve()
    try:
        restored = restore_repo(repo)
        changed = 0
        if args.touch_changed_since:
            changed = mark_changed(repo, args.touch_changed_since)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"Restored Git mtimes for {restored} tracked files; refreshed {changed} changed files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
