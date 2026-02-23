#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <model_id> [models_tsv_path]"
  exit 1
fi

MODEL_ID="$1"
MODELS_FILE="${2:-models.tsv}"
ENABLE_HF_TRANSFER="${SENTRA_ENABLE_HF_TRANSFER:-1}"

print_auth_help() {
  local repo="$1"
  cat <<MSG
download failed for ${repo}

Likely cause:
- the repo or file is gated and requires authentication/license acceptance.

Next steps:
1) log in: huggingface-cli login
2) ensure your account has access to: https://huggingface.co/${repo}
3) optionally export HF_TOKEN=<token> before running this script
4) to keep moving quickly, try an alternate preset such as mistral7b_v03_q4km
MSG
}

if [[ ! -f "$MODELS_FILE" ]]; then
  echo "models file not found: $MODELS_FILE"
  exit 1
fi

LINE=$(awk -F '\t' -v id="$MODEL_ID" '$1 == id {print $0}' "$MODELS_FILE" || true)
if [[ -z "$LINE" ]]; then
  echo "model id not found: $MODEL_ID"
  exit 1
fi

HF_REPO=$(echo "$LINE" | awk -F '\t' '{print $3}')
HF_FILE=$(echo "$LINE" | awk -F '\t' '{print $4}')
LOCAL_PATH=$(echo "$LINE" | awk -F '\t' '{print $5}')

mkdir -p "$(dirname "$LOCAL_PATH")"

if [[ -f "$LOCAL_PATH" ]]; then
  echo "already present: $LOCAL_PATH"
  exit 0
fi

if command -v huggingface-cli >/dev/null 2>&1; then
  if [[ "$ENABLE_HF_TRANSFER" == "1" ]]; then
    if python3 -c "import hf_transfer" >/dev/null 2>&1; then
      export HF_HUB_ENABLE_HF_TRANSFER=1
    else
      echo "hf_transfer not installed; falling back to standard download path."
      export HF_HUB_ENABLE_HF_TRANSFER=0
    fi
  fi

  set +e
  OUT=$(huggingface-cli download "$HF_REPO" "$HF_FILE" --local-dir "$(dirname "$LOCAL_PATH")" --resume-download 2>&1)
  CODE=$?
  set -e
  if [[ $CODE -ne 0 ]]; then
    echo "$OUT"
    if echo "$OUT" | grep -Eq "401|403|Unauthorized|Forbidden"; then
      print_auth_help "$HF_REPO"
    fi
    exit $CODE
  fi
  echo "$OUT"
else
  URL="https://huggingface.co/${HF_REPO}/resolve/main/${HF_FILE}?download=true"
  CURL_HEADERS=()
  if [[ -n "${HF_TOKEN:-}" ]]; then
    CURL_HEADERS=(-H "Authorization: Bearer ${HF_TOKEN}")
  fi

  set +e
  OUT=$(curl -fL --retry 3 --retry-delay 2 -C - "${CURL_HEADERS[@]}" "$URL" -o "$LOCAL_PATH" 2>&1)
  CODE=$?
  set -e
  if [[ $CODE -ne 0 ]]; then
    echo "$OUT"
    if echo "$OUT" | grep -Eq "401|403|Unauthorized|Forbidden"; then
      print_auth_help "$HF_REPO"
    fi
    exit $CODE
  fi
fi

echo "downloaded: $LOCAL_PATH"
