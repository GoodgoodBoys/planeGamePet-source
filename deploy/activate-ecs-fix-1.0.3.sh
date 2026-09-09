#!/bin/bash
# One scoped code-only activation. No config/unit/Nginx/FingerKnight edits.
set -Eeuo pipefail
umask 077
stage=/opt/plane-pet/staging/hardening-1.0.3-20260906
source="$stage/source-complete"
candidate="$source/linux-check.2EooHkHF/plane-pet-server"
gateway="$source/gateway/plane_pet_gateway.py"
test "$(id -u)" = 0
test "$(realpath "$stage")" = "$stage"
test ! -e "$stage/ACTIVATION_OK"
cd -- "$stage"
exec 9>"$stage/activation.lock"
flock -n 9
check_code() {
  printf '%s  %s\n' 59177e624f65532fb5d005c2dc3d6e17de7ae884e183c8c7bff016a2f262aa0e /opt/plane-pet/bin/plane-pet-server | sha256sum -c -
  printf '%s  %s\n' a42c731a8ca2a8e3f9b1913b3dd03553b7d75ac782b783cc4a4785d0a088bad8 /opt/plane-pet/gateway/plane_pet_gateway.py | sha256sum -c -
}
check_code
printf '%s  %s\n' abd3d3fe4526996882b6a68bb53e93991cc67ac494783555f0aeefa2eace6e19 "$candidate" | sha256sum -c -
printf '%s  %s\n' 07e81700fadb10d0014ca81229fa9ac3981a02453ad2058fb8bb3702d8b340c4 "$gateway" | sha256sum -c -
systemctl is-active --quiet plane-pet.service plane-pet-gateway.service
test -z "$(ss -Htn state established '( sport = :32111 )')"
backup=$(mktemp -d /opt/plane-pet/backups/hardening-1.0.3-20260906.XXXXXXXX)
printf '%s\n' "$backup" > "$stage/activation-backup.path"
cp -a /opt/plane-pet/bin/plane-pet-server "$backup/plane-pet-server"
cp -a /opt/plane-pet/gateway/plane_pet_gateway.py "$backup/plane_pet_gateway.py"
sha256sum /etc/systemd/system/plane-pet.service /etc/systemd/system/plane-pet-gateway.service > "$backup/protected.sha256"
find /etc/nginx -type f -exec sha256sum {} + >> "$backup/protected.sha256"
find -L /opt/fingerknight-server/current -maxdepth 1 -type f -exec sha256sum {} + >> "$backup/protected.sha256"
readlink -f /opt/fingerknight-server/current > "$backup/fingerknight-current.txt"
systemctl show fingerknight-server.service plane-link.service nginx.service -p Id -p ActiveState -p MainPID > "$backup/protected-services.before"
atomic_install() {
  local from=$1 to=$2 temporary
  case "$to" in /opt/plane-pet/bin/plane-pet-server|/opt/plane-pet/gateway/plane_pet_gateway.py) ;; *) return 2 ;; esac
  temporary=$(mktemp "${to}.hardening-1.0.3.XXXXXXXX")
  install -o root -g root -m 755 -- "$from" "$temporary"
  mv -Tf -- "$temporary" "$to"
}
changed=0
rollback() {
  local rc=$?
  trap - ERR INT TERM
  if [ "$changed" = 1 ]; then
    echo 'ROLLBACK: only PlanePet code; live data and all other projects are untouched.' >&2
    set +e
    systemctl stop plane-pet-gateway.service plane-pet.service
    atomic_install "$backup/plane-pet-server" /opt/plane-pet/bin/plane-pet-server
    atomic_install "$backup/plane_pet_gateway.py" /opt/plane-pet/gateway/plane_pet_gateway.py
    systemctl start plane-pet.service plane-pet-gateway.service
    check_code
  fi
  if [ "$rc" = 0 ]; then rc=1; fi
  exit "$rc"
}
trap rollback ERR INT TERM
check_code
sha256sum -c "$backup/protected.sha256"
test -z "$(ss -Htn state established '( sport = :32111 )')"
changed=1
systemctl stop plane-pet-gateway.service
systemctl stop plane-pet.service
export PYTHONDONTWRITEBYTECODE=1
# Quiescent three-file backup, using SQLite's online-backup API for WAL safety.
python3 "$source/deploy/backup-plane-pet.py" --source /var/lib/plane-pet --destination "$backup/data" > "$backup/data-backup.txt"
atomic_install "$candidate" /opt/plane-pet/bin/plane-pet-server
atomic_install "$gateway" /opt/plane-pet/gateway/plane_pet_gateway.py
systemctl start plane-pet.service
systemctl start plane-pet-gateway.service
python3 - "$backup" <<'PY'
import base64, json, pathlib, sqlite3, sys, time, urllib.error, urllib.request
backup = pathlib.Path(sys.argv[1])
for attempt in range(40):
    try:
        assert urllib.request.urlopen('http://127.0.0.1:32111/readyz', timeout=2).read() == b'ready\n'
        break
    except (OSError, AssertionError):
        if attempt == 39: raise
        time.sleep(.25)
base = 'https://8.166.124.212:32112'
assert urllib.request.urlopen(base + '/healthz', timeout=10).read() == b'ok\n'
try:
    urllib.request.urlopen(base + '/admin', timeout=10)
    raise AssertionError('Admin unexpectedly allowed anonymous access')
except urllib.error.HTTPError as error:
    assert error.code == 401
password = pathlib.Path('/etc/plane-pet/gateway-admin.password').read_text().strip()
authorization = base64.b64encode(('plane-pet:' + password).encode()).decode()
headers = {'Authorization': 'Basic ' + authorization}
request = urllib.request.Request('http://127.0.0.1:32111/admin/status', headers=headers)
status = json.load(urllib.request.urlopen(request, timeout=5))
assert status['backend_ready'] and status['telemetry_write_failures'] == 0
page = urllib.request.urlopen(urllib.request.Request(base + '/admin', headers=headers), timeout=10).read().decode()
assert '<html' in page and len(page) > 1000
data = next((backup / 'data').iterdir())
live = pathlib.Path('/var/lib/plane-pet')
for name in ('server_bindings.db', 'gateway_tokens.db'):
    assert (data / name).read_bytes() == (live / name).read_bytes(), name + ' changed'
with sqlite3.connect((data / 'telemetry.db').as_uri() + '?mode=ro', uri=True) as db:
    before = db.execute('SELECT * FROM telemetry_events ORDER BY id').fetchall()
    schema = db.execute('PRAGMA table_info(telemetry_events)').fetchall()
with sqlite3.connect((live / 'telemetry.db').as_uri() + '?mode=ro', uri=True) as db:
    assert db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
    assert db.execute('PRAGMA table_info(telemetry_events)').fetchall() == schema
    after = {row[0]: row for row in db.execute('SELECT * FROM telemetry_events')}
    retained = [row for row in before if row[4] >= int(time.time()*1000) - 90*86400000]
    assert all(after.get(row[0]) == row for row in retained)
print('LIVE_DATA_PRESERVED historical_rows=%d binding_and_credentials_identical=1 schema_identical=1' % len(retained))
print('LIVE_HEALTH_OK ready=200 public_tls=200 admin_unauthorized=401 admin_authenticated=200')
print('LIVE_CAPACITY connections=%d enrollments=%d remaining=%d' %
      (status['connections'], status['enrollments'], status['enrollment_remaining']))
PY
systemctl is-active --quiet plane-pet.service plane-pet-gateway.service
sha256sum -c "$backup/protected.sha256"
test "$(readlink -f /opt/fingerknight-server/current)" = "$(cat "$backup/fingerknight-current.txt")"
systemctl show fingerknight-server.service plane-link.service nginx.service -p Id -p ActiveState -p MainPID > "$backup/protected-services.after"
cmp "$backup/protected-services.before" "$backup/protected-services.after"
sha256sum /opt/plane-pet/bin/plane-pet-server /opt/plane-pet/gateway/plane_pet_gateway.py > "$stage/activated-code.sha256"
date -u +%FT%TZ > "$stage/ACTIVATION_OK"
changed=0
trap - ERR INT TERM
printf 'PLANE_PET_1_0_3_ACTIVATION_OK code_only=1 protected_services_configs_unchanged=1 backup=%s\n' "$backup"
