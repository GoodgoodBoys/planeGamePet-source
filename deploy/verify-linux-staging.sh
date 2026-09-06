#!/bin/bash
# Compile and test only. Never installs files or touches production services.
set -euo pipefail
task_source=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
case "$task_source" in
  /opt/plane-pet/staging/*) ;;
  *) echo 'Refusing to run outside /opt/plane-pet/staging/<release>.' >&2; exit 2 ;;
esac
cd -- "$task_source"
test -f server/main.cpp
test -f gateway/plane_pet_gateway.py
task_check=$(mktemp -d "$task_source/linux-check.XXXXXXXX")
mkdir -- "$task_check/tmp" "$task_check/state"
export TMPDIR="$task_check/tmp"
export PYTHONPYCACHEPREFIX="$task_check/pycache"
export PLANE_PET_TOKEN_STORE="$task_check/state/gateway_tokens.db"
export PLANE_PET_TELEMETRY_STORE="$task_check/state/telemetry.db"
export PLANE_PET_ADMIN_PASSWORD_FILE="$task_check/state/no-admin-password"
printf 'ISOLATED_CHECK_DIRECTORY=%s\n' "$task_check"
g++ --version | head -1
python3 --version
nice -n 19 g++ -std=c++17 -O2 -Wall -Wextra -pthread \
  server/main.cpp -o "$task_check/plane-pet-server"
nice -n 19 g++ -std=c++17 -O2 -Wall -Wextra -pthread \
  tests/server_state_test.cpp -o "$task_check/server-state-test"
"$task_check/server-state-test" "$task_check/state"
python3 -m py_compile gateway/plane_pet_gateway.py tests/test_gateway_regressions.py tests/test_gateway_telemetry.py tests/server_edge_test.py
python3 -m unittest discover -s tests -p 'test_gateway*.py' -v
python3 tests/server_edge_test.py --server "$task_check/plane-pet-server"
sha256sum "$task_check/plane-pet-server" gateway/plane_pet_gateway.py
printf 'PLANE_PET_LINUX_STAGING_VERIFIED=%s\n' "$task_check"
