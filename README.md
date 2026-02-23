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
- `/model use <id|num>`
- `/model add <id> <hf-repo> <hf-file> [local-path]`
- `/model download <id|num>`
- `/model validate`
- `/model remove <id|num>` (asks for confirmation)

Model presets are defined in `models.tsv`:

```text
id<TAB>name<TAB>hf_repo<TAB>hf_file<TAB>local_path
```

Active model selection is persisted across runs via `state_file` in config.

Example for adding a new Hugging Face GGUF and running it:

```text
/model add qwen25_7b_q4km Qwen/Qwen2.5-7B-Instruct-GGUF qwen2.5-7b-instruct-q4_k_m.gguf
/model download qwen25_7b_q4km
/model use qwen25_7b_q4km
/model validate
```

## REPL UX (Smooth Mode)

Core commands:

- `/help`
- `/status`
- `/clear`
- `/menu`
- `/menu run <n>`
- `/code list`
- `/code copy [n]`
- `/code shell`
- `/code shell run [n]`

Menu mode:

- Run `/menu` to enter menu mode (`menu>` prompt).
- Type menu numbers directly (`1`, `2`, `3`, ...).
- Type `q`/`quit`/`exit` to leave via menu action `0`.
- Any slash command exits menu mode and runs normally.

Quick aliases (no leading slash needed in normal prompt):

- `help`, `h`, `?` -> `/help`
- `menu`, `m` -> `/menu`
- `status`, `s` -> `/status`
- `clear`, `cls` -> `/clear`
- `models` -> `/model list`
- `use <id|num>` -> `/model use <id|num>`
- `download <id|num>` -> `/model download <id|num>`
- `remove <id|num>` -> `/model remove <id|num>`
- `q`, `quit`, `exit` -> `/exit`

## Runtime Configuration

Use `sentra.conf`:

- `runtime_preference=llama-inproc|local-binary|mock`
- `local_command_template=llama-cli -m {model_path} -n {max_tokens} --no-display-prompt -p {prompt}`
- `max_tokens=...`
- `context_window_tokens=...`

`llama-inproc` runs GGUF directly through linked `libllama` inside Sentra (no `llama-cli` subprocess).

`local-binary` requires placeholders `{model_path}`, `{prompt}`, and `{max_tokens}` and a resolvable executable on `PATH`. If unavailable, Sentra falls back deterministically to the first available runtime and prints a startup note.

## Runtime Troubleshooting Matrix

| Symptom | Likely Cause | Action |
|---|---|---|
| `runtime 'X' unavailable; using 'Y'` | Preferred runtime unavailable on this build/machine | Install required dependencies or choose an available runtime |
| `llama-inproc failed to create context` | Local backend/device initialization issue | Ensure `libllama` and `libggml` are installed and rebuild; verify model is valid and memory is sufficient |
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
