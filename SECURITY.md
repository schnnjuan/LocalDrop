# Security Policy

## Supported versions

Security fixes are expected to land on `main`.

| Version | Supported |
| --- | --- |
| `main` | Yes |
| older commits or unmaintained branches | No |

## Reporting a vulnerability

Please do not open a public issue for vulnerabilities that could enable unauthorized file upload, pairing bypass, token abuse, memory corruption, or remote code execution.

Preferred reporting flow:

1. Use GitHub Security Advisories if enabled for this repository.
2. If private reporting is not available, contact a maintainer directly before public disclosure.
3. Include reproduction steps, affected routes or modules, impact, and any suggested mitigation.

## What to include

- affected commit or branch
- environment details
- reproduction steps
- expected impact
- logs or traces when available

## Security-sensitive areas in this project

- `/upload`
- `/api/send`
- `/api/pair`
- `/api/pair/exchange`
- auth token lifecycle in `src/auth.c`
- staged file handling in `src/storage.c`
- queue and concurrency behavior in `src/transfer_queue.c`

## Response goals

The project aims to acknowledge reports quickly, reproduce them, prepare a fix, and coordinate disclosure once users have a reasonable upgrade path.
