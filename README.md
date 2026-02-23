# Sentra CLI

Sentra CLI is a local-first terminal assistant with explicit local model operations, runtime fallbacks, and persistent sessions.

## Build and Run

```bash
cmake -S . -B build && cmake --build build
./build/sentra
```

Optional:

```bash
./build/sentra --config sentra.conf --session session-123
```

## Model Lifecycle Commands

- `/model list`
- `/model current`
- `/model use <model-id>`
- `/model download <model-id>`
- `/model validate`
- `/model remove <model-id>` (asks for confirmation)

Model presets are defined in `models.tsv`:

```text
id<TAB>name<TAB>hf_repo<TAB>hf_file<TAB>local_path
```

Active model selection is persisted across runs via `state_file` in config.

## Runtime Configuration

Use `sentra.conf`:

- `runtime_preference=mock|local-binary`
- `local_command_template=llama-cli -m {model_path} -n {max_tokens} --no-display-prompt -p {prompt}`
- `max_tokens=...`
- `context_window_tokens=...`

`local-binary` requires placeholders `{model_path}`, `{prompt}`, and `{max_tokens}` and a resolvable executable on `PATH`. If unavailable, Sentra falls back deterministically to the first available runtime and prints a startup note.

## Runtime Troubleshooting Matrix

| Symptom | Likely Cause | Action |
|---|---|---|
| `runtime 'local-binary' unavailable; using 'mock'` | Missing `local_command_template` placeholder or missing executable | Fix template and ensure `llama-cli` (or equivalent) is installed and on `PATH` |
| `active model path is missing` | Model file not downloaded or removed | Run `/model download <id>` then `/model validate` |
| `local-binary runtime failed with exit code ...` | Runtime process error | Inspect printed stderr, verify model path, and run command template manually |
| Download 401/403 | Hugging Face auth/license not satisfied | Run `huggingface-cli login`, accept model license, retry |
| `hf_transfer not installed` message | Optional acceleration package missing | Continue with fallback download path or install `hf_transfer` |

## Hardware and Model Size Guidance

- 3B-4B quantized models: lower memory footprint, fastest startup, good for laptops.
- 7B-8B quantized models: stronger quality, moderate memory/latency tradeoff.
- Higher parameter models: require substantially more RAM/VRAM; prefer desktop-class hardware.
- If latency grows in long chats, reduce `max_tokens` and/or `context_window_tokens`.

## Session Commands and Storage

- `/session`
- `/session info`
- `/session list`

Session logs are append-only in `.sentra/sessions/<session-id>.log` using a structured `v1` line format. Metadata is stored in `.sentra/sessions/<session-id>.meta` with created time, active model id, and runtime name.

## Tests and Smoke

```bash
./build/sentra_tests
./tests/smoke_repl.sh
```

## Project Layout

- `include/sentra/`: public interfaces and types
- `src/core/`: orchestration, registry, state, sessions, context windowing
- `src/runtime/`: runtime adapters
- `src/cli/`: REPL loop and command handling
- `scripts/`: operational helpers (downloads)
- `docs/`: architecture and operations notes
