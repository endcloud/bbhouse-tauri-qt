#!/usr/bin/env python3
"""Read-only repository audit. Never print matched credentials or file contents."""
import argparse
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SENSITIVE_PATH = re.compile(
    r"(?i)(cookie[^/]*\.(txt|json)$|\.(db|sqlite3?|log|pem|key|p12|pfx)$|"
    r"(^|/)(\.env(?:\.[^/]+)?|special-follow\.json|history-service\.json|"
    r"bilibili-history-export\.json|\.DS_Store|avatar-cache)(/|$))"
)
PATTERNS = {
    "credential": re.compile(
        rb"(?i)(?:SESSDATA|bili_jct|access_token|refresh_token)\s*[=:\"\s]+"
        rb"([A-Za-z0-9%_+/.=-]{16,})"
    ),
    "private-key": re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    "github-token": re.compile(
        rb"(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})"
    ),
    "signed-media": re.compile(
        rb"(?i)[?&](?:wsSecret|sign|token|upsig|deadline)=[a-f0-9%]{24,}"
    ),
    "sqlite-database": re.compile(rb"\ASQLite format 3\x00"),
}


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history", action="store_true", help="Also audit all reachable Git blobs")
    args = parser.parse_args()
    findings = set()

    def inspect(path, data, revision):
        for label, pattern in PATTERNS.items():
            if pattern.search(data):
                findings.add((label, path, revision))

    files = git("ls-files", "-z").decode().split("\0")[:-1]
    for path in files:
        if SENSITIVE_PATH.search(path) and not path.endswith(".env.example"):
            findings.add(("sensitive-path", path, "working-tree"))
        target = ROOT / path
        # Do not follow a tracked link into user data outside this repository.
        if target.is_file() and not target.is_symlink():
            inspect(path, target.read_bytes(), "working-tree")
    blobs = 0
    if args.history:
        objects = git("rev-list", "--objects", "--all").decode().splitlines()
        with subprocess.Popen(["git", "cat-file", "--batch"], cwd=ROOT,
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
            for row in objects:
                oid, _, path = row.partition(" ")
                process.stdin.write((oid + "\n").encode())
                process.stdin.flush()
                header = process.stdout.readline().split()
                data = process.stdout.read(int(header[2]))
                process.stdout.read(1)
                if header[1] == b"blob":
                    blobs += 1
                    inspect(path, data, oid[:12])
                    if SENSITIVE_PATH.search(path) and not path.endswith(".env.example"):
                        findings.add(("sensitive-path", path, oid[:12]))
            process.stdin.close()
    print(f"Tracked files: {len(files)}; historical blobs: {blobs}; findings: {len(findings)}")
    for label, path, revision in sorted(findings):
        print(f"{label}: {path} ({revision}); matched contents withheld")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
