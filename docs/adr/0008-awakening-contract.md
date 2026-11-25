# ADR 0008: versioned, silent-capable cognitive proposals

Status: accepted, 2026-11-25; extends ADR 0004 without adding real inference.

Keep legacy v1 decoding for compatibility and make the streaming mock emit v2.
The new contract has nullable speech/voice, emotion/intensity, face, animation,
known actions, <=2 memory candidates, <=2 bounded personality deltas and <=2
future intentions. The raw response remains <=2048 bytes and context <=4096.
Validate UTF-8, enums, finite numbers, ranges, counts, duplicate fields/traits and
every capability before any effect. Reject unknown actions and malformed data.

Personality and intention execution remain denied to the creature app in Habitat;
they are proposals for Awakening. The model may remain silent. Presentation/text
effects do not imply an audio driver. Memory proposals enter the locally owned
transactional queue. No output can set identity, permissions, SQL or file paths.

Local `agent.md` supplies <=1023 bytes of configured guidance; name is a separate
persistent identity field, initially Ember. Context includes identity, personality,
body/emotional state, trigger and selectively retrieved memories. The watch owns
all of them. No server, phone or model becomes authoritative.
