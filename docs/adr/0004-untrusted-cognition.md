# ADR 0004: cognition proposes bounded capabilities

Status: accepted, 2026-11-25.

Genesis implements a deterministic streaming mock, not a live provider. The
`InferenceProvider` contract supports bounded context, token limits, polling and
cancellation; a real configurable OpenAI-compatible HTTPS adapter follows later.
Context composition remains on the watch. Raw response budget is 2048 bytes,
context 4096 bytes and recalled memories eight records.

Choose a narrow non-recursive JSON decoder for five required fields, known enums
and <=4 actions. Reject unknown/duplicate fields, oversized data, non-finite
numbers and unauthorized capabilities before effects. Unicode escape decoding,
intentions and personality proposals are explicitly deferred; this is not a
general-purpose JSON library. Expand or replace it with a reviewed bounded parser
before enabling arbitrary external provider output.

Actions are speak (text presentation), animate (validated presentation state),
remember (locally queued proposal). No action accepts a file path, SQL or code.
The model cannot mutate identity or vital state. Future peripheral actions need
their own explicit capabilities and consent policy where appropriate.
Rate limit is one request per minute; timeout 10 s; failures exponentially back
off up to 32 minutes. Disabled cognition, low battery and connectivity loss
cancel provider work without stopping instincts.
