#!/usr/bin/env python3
"""Remove only credentials created by public_gateway_smoke.py."""

import os
import sqlite3
import stat
import sys
from pathlib import Path


DIGESTS = Path("/tmp/public_gateway_smoke_digests.txt")
REGISTRY = Path("/var/lib/plane-pet/gateway_tokens.db")
TELEMETRY = Path("/var/lib/plane-pet/telemetry.db")


def main() -> None:
    use_digest_file = len(sys.argv) == 1
    requested = ({line.strip() for line in DIGESTS.read_text(
        encoding="ascii").splitlines()} if use_digest_file else set(sys.argv[1:]))
    if not requested or any(
            len(value) != 64 or any(char not in "0123456789abcdef" for char in value)
            for value in requested):
        raise RuntimeError("invalid smoke-test credential digest list")

    registry_stat = REGISTRY.stat() if REGISTRY.exists() else None
    original = REGISTRY.read_text(encoding="ascii").splitlines() if registry_stat else []
    retained = [line for line in original if line.split(" ", 1)[0] not in requested]
    removed = len(original) - len(retained)
    if removed != len(requested):
        raise RuntimeError(
            f"expected to remove {len(requested)} credentials, found {removed}")

    temporary = REGISTRY.with_suffix(".cleanup.tmp")
    with temporary.open("w", encoding="ascii", newline="\n") as output:
        for line in retained:
            output.write(line + "\n")
        output.flush()
        os.fsync(output.fileno())
    if registry_stat:
        os.chmod(temporary, stat.S_IMODE(registry_stat.st_mode))
        os.chown(temporary, registry_stat.st_uid, registry_stat.st_gid)
    else:
        os.chmod(temporary, 0o600)
    os.replace(temporary, REGISTRY)
    telemetry_removed = 0
    if TELEMETRY.exists():
        with sqlite3.connect(TELEMETRY) as database:
            for digest in requested:
                cursor = database.execute(
                    "DELETE FROM telemetry_events WHERE installation_id=?",
                    (digest[:24],))
                telemetry_removed += cursor.rowcount
            database.commit()
    if use_digest_file:
        DIGESTS.unlink()
    print(f"PUBLIC_GATEWAY_SMOKE_CLEANUP_OK removed={removed} "
          f"telemetry_removed={telemetry_removed} retained={len(retained)}")


if __name__ == "__main__":
    main()
