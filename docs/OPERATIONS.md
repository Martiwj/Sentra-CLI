# Sentra CLI Operations

## Offline Setup

1. Build:
```bash
cmake -S . -B build && cmake --build build
```
2. Configure `sentra.conf` for local runtime:
```text
runtime_preference=local-binary
local_command_template=llama-cli -m {model_path} -n {max_tokens} --no-display-prompt -p {prompt}
```
3. Download a preset:
```bash
./scripts/download_model.sh llama31_8b_q4km
```
4. Validate in CLI:
```text
/model validate
```

## Download and Auth Recovery

- If download returns 401/403:
1. `huggingface-cli login`
2. Accept model license/access on Hugging Face
3. Retry download

- `hf_transfer` is optional acceleration. If unavailable, the script falls back automatically.
- The script uses resumable/retry-friendly options (`--resume-download`, `curl -C - --retry`).

## Runtime Recovery

- Missing executable:
  - Ensure runtime binary (`llama-cli`) is installed and on `PATH`.
- Placeholder/template issues:
  - Ensure all placeholders exist: `{model_path}`, `{prompt}`, `{max_tokens}`.
- Non-zero runtime exit:
  - Sentra surfaces stderr/output; run the command template manually to isolate environment/model issues.

## Model File Recovery

- Validate active model:
```text
/model validate
```
- Re-download active model:
```text
/model download <id>
```
- Remove corrupted local model:
```text
/model remove <id>
```

## Session Recovery

- List sessions:
```text
/session list
```
- Inspect active session metadata:
```text
/session info
```
- Session logs are append-only `.log` files; metadata is in sidecar `.meta`.
- Active model persistence across runs is stored in `state_file` (`.sentra/state.conf` by default).
