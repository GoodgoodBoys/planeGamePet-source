#!/bin/sh
set -eu

# Clears only the anonymous analytics baseline and gateway enrollment registry.
# The authoritative PC pairing database is intentionally never modified.
if [ "$(id -u)" -ne 0 ]; then
  echo "run as root" >&2
  exit 1
fi

telemetry=/var/lib/plane-pet/telemetry.db
registry=/var/lib/plane-pet/gateway_tokens.db
bindings=/var/lib/plane-pet/server_bindings.db

binding_before=missing
if [ -f "$bindings" ]; then
  binding_before=$(sha256sum "$bindings" | awk '{print $1}')
fi
events_before=$(/usr/bin/python3 - "$telemetry" <<'PY'
import sqlite3
import sys
from pathlib import Path
path = Path(sys.argv[1])
if not path.exists():
    print(0)
else:
    with sqlite3.connect(path) as database:
        try:
            print(database.execute("SELECT COUNT(*) FROM telemetry_events").fetchone()[0])
        except sqlite3.Error:
            print(0)
PY
)
tokens_before=0
if [ -f "$registry" ]; then
  tokens_before=$(wc -l < "$registry" | tr -d ' ')
fi

systemctl stop plane-pet-gateway.service
trap 'systemctl start plane-pet-gateway.service >/dev/null 2>&1 || true' EXIT
rm -f /var/lib/plane-pet/telemetry.db \
      /var/lib/plane-pet/telemetry.db-wal \
      /var/lib/plane-pet/telemetry.db-shm
install -o plane-pet -g plane-pet -m 0600 /dev/null "$registry"
systemctl start plane-pet-gateway.service
trap - EXIT

for attempt in 1 2 3 4 5; do
  if curl --silent --show-error --fail \
      http://127.0.0.1:32111/healthz >/dev/null; then
    break
  fi
  sleep 1
done
systemctl is-active --quiet plane-pet-gateway.service
curl --silent --show-error --fail \
    http://127.0.0.1:32111/healthz >/dev/null

binding_after=missing
if [ -f "$bindings" ]; then
  binding_after=$(sha256sum "$bindings" | awk '{print $1}')
fi
if [ "$binding_before" != "$binding_after" ]; then
  echo "pairing database changed unexpectedly" >&2
  exit 2
fi

events_after=$(/usr/bin/python3 - "$telemetry" <<'PY'
import sqlite3
import sys
from pathlib import Path
path = Path(sys.argv[1])
if not path.exists():
    print(0)
else:
    with sqlite3.connect(path) as database:
        print(database.execute("SELECT COUNT(*) FROM telemetry_events").fetchone()[0])
PY
)
tokens_after=$(wc -l < "$registry" | tr -d ' ')
printf 'PLANE_PET_ANALYTICS_RESET_OK events=%s->%s tokens=%s->%s bindings_unchanged=1\n' \
  "$events_before" "$events_after" "$tokens_before" "$tokens_after"
