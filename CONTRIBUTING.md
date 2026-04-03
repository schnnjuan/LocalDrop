# Contributing to LocalDrop

Thanks for considering a contribution.

## Before you start

- Read the [README](./README.md) for project scope.
- Read the docs in [docs/](./docs/README.md).
- Check [SECURITY.md](./SECURITY.md) before reporting sensitive issues.

## Ways to contribute

- fix bugs
- improve tests
- improve docs
- refine the UI or developer workflow
- propose platform compatibility improvements
- harden security or request validation paths

## Development workflow

1. Sync with `main`.
2. Create a feature branch.
3. Make focused commits.
4. Run the most relevant local tests.
5. Open a pull request with context and verification notes.

## Build and test

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

If you touch concurrency, auth, or request parsing, also consider sanitizer runs documented in [docs/wiki/Development.md](./docs/wiki/Development.md).

## Coding expectations

- Keep C code compatible with the current CMake setup and C11 target.
- Prefer small, reviewable changes over broad rewrites.
- Preserve existing security checks unless you are deliberately replacing them with something better.
- Add tests when changing API behavior, token handling, storage flow, or queue behavior.
- Update docs when behavior or public workflow changes.

## Pull request checklist

- Describe the user-facing or operator-facing change.
- Explain any security or behavior tradeoffs.
- List the commands you ran for verification.
- Call out anything intentionally left out of scope.

## Documentation changes

Docs improvements are welcome even without code changes. If you update architecture, API, or operational behavior, keep the wiki pages and README aligned.
