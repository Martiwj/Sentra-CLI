# Sentra CLI (MVP)

Sentra CLI is a local-first terminal assistant scaffold designed for offline usage and C++ implementation.

This MVP proves:
- Interactive REPL chat loop
- Session persistence to local disk
- Pluggable runtime architecture
- Mock runtime for deterministic offline testing
- Optional local binary adapter for `llama.cpp`-style CLI invocation

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

## Runtime Modes

Configured in `sentra.conf`:

- `runtime_preference=mock`
  - Always available; useful for architecture and UX validation.
- `runtime_preference=local-binary`
  - Requires `local_command_template` with a `{prompt}` placeholder.
  - Example with llama.cpp:

```text
local_command_template=llama-cli -m ./models/model.gguf -n 256 --no-display-prompt -p {prompt}
```

If preferred runtime is unavailable, Sentra falls back to the first available runtime.

## Session Storage

Sessions are stored in `.sentra/sessions/<session-id>.log`.
Each line is `role<TAB>content` with escaping for tabs/newlines.

## Commands

- `/help`
- `/session`
- `/exit` or `/quit`

## Project Layout

- `include/sentra/` public interfaces and core types
- `src/core/` orchestration and session store
- `src/runtime/` runtime adapters
- `src/cli/` REPL interaction loop
- `docs/` architecture and roadmap notes

## Notes

- This is an MVP scaffold, not production-safe execution orchestration.
- Model outputs are treated as plain text; no automatic command execution exists.
- Next steps are listed in `docs/ARCHITECTURE.md`.
