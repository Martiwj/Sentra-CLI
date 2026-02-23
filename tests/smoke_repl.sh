#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_PATH="${ROOT_DIR}/build/sentra"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

mkdir -p "${TMP_DIR}/models" "${TMP_DIR}/sessions"
MODEL_FILE="${TMP_DIR}/models/test.gguf"
printf 'fake model file\n' > "${MODEL_FILE}"

cat > "${TMP_DIR}/models.tsv" <<EOF
test_model	Test Model	repo/test	test.gguf	${MODEL_FILE}
alt_model	Alt Model	repo/alt	alt.gguf	${TMP_DIR}/models/alt.gguf
EOF

cat > "${TMP_DIR}/sentra.conf" <<EOF
runtime_preference=mock
sessions_dir=${TMP_DIR}/sessions
state_file=${TMP_DIR}/state.conf
models_file=${TMP_DIR}/models.tsv
default_model_id=test_model
system_prompt=You are Sentra smoke test prompt.
max_tokens=64
context_window_tokens=128
local_command_template=
EOF

OUTPUT_FILE="${TMP_DIR}/smoke.out"
{
  printf '/model current\n'
  printf '/model validate\n'
  printf '/model use alt_model\n'
  printf '/model current\n'
  printf '/session info\n'
  printf '/session list\n'
  printf '/exit\n'
} | "${BIN_PATH}" --config "${TMP_DIR}/sentra.conf" > "${OUTPUT_FILE}" 2>&1

grep -q "model valid: test_model" "${OUTPUT_FILE}"
grep -q "active model: alt_model" "${OUTPUT_FILE}"
grep -q "session_id:" "${OUTPUT_FILE}"
grep -q "runtime_name: mock" "${OUTPUT_FILE}"

echo "smoke_repl: passed"
