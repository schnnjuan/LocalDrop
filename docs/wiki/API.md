# HTTP API

The API is served by the local embedded HTTP server.

Base URL:

```text
http://<host>:8080
```

## Headers

| Header | Purpose |
| --- | --- |
| `X-LocalDrop-UI-Token` | Authenticates local UI actions after admin unlock |
| `X-LocalDrop-Pair-Token` | Authenticates paired peer uploads |

## Routes

### `GET /`

Returns the embedded web UI.

Auth: none

### `GET /api/devices`

Returns discovered peers as JSON.

Auth: none

Typical fields:

- `name`
- `ip`
- `port`
- `active`
- `paired`
- `last_seen`

### `POST /api/session/unlock`

Unlocks the local UI session.

Auth: none

Form fields:

- `admin_code`

Success response:

```json
{
  "status": "ok",
  "message": "Sessao destravada com sucesso",
  "token": "<ui-token>"
}
```

### `GET /api/pair/local-code`

Returns a short-lived pairing code for the current node.

Auth: `X-LocalDrop-UI-Token`

Success response:

```json
{
  "status": "ok",
  "code": "123456",
  "expires_at": 1760000000
}
```

### `POST /api/pair/exchange`

Completes the receiver-side part of a pairing flow and returns a peer token.

Auth: none

Form fields:

- `pair_code`
- `peer_name`
- `peer_port`

Success response:

```json
{
  "status": "ok",
  "message": "Peer pareado com sucesso",
  "token": "<pair-token>",
  "expires_at": 1760000000
}
```

### `POST /api/pair`

Initiates pairing from a local unlocked UI to a discovered remote peer.

Auth: `X-LocalDrop-UI-Token`

Form fields:

- `target_ip`
- `target_port`
- `pair_code`
- `target_name` optional

### `POST /upload`

Receives a file on the current node.

Auth:

- `X-LocalDrop-UI-Token`, or
- `X-LocalDrop-Pair-Token`

Multipart fields:

- `file`

### `POST /api/send`

Stages a file locally and enqueues a remote transfer job.

Auth: `X-LocalDrop-UI-Token`

Multipart fields:

- `file`
- `target_ip`
- `target_port`
- `target_name` optional

Success response:

```json
{
  "status": "ok",
  "message": "Transferencia enfileirada com sucesso",
  "job_id": "<job-id>"
}
```

### `GET /api/transfers`

Lists known transfer jobs.

Auth: `X-LocalDrop-UI-Token`

### `GET /api/transfers/<job_id>`

Returns a single transfer job status.

Auth: `X-LocalDrop-UI-Token`

Typical fields:

- `job_id`
- `status`
- `target_ip`
- `target_port`
- `bytes_sent`
- `total_bytes`
- `created_at`
- `updated_at`
- `error_message`

## Common error classes

| Status | Meaning |
| --- | --- |
| `400` | Invalid or incomplete request payload |
| `401` | Missing or invalid local session or pair token |
| `403` | Known request type, but target peer or pairing state is not allowed |
| `404` | Unknown route or transfer job not found |
| `413` | Uploaded file exceeded the configured maximum size |
| `429` | Rate limit reached |
| `502` | Remote pairing exchange failed |
| `503` | Transfer queue unavailable |

## Operational notes

- Pairing codes are temporary.
- UI sessions are bound to the client IP that unlocked them.
- Remote sends are only allowed to active discovered peers that are already paired.
- Transfer state is eventually cleaned up from memory after retention windows expire.
