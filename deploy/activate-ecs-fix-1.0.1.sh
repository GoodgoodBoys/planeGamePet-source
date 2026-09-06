#!/bin/bash
# One explicitly scoped release; refuse changed baselines rather than overwrite them.
set -Eeuo pipefail
umask 077
stage=/opt/plane-pet/staging/fix-1.0.1-20260905-1335
baseline=/opt/plane-pet/backups/fix-1.0.1-20260905-1335
candidate="$stage/source/linux-check.VWdQlJZP/plane-pet-server"
gateway="$stage/source/gateway/plane_pet_gateway.py"
cd -- "$stage"
exec 9>"$stage/activation.lock"
flock -n 9
test "$(id -u)" = 0
test "$(realpath "$stage")" = "$stage"
test "$(realpath "$baseline")" = "$baseline"
test ! -e "$stage/ACTIVATION_OK"
sha256sum -c "$baseline/production-code.sha256"
sha256sum -c "$baseline/protected.sha256"
test "$(readlink -f /opt/fingerknight-server/current)" = "$(cat "$baseline/fingerknight-current.txt")"
printf '%s  %s\n' 59177e624f65532fb5d005c2dc3d6e17de7ae884e183c8c7bff016a2f262aa0e "$candidate" | sha256sum -c -
printf '%s  %s\n' a42c731a8ca2a8e3f9b1913b3dd03553b7d75ac782b783cc4a4785d0a088bad8 "$gateway" | sha256sum -c -
grep -Fx "PLANE_PET_LINUX_STAGING_VERIFIED=$stage/source/linux-check.VWdQlJZP" "$stage/linux-verification.log"
nginx -t
systemctl is-active --quiet plane-pet.service plane-pet-gateway.service
systemctl show fingerknight-server.service plane-link.service -p Id -p ActiveState -p MainPID > "$stage/protected-services.before"
systemctl show nginx.service -p MainPID > "$stage/nginx-master.before"

# Build only the four audited Nginx substitutions and one gateway setting.
cp -a /etc/nginx/sites-enabled/plane-pet.conf "$stage/plane-pet.conf.candidate"
cp -a /etc/systemd/system/plane-pet-gateway.service "$stage/plane-pet-gateway.service.candidate"
python3 - <<'PY'
from pathlib import Path
root = Path('/opt/plane-pet/staging/fix-1.0.1-20260905-1335')
patches = {
 'plane-pet.conf.candidate': [
  ('limit_req_zone $binary_remote_addr zone=plane_pet_handshake:10m rate=12r/m;',
   'limit_req_zone $binary_remote_addr zone=plane_pet_handshake:10m rate=2r/s;', 1),
  ('limit_req zone=plane_pet_handshake burst=6 nodelay;',
   'limit_req zone=plane_pet_handshake burst=64 nodelay;', 4),
  ('limit_conn plane_pet_connections 4;', 'limit_conn plane_pet_connections 64;', 2),
  ('proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;',
   'proxy_set_header X-Forwarded-For $remote_addr;', 6)],
 'plane-pet-gateway.service.candidate': [
  ('Environment=PLANE_PET_MAX_ENROLLMENTS_PER_IP=8\n',
   'Environment=PLANE_PET_MAX_ENROLLMENTS_PER_IP=64\n', 1)]}
for name, replacements in patches.items():
    path = root / name
    value = path.read_text()
    for old, new, count in replacements:
        if value.count(old) != count:
            raise RuntimeError(f'Unexpected configuration baseline: {name}')
        value = value.replace(old, new)
    path.write_text(value)
print('EXACT_CONFIG_PATCHES_VERIFIED nginx=13 gateway_unit=1')
PY

compat=$(mktemp -d "$stage/live-copy.XXXXXXXX")
cp -a /var/lib/plane-pet/gateway_tokens.db "$compat/gateway_tokens.db"
python3 - "$compat/telemetry.db" <<'PY'
import sqlite3, sys
with sqlite3.connect('file:/var/lib/plane-pet/telemetry.db?mode=ro', uri=True) as source:
    with sqlite3.connect(sys.argv[1]) as target:
        source.backup(target)
        assert target.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
PY
export PYTHONPYCACHEPREFIX="$stage/deploy-pycache"
python3 "$stage/verify-live-data-copy.py" "$compat" "$gateway" "$baseline/plane_pet_gateway.py"

backup=$(mktemp -d /opt/plane-pet/backups/activation-1.0.1-20260905.XXXXXXXX)
for name in plane-pet-server plane_pet_gateway.py plane-pet.service plane-pet-gateway.service plane-pet.conf; do
  cp -a "$baseline/$name" "$backup/$name"
done
printf 'ACTIVATION_BACKUP=%s\n' "$backup"
printf '%s\n' "$backup" > "$stage/activation-backup.path"
changed=0
atomic_install() {
  local source=$1 target=$2 mode=$3 temporary
  temporary=$(mktemp "${target}.plane-pet-fix.XXXXXXXX")
  install -o root -g root -m "$mode" -- "$source" "$temporary"
  mv -Tf -- "$temporary" "$target"
}
rollback() {
  local rc=$?
  trap - ERR INT TERM
  if [ "$changed" = 1 ]; then
    echo 'Activation failed: restoring ONLY Plane Pet code/configuration. Data is not rolled back.' >&2
    set +e
    atomic_install "$backup/plane-pet-server" /opt/plane-pet/bin/plane-pet-server 755
    atomic_install "$backup/plane_pet_gateway.py" /opt/plane-pet/gateway/plane_pet_gateway.py 755
    atomic_install "$backup/plane-pet-gateway.service" /etc/systemd/system/plane-pet-gateway.service 644
    atomic_install "$backup/plane-pet.conf" /etc/nginx/sites-enabled/plane-pet.conf 644
    systemctl daemon-reload
    systemctl restart plane-pet.service plane-pet-gateway.service
    nginx -t && systemctl reload nginx.service
    sha256sum -c "$baseline/production-code.sha256"
  fi
  if [ "$rc" = 0 ]; then rc=1; fi
  exit "$rc"
}
trap rollback ERR INT TERM
# Recheck immediately before the only maintenance window.
sha256sum -c "$baseline/production-code.sha256"
sha256sum -c "$baseline/protected.sha256"
changed=1
systemctl stop plane-pet-gateway.service
systemctl stop plane-pet.service
cp -a /var/lib/plane-pet/server_bindings.db "$backup/server_bindings.db"
cp -a /var/lib/plane-pet/gateway_tokens.db "$backup/gateway_tokens.db"
python3 - "$backup/telemetry.db" <<'PY'
import sqlite3, sys
with sqlite3.connect('file:/var/lib/plane-pet/telemetry.db?mode=ro', uri=True) as source:
    with sqlite3.connect(sys.argv[1]) as target:
        source.backup(target)
        assert target.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
        print('FRESH_TELEMETRY_BACKUP_OK rows=' + str(target.execute('SELECT COUNT(*) FROM telemetry_events').fetchone()[0]))
PY
atomic_install "$candidate" /opt/plane-pet/bin/plane-pet-server 755
atomic_install "$gateway" /opt/plane-pet/gateway/plane_pet_gateway.py 755
atomic_install "$stage/plane-pet-gateway.service.candidate" /etc/systemd/system/plane-pet-gateway.service 644
atomic_install "$stage/plane-pet.conf.candidate" /etc/nginx/sites-enabled/plane-pet.conf 644
nginx -t
systemd-analyze verify /etc/systemd/system/plane-pet.service /etc/systemd/system/plane-pet-gateway.service
systemctl daemon-reload
systemctl start plane-pet.service
systemctl start plane-pet-gateway.service
systemctl reload nginx.service
python3 - "$backup" <<'PY'
import base64, pathlib, sqlite3, sys, time, urllib.error, urllib.request
for attempt in range(20):
    try:
        assert urllib.request.urlopen('http://127.0.0.1:32111/healthz', timeout=2).read() == b'ok\n'
        break
    except (OSError, AssertionError):
        if attempt == 19: raise
        time.sleep(.25)
base = 'https://8.166.124.212:32112'
assert urllib.request.urlopen(base + '/healthz', timeout=10).read() == b'ok\n'
try:
    urllib.request.urlopen(base + '/admin', timeout=10)
    raise AssertionError('Unauthenticated admin was accessible')
except urllib.error.HTTPError as error:
    assert error.code == 401
password = pathlib.Path('/etc/plane-pet/gateway-admin.password').read_text().strip()
credential = base64.b64encode(('plane-pet:' + password).encode()).decode()
request = urllib.request.Request(base + '/admin', headers={'Authorization': 'Basic ' + credential})
response = urllib.request.urlopen(request, timeout=10)
page = response.read().decode()
assert response.status == 200 and '<html' in page and len(page) > 1000
backup = pathlib.Path(sys.argv[1])
for name in ('server_bindings.db', 'gateway_tokens.db'):
    assert (backup / name).read_bytes() == (pathlib.Path('/var/lib/plane-pet') / name).read_bytes(), name + ' changed'
with sqlite3.connect('file:' + str(backup / 'telemetry.db') + '?mode=ro', uri=True) as old:
    before = old.execute('SELECT * FROM telemetry_events ORDER BY id').fetchall()
with sqlite3.connect('file:/var/lib/plane-pet/telemetry.db?mode=ro', uri=True) as live:
    assert live.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
    current = {row[0]: row for row in live.execute('SELECT * FROM telemetry_events')}
    cutoff = int(time.time() * 1000) - 90 * 86400000
    kept = [row for row in before if row[4] >= cutoff]
    assert all(current.get(row[0]) == row for row in kept), 'Historical events changed'
    print(f'LIVE_DATA_PRESERVED retained_history={len(kept)} current_rows={len(current)} binding_and_credentials_identical=1')
print('LIVE_HEALTH_OK loopback=200 public_tls=200 admin_no_auth=401 admin_authenticated=200')
PY
systemctl is-active --quiet plane-pet.service plane-pet-gateway.service
sha256sum -c "$baseline/protected.sha256"
cmp "$baseline/plane-pet.service" /etc/systemd/system/plane-pet.service
test "$(readlink -f /opt/fingerknight-server/current)" = "$(cat "$baseline/fingerknight-current.txt")"
systemctl show fingerknight-server.service plane-link.service -p Id -p ActiveState -p MainPID > "$stage/protected-services.after"
cmp "$stage/protected-services.before" "$stage/protected-services.after"
systemctl show nginx.service -p MainPID > "$stage/nginx-master.after"
cmp "$stage/nginx-master.before" "$stage/nginx-master.after"
sha256sum /opt/plane-pet/bin/plane-pet-server /opt/plane-pet/gateway/plane_pet_gateway.py /etc/systemd/system/plane-pet-gateway.service /etc/nginx/sites-enabled/plane-pet.conf > "$stage/activated-code.sha256"
date -u +%FT%TZ > "$stage/ACTIVATION_OK"
changed=0
trap - ERR INT TERM
echo 'PLANE_PET_ACTIVATION_OK protected_config_services_unchanged=1 nginx_graceful_reload=1'
