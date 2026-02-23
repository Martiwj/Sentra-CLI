#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <model_id> [models_tsv_path]"
  exit 1
fi

MODEL_ID="$1"
MODELS_FILE="${2:-models.tsv}"

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

if command -v huggingface-cli >/dev/null 2>&1; then
  huggingface-cli download "$HF_REPO" "$HF_FILE" --local-dir "$(dirname "$LOCAL_PATH")"
else
  URL="https://huggingface.co/${HF_REPO}/resolve/main/${HF_FILE}?download=true"
  curl -L "$URL" -o "$LOCAL_PATH"
fi

echo "downloaded: $LOCAL_PATH"
