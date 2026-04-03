# Architecture

## Runtime overview

LocalDrop runs as a single local process with several cooperating subsystems:

- `src/main.c`: process bootstrap, local IP detection, QR output, subsystem startup and shutdown
- `src/server.c`: HTTP routing, request validation, upload handling, pairing endpoints, and JSON responses
- `src/ui.c`: HTML, CSS, and JavaScript for the embedded web UI
- `src/discovery.c`: Avahi registration, browsing, resolving, and polling
- `src/peer_registry.c`: synchronized in-memory registry of discovered peers
- `src/auth.c`: local admin code, UI sessions, pairing codes, peer tokens, and rate limiting
- `src/storage.c`: staging uploads, filename sanitization, commit/abort behavior, and local file cleanup
- `src/transfer.c`: libcurl-backed remote upload and pairing exchange client
- `src/transfer_queue.c`: background worker, queue state, and transfer status API surface
- `src/json_utils.c`: safe JSON escaping and append helpers
- `src/crypto.c`: random token and numeric code generation

## Main lifecycle

At startup LocalDrop does the following:

1. Initializes the peer registry.
2. Initializes libcurl global state.
3. Creates the local auth state and prints the admin code.
4. Ensures `downloads/` and `staging/` exist.
5. Starts the transfer queue worker.
6. Starts Avahi discovery when available.
7. Starts the `libmicrohttpd` daemon on port `8080`.

At shutdown it stops the server, discovery, queue worker, storage, auth, libcurl, and registry in reverse order.

## Concurrency model

- `libmicrohttpd` runs with `MHD_USE_THREAD_PER_CONNECTION`.
- The transfer queue has one dedicated worker thread for remote sends.
- Shared auth state and queue state use `pthread_mutex_t`.
- Shared peer state is centralized in the registry module instead of ad hoc globals.

This design keeps network request handling simple while avoiding direct synchronous remote transfer work inside request threads.

## Storage model

Incoming and outgoing files do not go directly from socket to final path.

- `staging/`: temporary upload content
- `downloads/`: committed local files

Upload stages are:

1. `upload_stage_open`
2. `upload_stage_write`
3. `upload_stage_seal`
4. either `upload_stage_commit` or `upload_stage_abort`

This prevents partially received files from being treated as completed downloads.

## Security model

LocalDrop currently protects actions with three layers:

- local admin code to unlock UI actions
- pairing code exchange to establish trust between peers
- per-peer token validation on remote upload endpoints

Additional controls:

- request rate limiting by client IP and scope
- destination validation against active discovered peers
- JSON escaping before browser rendering
- filename sanitization before writing staged files

## Transfer model

`/api/send` does not synchronously push a file to the target peer from inside the HTTP handler.

Instead it:

1. validates the target peer
2. seals the staged upload file
3. enqueues a transfer job
4. returns a `job_id`

The worker thread then performs the remote upload with `libcurl` and updates transfer status for polling via `/api/transfers`.
