# ADR 0008: Local voice directory and independent authorization

Status: accepted, 2026-11-25. Audio extraction, strong authentication and key storage
remain platform integrations, not implemented production services.

## Decision

The watch owns a bounded `VoiceDirectory`, backed by the local `IdentityStore`.
Stable profiles have exactly one role: owner, authorized, or guest. A guest can
accumulate real, timestamped encounters across contexts without gaining authority.
Only an unlocked owner can enroll voices, change roles/grants, assign labels,
configure guest tracking, inspect timelines or forget voice data.

Speaker matching consumes normalized 192-dimensional vectors from a versioned
encoder. Maximum cosine similarity is computed per person, across up to three
explicitly enrolled templates. A configurable threshold and a gap from the second
person reject uncertain matches. Low-quality, short, non-finite and overlapping
samples are refused. Unknown speakers do not inherit the nearest known identity.
Near-threshold unmatched voices do not automatically create another guest.
Templates never adapt automatically from conversation; repetition updates history,
not authority or biometric templates. Different encoder versions never compare.

A speaker match creates a short-lived, in-process session with weak identity
strength. Only explicitly granted low-impact tools are eligible in that session.
Private history and message/mail operations require local unlocking. Every outgoing
message additionally requires unlocked-owner approval of its immutable payload.
Jobs/tasks persist an actor and captured grants, checked against current policy
before execution/continuation. Revocation cannot recall an already dispatched
message; uncertain delivery is never replayed.

No production plaintext protector or permissive authenticator is supplied. The
platform must implement `VoiceprintProtector` using authenticated encryption and
protected keys, and `LocalIdentityAuthenticator` using fresh, profile-bound local
proofs. Templates are opaque sealed blobs in SQLite; raw audio is not stored.
Guest tracking defaults off. The microphone remains off until explicit capture.

## Alternatives

- Let the LLM infer the speaker/role: rejected; untrusted text cannot authorize tools.
- Grant full access after a voice match: rejected; replay, synthesis, ambiguity and
  accidental matches make voice alone unsuitable for consequential actions.
- One shared memory/account for everyone: rejected; recognized guests and other
  authorized users must not receive owner data or default owner service bindings.
- Automatic template/name merging or privilege promotion: rejected; erroneous
  matches could poison identity and extend permissions without owner intent.
- A mandatory remote biometric service: rejected; local state remains authoritative
  and matching/policy work independently of network availability.

## Consequences

No new runtime dependency. Matching is bounded by 48 profiles x 3 templates x 192
floats. Fixed output pages and eight sessions limit domain allocations; SQLite
still allocates in its desktop adapter. Schema v5 adds identities, templates,
encounters and actor/approval metadata without deleting existing user records.
The directory must outlive Runtime and share its owning thread.

Matching defaults are engineering placeholders, not calibrated accuracy or replay
protection. Real microphones, language/acoustic conditions, encoder resources,
threshold calibration, enrollment UX, proof validation, encryption, BLE framing,
permissions UI and device tests remain necessary before actual deployment.
