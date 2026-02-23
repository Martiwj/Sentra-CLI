# Sentra CLI (MVP)

Sentra CLI is a local-first terminal assistant scaffold designed for offline usage and C++ implementation.

This MVP proves:
- Interactive REPL chat loop
- Session persistence to local disk
- Pluggable runtime architecture
- Model registry with Hugging Face-backed presets
- Runtime model switching (`/model use <id>`)

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/sentra
```

Optional:

```bash
./build/sentra --config sentra.conf --session session-123
```

## Configure Runtime

Edit `/Users/mwj/Documents/Sentra-CLI/sentra.conf`.

- `runtime_preference=mock`
  - Always available; useful for architecture and UX validation.
- `runtime_preference=local-binary`
  - Uses an external local runtime command (for example `llama-cli`).
  - Requires `local_command_template` with placeholders.

Example:

```text
runtime_preference=local-binary
local_command_template=llama-cli -m {model_path} -n {max_tokens} --no-display-prompt -p {prompt}
```

## Hugging Face Model Presets

Model presets live in `/Users/mwj/Documents/Sentra-CLI/models.tsv`.
Each row has:

```text
id<TAB>name<TAB>hf_repo<TAB>hf_file<TAB>local_path
```

Included preset IDs:
- `llama31_8b_q4km`
- `mistral7b_v03_q4km`
- `phi3_mini_q4km`

Download one preset model:

```bash
./scripts/download_model.sh llama31_8b_q4km
```

Then set runtime to local-binary and run Sentra.

## REPL Commands

- `/help`
- `/session`
- `/model list`
- `/model current`
- `/model use <model-id>`
- `/exit` or `/quit`

## Session Storage

Sessions are stored in `.sentra/sessions/<session-id>.log`.
Each line is `role<TAB>content` with escaping for tabs/newlines.

## Project Layout

- `/Users/mwj/Documents/Sentra-CLI/include/sentra/` public interfaces and core types
- `/Users/mwj/Documents/Sentra-CLI/src/core/` orchestration, model registry, and session store
- `/Users/mwj/Documents/Sentra-CLI/src/runtime/` runtime adapters
- `/Users/mwj/Documents/Sentra-CLI/src/cli/` REPL interaction loop
- `/Users/mwj/Documents/Sentra-CLI/scripts/` helper scripts
- `/Users/mwj/Documents/Sentra-CLI/docs/` architecture and roadmap notes

## Notes

- This is an MVP scaffold, not production-safe execution orchestration.
- Model outputs are treated as plain text; no automatic command execution exists.
- Next steps are listed in `/Users/mwj/Documents/Sentra-CLI/docs/ARCHITECTURE.md`.
