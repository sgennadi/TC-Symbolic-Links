#!/usr/bin/env python3
"""Restricted forced-command SSH helper: a POSIX directory symlink inside one share.

The SSH public key must be restricted to this executable with a fixed
--share and --root. Run as a non-root service user with existing permissions.
Protocol: TCS-POSIX-1 and three base64 UTF-8 lines (share, source, link).
Replies: TCS-POSIX-OK or TCS-POSIX-ERR:<reason>. No shell commands in input.
"""
import argparse
import base64
import binascii
import os
import posixpath
import stat
import sys

MAX_INPUT = 16384
O_DIR = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC


class Denied(Exception):
    def __init__(self, reason):
        self.reason = reason


def decode_field(text):
    try:
        if len(text) > 8192:
            raise Denied("invalid-request")
        return base64.b64decode(text, validate=True).decode("utf-8", errors="strict")
    except (binascii.Error, UnicodeError, ValueError) as exc:
        raise Denied("invalid-request") from exc


def components(path):
    if not isinstance(path, str) or not path or len(path.encode("utf-8")) > 4096:
        raise Denied("invalid-path")
    if "\\" in path or "\x00" in path or "\r" in path or "\n" in path:
        raise Denied("invalid-path")
    parts = path.split("/")
    if any(piece in ("", ".", "..") for piece in parts):
        raise Denied("invalid-path")
    return parts


def open_directory(root_fd, parts):
    fd = os.dup(root_fd)
    try:
        for piece in parts:
            next_fd = os.open(piece, O_DIR, dir_fd=fd)
            os.close(fd)
            fd = next_fd
        return fd
    except Exception:
        os.close(fd)
        raise


def read_request(stream):
    raw = stream.buffer.read(MAX_INPUT + 1)
    if len(raw) > MAX_INPUT:
        raise Denied("invalid-request")
    try:
        lines = raw.decode("ascii", errors="strict").splitlines()
    except UnicodeError as exc:
        raise Denied("invalid-request") from exc
    if len(lines) != 4 or lines[0] != "TCS-POSIX-1":
        raise Denied("invalid-request")
    return tuple(decode_field(value) for value in lines[1:])


def apply(root, allowed_share, request):
    share, source, link = request
    if share.casefold() != allowed_share.casefold():
        raise Denied("share-mismatch")
    source_parts, link_parts = components(source), components(link)
    root_fd = os.open(root, O_DIR)
    try:
        # No symlinks in source or link ancestors, including the share root.
        source_fd = open_directory(root_fd, source_parts)
        os.close(source_fd)
        parent_fd = open_directory(root_fd, link_parts[:-1])
        try:
            leaf = link_parts[-1]
            try:
                os.stat(leaf, dir_fd=parent_fd, follow_symlinks=False)
                raise Denied("already-exists")
            except FileNotFoundError:
                pass
            parent = "/".join(link_parts[:-1]) or "."
            target = posixpath.relpath("/".join(source_parts), start=parent)
            if target in ("", "."):
                raise Denied("invalid-path")
            # Atomic no-overwrite creation under the already validated dirfd.
            os.symlink(target, leaf, dir_fd=parent_fd)
            created = os.stat(leaf, dir_fd=parent_fd, follow_symlinks=False)
            if not stat.S_ISLNK(created.st_mode) or os.readlink(leaf, dir_fd=parent_fd) != target:
                raise Denied("verify-failed")
        finally:
            os.close(parent_fd)
    finally:
        os.close(root_fd)


def main():
    parser = argparse.ArgumentParser(description="Restricted TC POSIX symlink helper")
    parser.add_argument("--share", required=True)
    parser.add_argument("--root", required=True)
    args = parser.parse_args()
    try:
        if not args.share or "/" in args.share or "\\" in args.share:
            raise Denied("invalid-share-config")
        if not os.path.isabs(args.root):
            raise Denied("invalid-root-config")
        apply(args.root, args.share, read_request(sys.stdin))
        print("TCS-POSIX-OK", flush=True)
        return 0
    except Denied as exc:
        print("TCS-POSIX-ERR:" + exc.reason, flush=True)
    except FileExistsError:
        print("TCS-POSIX-ERR:already-exists", flush=True)
    except PermissionError:
        print("TCS-POSIX-ERR:access-denied", flush=True)
    except (FileNotFoundError, NotADirectoryError):
        print("TCS-POSIX-ERR:source-or-parent-missing", flush=True)
    except (OSError, ValueError):
        print("TCS-POSIX-ERR:filesystem-error", flush=True)
    return 1


if __name__ == "__main__":
    sys.exit(main())
