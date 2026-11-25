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

## Deferred tools (core implemented, wire adapter pending)

Inference response v2 adds up to two `tool_calls`; the v1 presentation-only
response remains accepted. See `schemas/cognitive-v2.schema.json` and
`examples/deferred-tools.json`. Each call has `id`, `name`, `execute_at`,
`expires_at`, and a strictly typed `arguments` object. `execute_at=0` means now;
`expires_at=0` means no expiry for local-only operations. Phone tools require an
expiry. Names are allowlisted, never arbitrary function paths.

Local `save_note`/`set_reminder` operations commit an explicit user entry, separate
from autobiographical memories. `set_alarm`/`send_message` use the optional phone
adapter only if enabled and negotiated. The watch persists and schedules jobs;
inference can disconnect immediately. Only explicit user requests authorize tool
proposals; recalled data and autonomous thinking cannot submit them.

The envelope now reserves `tool_request`, `tool_result`, `tool_result_ack` and
`tool_status_request`. Wire serialization, authenticated session binding and the
APK are not implemented. An execution result correlates by durable operation ID
across sessions; anti-replay session checks still apply to each envelope. The
phone retains results until the watch durably accepts them. Status reconciliation
never re-executes an uncertain action. Terminal, pending and uncertain statuses
are included in the next bounded cognitive context; no automatic inference loop
is started just to narrate a tool completion.

The simulator's provider remains a fixed mock; it does not understand free-form
user commands. `Runtime::request` and the structured parser/dispatcher are ready
for a real provider. `PhoneTools` is a typed adapter boundary, not evidence of
Android integration. See ADR 0005 for states, consent and crash behavior.

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

## Scheduled workflows and service bindings (cognitive v3)

`cognitive-v3.schema.json` extends the accepted response with `schedule_agent_task`,
provider-neutral service names and binding/subject/reference arguments. v1/v2 and
legacy `set_alarm`/`send_message` names remain accepted. Model task creation cannot
supply operation grants; local policy captures them. The trusted local API shape
is `agent-task-v1.schema.json`, which must never be exposed as a model permission
endpoint. Scheduling is one-shot UTC with an explicit execution window.

`service-catalog-v1` declares configured bindings and supported operation names.
Local policy intersects them; the APK will map them to actual apps/accounts/APIs.
`service-result-v1` includes status, bounded data, error and two source URLs. Core
interfaces and validation are implemented; authenticated wire codecs/routers and
actual APK providers are not. Reserved envelope types now include `service_catalog`,
`task_result`, `task_result_ack` for later transport/presentation integration.

The scheduler starts inference at the due time, persists child jobs transactionally,
waits without active inference, then feeds the actual results/sources to a bounded
continuation. IDs derive from the task/turn/slot. It persists final speech and unread
notice; a local acknowledgement dismisses it. Service/mail/search text is untrusted
data. Recovery cannot silently repeat an uncertain external effect. See
`docs/architecture/agent-tasks.md` and ADR 0006 for lifecycle, quotas and wake APIs.

## Voice directory host contract

`VoiceObservation`, `SpeakerEncoder`, `LocalIdentityAuthenticator` and
`VoiceprintProtector` are bounded internal C++ integration contracts, not implemented
wire endpoints. See [voice directory](../docs/architecture/voice-directory.md).
The bridge must authenticate/pair before transferring captures or embeddings and
bind utterance, transcript, capture ID, quality, overlap and encoder version.
Network/model input cannot select role, actor or authority. Session handles are
in-process only; an authenticated wire codec and proof/enrollment UX remain future
work. Cognitive response v3 intentionally has no identity-administration tool.
## Habitat / Awakening response v2

`schemas/cognitive-v2.schema.json` is the current mock response contract;
`examples/cognitive-silent-v2.json` demonstrates a response with no speech.
Legacy five-field v1 remains accepted for compatibility. V2 requires explicit
version, nullable speech, emotion/intensity, facial expression, animation,
nullable bounded voice parameters, known actions and bounded proposal arrays.
At most four unique actions and two candidates of each proposal kind fit within
2048 raw bytes. Personality deltas are at most 0.05 per distinct trait; intentions
are relative delays of 1..604800 seconds. Unknown/duplicate fields, invalid UTF-8,
non-finite numbers, unknown enums, unsupported escapes and oversized data fail.
The bounded decoder supports raw UTF-8 and ordinary quote/backslash/newline/tab
escapes; Unicode escape sequences are deliberately rejected in Habitat.

The watch validates every capability before effects. Current creature permissions
allow animate, text speech and memory proposals. Personality/intention execution
is denied even when syntax is valid; neither changes permissions or identity.
Voice parameters do not start audio. Agent guidance is local, <=1023 bytes;
context also contains persistent name/identity, personality, body/emotion state,
trigger and selected memories. No real provider is added in Habitat.

## Integrated response compatibility

Canonical cognitive-v2 remains the Habitat nullable-speech/proposal contract. The
older deferred-tool response is retained as cognitive-tools-v2. Cognitive-v3 accepts
legacy tool output or Habitat v2 with optional bounded tool_calls. The runtime
validates every proposal/tool and denies ungranted identity/permission changes.
