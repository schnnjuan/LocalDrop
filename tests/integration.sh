#!/usr/bin/env bash
set -euo pipefail

repo_root="${1:-}"
binary_path="${2:-}"

if [[ -z "${binary_path}" || ! -x "${binary_path}" ]]; then
  echo "binary path is required" >&2
  exit 1
fi

extract_json_string() {
  local json="$1"
  local field="$2"
  printf '%s' "$json" | sed -n "s/.*\"${field}\":\"\\([^\"]*\\)\".*/\\1/p"
}

workdir="$(mktemp -d)"
log_file="${workdir}/server.log"
server_pid=""

cleanup() {
  if [[ -n "${server_pid}" ]] && kill -0 "${server_pid}" 2>/dev/null; then
    kill -INT "${server_pid}" 2>/dev/null || true
    wait "${server_pid}" 2>/dev/null || true
  fi
  rm -rf "${workdir}"
}

trap cleanup EXIT

cd "${workdir}"
stdbuf -oL -eL "${binary_path}" >"${log_file}" 2>&1 &
server_pid="$!"

admin_code=""
for _ in $(seq 1 50); do
  admin_code="$(sed -n 's/.*Codigo administrativo local: \([0-9]\{6\}\).*/\1/p' "${log_file}" | tail -n 1)"
  if [[ -n "${admin_code}" ]]; then
    break
  fi
  sleep 0.2
done

if [[ -z "${admin_code}" ]]; then
  echo "failed to capture admin code" >&2
  cat "${log_file}" >&2 || true
  exit 1
fi

blocked_upload="$(curl -sS -F file=@/etc/hosts http://127.0.0.1:8080/upload)"
[[ "${blocked_upload}" == *"Upload bloqueado"* ]]

unlock_json="$(curl -sS -F admin_code="${admin_code}" http://127.0.0.1:8080/api/session/unlock)"
ui_token="$(extract_json_string "${unlock_json}" token)"
[[ -n "${ui_token}" ]]

pair_code_json="$(curl -sS -H "X-LocalDrop-UI-Token: ${ui_token}" http://127.0.0.1:8080/api/pair/local-code)"
pair_code="$(extract_json_string "${pair_code_json}" code)"
[[ -n "${pair_code}" ]]

pair_exchange_json="$(curl -sS -F pair_code="${pair_code}" -F peer_name="self-peer" -F peer_port="8080" http://127.0.0.1:8080/api/pair/exchange)"
pair_exchange_token="$(extract_json_string "${pair_exchange_json}" token)"
[[ -n "${pair_exchange_token}" ]]

pair_json="$(curl -sS -H "X-LocalDrop-UI-Token: ${ui_token}" \
  -F target_ip="127.0.0.1" \
  -F target_port="8080" \
  -F target_name="self-peer" \
  -F pair_code="${pair_code}" \
  http://127.0.0.1:8080/api/pair)"
[[ "${pair_json}" == *"\"status\":\"ok\""* ]]

printf 'integration-send' > send.txt
send_json="$(curl -sS -H "X-LocalDrop-UI-Token: ${ui_token}" \
  -F file=@send.txt \
  -F target_ip="127.0.0.1" \
  -F target_port="8080" \
  http://127.0.0.1:8080/api/send)"
job_id="$(extract_json_string "${send_json}" job_id)"
[[ -n "${job_id}" ]]

job_status=""
for _ in $(seq 1 50); do
  job_json="$(curl -sS -H "X-LocalDrop-UI-Token: ${ui_token}" http://127.0.0.1:8080/api/transfers/${job_id})"
  job_status="$(extract_json_string "${job_json}" status)"
  if [[ "${job_status}" == "completed" ]]; then
    break
  fi
  if [[ "${job_status}" == "failed" ]]; then
    echo "transfer job failed: ${job_json}" >&2
    exit 1
  fi
  sleep 0.2
done

if [[ "${job_status}" != "completed" ]]; then
  echo "transfer job did not complete" >&2
  cat "${log_file}" >&2 || true
  exit 1
fi

find downloads -type f -name '*send.txt' | grep -q .
if find staging -type f -print -quit | grep -q .; then
  echo "staging directory still has files" >&2
  find staging -type f >&2 || true
  exit 1
fi

echo "localdrop_integration: ok"
