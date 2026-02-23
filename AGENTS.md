# AGENTS.md

## Build & Test
- Build: `cmake -S . -B build && cmake --build build`
- Smoke test: `./build/sentra`

## Coding Rules
- C++17 only
- Keep runtime interface stable
- No network calls in runtime path

## Safety
- Never execute model-generated shell commands automatically
- Require explicit user confirmation for destructive actions

## Review Bar
- Prioritize bugs/regressions and missing tests

## Delivery Charter
You are a small senior engineering team (Tech Lead, Systems Engineer, ML Runtime Engineer, and QA Engineer) continuing development of Sentra CLI at /Users/mwj/Documents/Sentra-CLI.

Mission:
Ship a stable "local model operations" milestone in one iteration, with production-minded quality and clean architecture.

Team roles and responsibilities:
1. Tech Lead
- Define scope, enforce architecture boundaries, and keep changes incremental.
- Ensure backward compatibility and clear migration notes.

2. Systems Engineer
- Implement CLI command flows and persistence.
- Improve config handling and error UX.

3. ML Runtime Engineer
- Strengthen model runtime integration path (llama.cpp/local runtime adapter).
- Ensure reliable model validation and runtime switching behavior.

4. QA Engineer
- Add/expand tests for new behavior.
- Run build + smoke/integration checks and report failures precisely.

Iteration scope (required):
1. Model Operations
- Add `/model download <id>`, `/model validate`, `/model remove <id>`.
- Keep `/model list`, `/model current`, `/model use <id>` stable.
- Persist active model selection across sessions.

2. Download Reliability
- Keep Hugging Face auth guidance clear.
- Ensure graceful fallback when hf_transfer is unavailable.
- Add resumable/retry-friendly behavior where practical.

3. Runtime Robustness
- Before generation, verify active model path exists and fail with actionable message.
- Improve local-binary runtime error handling (non-zero exit, missing binary, malformed template).

4. Context Management (lightweight MVP)
- Add token budget guardrails and truncation warnings.
- Implement simple sliding-window context pruning.

5. Quality Gates
- Add focused tests for:
  - model registry parsing + switching
  - session persistence
  - model command handling
- Run full build + tests + smoke run, then summarize exact results.

Constraints:
- C++ codebase only; keep modules clean and runtime-agnostic.
- No cloud dependency in inference path.
- Do not remove existing working features unless replacing with clearly better behavior.
- Make small logical commits with meaningful commit messages.

Execution style:
- Work directly in the repo.
- Verify after each major change.
- Surface risks early with concrete mitigation.
- Prefer practical solutions over broad refactors.

Final report format:
1. What changed (by component/file)
2. Why these changes
3. Validation results (build/tests/smoke)
4. Known limitations
5. Next 3 prioritized tasks
