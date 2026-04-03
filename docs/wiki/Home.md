# LocalDrop Wiki

LocalDrop is a local-network file sharing service written in C. It is designed around a simple operational model:

- discover peers on the LAN
- unlock local control with a short admin code
- pair peers explicitly before sending
- queue remote transfers outside request threads
- keep files on the local network

## What the project includes today

- mDNS service discovery using Avahi
- local HTTP server using `libmicrohttpd`
- outgoing HTTP transfer client using `libcurl`
- token generation and pairing helpers using OpenSSL-backed randomness
- a browser-based UI rendered directly by the binary
- staging and download storage separation
- CI coverage for hardening, ASan, UBSan, and TSan

## What the project does not include yet

- encrypted transport
- resumable uploads or downloads
- release packaging for multiple operating systems
- user accounts or multi-user access control

## Typical flow

1. Start LocalDrop on one or more machines on the same network.
2. Read the local admin code from the terminal.
3. Unlock the UI in the browser.
4. Generate a temporary pairing code on the receiver.
5. Pair from the sender to the receiver.
6. Upload a file for local receipt or enqueue a remote transfer.
7. Monitor transfer status from the queue view.

## Documentation map

- [Architecture](./Architecture.md)
- [API](./API.md)
- [Development](./Development.md)
- [Roadmap](./Roadmap.md)

## Project status

LocalDrop should be treated as an early open source project with a working core and a still-evolving product surface. The internals have explicit protections for peer authorization, rate limiting, JSON escaping, staged file writes, and concurrent access to peer state, but there is still room to improve transport security, packaging, and test depth.
