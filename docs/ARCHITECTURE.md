# Sentra CLI Architecture (MVP)

## Goals

- Fully local-first and offline operation once dependencies/models are available locally.
- C++ codebase with clear module boundaries for future runtime expansion.
- CLI-first UX with streaming output and persistent sessions.

## Components

1. `cli/repl`
- Owns user interaction loop and slash commands.
- Streams model output to terminal as it arrives.

2. `core/orchestrator`
- Selects active runtime by preference and availability.
- Converts message history into generation requests.
- Owns active model selection and runtime request metadata (`model_id`, `model_path`).

3. `core/model_registry`
- Loads local model catalog from `models.tsv`.
- Tracks active model and supports switching by model id.

4. `core/session_store`
- Persists and restores per-session message history.
- Uses append-only local logs for simplicity.

5. `runtime/*`
- `mock_runtime`: deterministic baseline for tests/dev.
- `local_binary_runtime`: adapter for local model CLIs with `{model_path}`, `{prompt}`, and `{max_tokens}` placeholders.

6. `config`
- Key-value config file for runtime selection, prompt defaults, and limits.

## Data Flow

1. User enters input in REPL.
2. Message is appended to in-memory history and persisted.
3. Orchestrator issues generation request to selected runtime.
4. Runtime streams tokens/chunks back to REPL.
5. Assistant response is persisted and added to history.

## Planned Evolution

1. Replace text log with SQLite storage and metadata tables.
2. Add token budgeting and summarization for long sessions.
3. Add tool policy layer and explicit command-approval workflow.
4. Add first-class runtime adapters (llama.cpp C API, ONNX Runtime GenAI).
5. Add benchmark/telemetry module (local-only, opt-in).
