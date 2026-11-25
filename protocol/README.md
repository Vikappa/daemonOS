# Dæmon Link protocol proposal, v1

This is a versioned integration contract, not a deployed BLE/HTTP implementation.
Genesis uses an in-process streaming provider and SDL input. Production network
transports, framing, authentication and CBOR encoding are not implemented yet.

The watch owns identity, memory, scheduling, prompt composition and actions.
The bridge offers optional proxy/audio/configuration/backup services. Replacing
or uninstalling it cannot delete authoritative watch state. Providers receive
only the context chosen by the watch and return untrusted proposals.

## Session and messages

On connection: exchange `hello` with supported major/minor versions, device and
session nonces, capabilities, maximum message size and codecs. Select common
major version; reject incompatible versions explicitly. Authenticate the session
before personal data. Negotiate individual capabilities; no remote endpoint can
grant itself watch permissions. A fresh session cannot replay commands from an
earlier session. Credentials are configured via a protected channel and never
appear in prompts, telemetry or backups by default.

The JSON debugging envelope requires `v`, `session`, `id`, `type`, `seq`, `final`
and `payload`; `reply_to` correlates replies. UUID-like request identifiers are
opaque strings; each stream has increasing sequence numbers. Structured errors
carry code/message/retryable. Maximum control envelope is 4096 bytes including
UTF-8 data. Treat identifiers as data, never filesystem paths or executable names.
Initial types: hello, hello_ack, error, event, command, inference_request,
inference_chunk, audio_start, audio_chunk, audio_end, cancel, backup_request,
backup_chunk, ack. Backups and notifications are opt-in capabilities.

OpenAI-compatible mapping: adapter supplies configurable base endpoint, model,
max tokens, timeout and generation parameters, builds `/v1/chat/completions`
messages from the watch context, and normalizes JSON/SSE chunks to the provider
contract. No model/provider is mandatory. llama.cpp/Ollama compatibility must be
tested rather than inferred from branding. HTTPS certificate validation is required;
remote inference endpoints need authentication. Plain HTTP is development-only
on loopback or an explicitly trusted local network, never an Internet default.

## Transports, framing and flow

Planned: BLE GATT for watch/bridge, HTTPS and SSE for providers, optional WebSocket,
localhost for desktop integration and in-memory transport for deterministic tests.
Keep the session/message layer independent of these adapters.

Proposed BLE fragment header is 12 little-endian bytes: magic u16 `0xDACE`, major
u8, flags u8 (start/end/ack/cancel), stream u16, sequence u16, payload length u16,
receive-credit u8, reserved u8. ATT MTU 23 leaves 20 value bytes and only 8 payload
bytes after this header; negotiate larger MTU but never assume it. Maximum logical
message 4096 bytes, at most two concurrent reassembly streams (8 KiB PSRAM), 8
fragment credits per stream. Validate lengths and limits before allocation;
duplicate/out-of-order fragments produce structured errors. Reset a stream at
sequence wrap. Credit exhaustion pauses transmission; cancellation has reserved
control capacity. Audio frames may be dropped with discontinuity markers rather
than growing unbounded queues. Throughput and buffer budgets require measurement.

CBOR with fixed integer keys is the preferred embedded candidate; JSON supports
interoperability/debugging. Wire keys and UUID assignments remain provisional
until an encoder/decoder interoperability test exists. Do not implement two
different semantic protocols for CBOR and JSON.

Requests time out at 10 s by default, bounded by watch policy. Correlation IDs
support cancellation and terminal replies. Reconnect uses capped exponential
backoff with jitter; pending operations fail explicitly, rather than silently
replaying non-idempotent actions. BLE deep sleep disconnects the session; wake
requires a new authenticated session. Backup transfer resumes only after
verified chunk checksums and explicit transfer IDs.

## Authentication and audio gates

Use OS BLE bonding plus authenticated pairing. A display confirmation or protected
out-of-band exchange must prevent unattended replacement of the bridge. LE Secure
Connections Just Works alone is not MITM protection. Application session crypto
requires a reviewed library/protocol, not custom cryptography. TLS is mandatory
for remote HTTPS. Encrypt/protect provider credentials through NVS/Android Keystore
adapters and redact diagnostics. None of this is claimed implemented in Genesis.

Push-to-talk is the default, microphone inactive otherwise. Start negotiates rate,
channels, codec and chunk duration. PCM 16 kHz mono is a reference format;
IMA-ADPCM and Opus are candidates pending CPU/RAM/latency/quality/BLE measurements.
Stop/cancel must stop capture, playback and queued animation work. The bridge's
STT/TTS adapters are interchangeable. Real speech, audio sync, VAD and codecs are
deferred to Voice. See [Opus reference documentation](https://opus-codec.org/docs/).
