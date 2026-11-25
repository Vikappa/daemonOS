# Voice directory

Implemented: deterministic matching, persistent profiles/encounters, authorization,
scoped retrieval, revocation and integration with immediate/deferred tools.
Not implemented: microphone pipeline, speaker encoder, Android PIN/biometric flow,
keystore/AEAD adapter, transport codecs or complete enrollment/approval UI.
The test adapters use synthetic vectors and deliberately insecure TEST-ONLY encoding;
they are never linked into the simulator or production core.

## Identity and familiarity

| Role | Recognition/history | Voice-only capabilities | Unlocked capabilities |
|---|---|---|---|
| owner | Explicitly enrolled; owner-assigned name | Explicit safe voice grants | Configured tools and directory administration |
| authorized | Explicitly enrolled or promoted by owner | Explicit safe subset and approved bindings | Explicit grants and service bindings; own private records |
| guest | Optional stable provisional ID; encounters across chats/days | Conversation; zero tools | Cannot unlock or change permissions |

`first_seen`, `last_seen`, observation count, distinct visits and distinct contexts
come from persisted observations, never dates claimed by a user or inferred by the
LLM. A visit is a distinct `(UTC day, context_id)` pair; extra audio captures in the
same visit do not inflate it. The context ID is an opaque conversation ID issued by
the trusted capture/session adapter. Capture IDs must be unique. Stored duplicates
are refused, including after restart. This prevents duplicate bookkeeping, not
replay attacks with fresh capture IDs.

An example with injected test dates of November 7, 23 and 25 produces one guest,
three visits, and the actual encounter timeline. No historical audio or identity
was imported from this conversation. An uttered name is stored as `claimed_name`
on the encounter, with no authority; only owner edits change the profile label or
`name_confirmed`. Familiarity alone never changes `guest` to `authorized`.
The implementation does not infer a person's real-world name, relationships or
other personal attributes from the sound of their voice.

## Platform entry points

1. Construct `VoiceDirectory(clock, random, identity_store)` and supply trusted
   `LocalIdentityAuthenticator` / `VoiceprintProtector` adapters. Runtime optionally
   accepts the non-owning directory pointer; the simulator always supplies it.
   Runtime calls `load()`. An unreadable directory keeps privileged paths closed
   while existing creature checkpoint recovery remains possible.
2. In a trusted local setup screen, `bootstrap(id, name, proof, session)` creates
   the single owner. No default proof validator exists. Setup atomically assigns
   legacy unscoped memories, entries, jobs and tasks to that owner. Existing
   message approvals have no owner signature and cannot dispatch under the new
   policy. After setup, legacy unscoped assistant/runtime requests are rejected.
3. An unlocked owner creates/edits profiles with `save_profile`, configures grants
   and optional explicit service binding IDs, and enrolls up to three samples with
   `enroll`. Enrollment increments the profile revision and invalidates previous
   sessions; unlock again after enrolling the owner. `configure` enables guest
   tracking explicitly and adjusts matching thresholds.
4. Push-to-talk/VAD/STT/encoder produce a `VoiceObservation` and its transcript.
   The adapter calls `Runtime::request_voice(observation, transcript, match)` on the
   runtime thread. Only matched/new-guest results enter cognition. Ambiguous,
   unknown, disabled and insufficient voices receive no session/request. The
   adapter must bind transcript, PCM, capture and encoder metadata to the same
   utterance; these are trusted host APIs, not unauthenticated wire methods.
5. Local unlocking uses `unlock(profile, fresh_proof, session)`; it never takes an
   owner role or identity claim from model output. Authenticator proofs must be
   fresh, consumed once, bound to profile and intent, and validated outside the LLM.
6. Use session overloads of `Runtime::request`, `schedule_agent_task`,
   `agent_result`, and `AssistantService::submit/notes/job/confirmation/confirm`.
   `confirm` needs the unlocked owner and operates on the exact stored payload.
   The UI must show recipient, content, subject, binding, reference and time before
   invoking it. Voice cannot approve. `cancel`, reminder completion and result
   acknowledgement currently require an unlocked owner.
7. `lock(session)` revokes a session immediately. Sessions expire after five
   monotonic minutes, belong to one directory instance, and are never serialized
   or sent over BLE. New captures replace previous weak sessions for that profile.
   Guest traffic cannot evict live unlocked sessions or prevent unlocking via weak
   slot saturation. Profile changes/enrollment/forgetting invalidate its sessions.
8. Owner-only paginated `profiles` and `encounters` drive the future directory UI.
   `forget_voice` transactionally removes templates and encounter history. Guests
   are removed entirely, freeing a directory slot; known profiles retain their
   owner-configured label/role/grants for future re-enrollment. This is logical
   deletion, not forensic erasure of SQLite pages or backups. Backup/key lifecycle
   and secure deletion policy belong to the platform integration.

`StateStore`, `IdentityStore`, `Runtime::agent_scheduler()` and low-level kernel methods
are trusted internal APIs. They are not capabilities available to a model, guest,
network client or APK command decoder. Production code must always inject the
initialized directory; the null directory constructor path exists for legacy
trusted simulator/domain fixtures and does not implement multi-user authorization.

## Authorization and private data

`safe_voice_tools` contains notes, local reminders, public web search and scheduled
agent work. Per-profile `voice_grants` must be a subset of both that mask and
normal `grants`. Guests have neither. Owner and authorized sessions created by
voice matching remain weak; the owner's voice is not a local unlock proof.
Authorized bridge calls require an explicitly allowed binding, even if the phone
has a default account. The bridge still validates operation support and transport
authentication separately. A binding denotes the configured account/provider
security boundary; APK implementations must not fall through to another account.

Immediate requests use immutable session identity; model responses cannot set
actor/role/grants. The core checks every returned call before a transactional
batch enqueue. Stored jobs carry the actor and grant snapshot, including immediate
rights; tasks inherit only that snapshot minus the right to schedule more tasks.
Both dispatch and each inference commit check current actor enablement/grants and
binding. Removing any captured grant stops work conservatively. Scheduled tasks
run without requiring an unlocked session to remain alive until their due time.
Owner-approved messages also record approver and profile revision; a policy change
invalidates an approval not yet dispatched. Revocation after dispatch does not
pretend the action was undone.

Owner/authorized unlocked cognition sees only that actor's memories and recent
tool results. Voice-only/guest cognition receives no private memory or prior tool
output. Scheduled cognition receives its own task results but no autobiographical
history when identity enforcement is active. Notes and task results require an
unlocked session and exact actor scope (owner may inspect jobs/tasks for review).
`SpeakerContext` contains only the device-supplied ID, label, role, strength,
allowed mask and familiarity timestamps/counts. Templates and raw audio never
enter prompts. Guest claimed names appear only in the owner-only timeline.
Public reminder/task notices are generic; their content must be fetched after
unlock. Owner-only confirmation retrieval supplies the private stored payload.
A new speaker replaces prior private presentation. Private inference text is
cleared, and speech motion cancelled, on expiry/revocation of its viewing session
at the next runtime tick. Actual TTS adapters must also cancel audio playback
when the UI/session is locked; mouth animation is not an audio security boundary.

## Matching, persistence and limits

Embeddings must have 192 finite float dimensions and nonzero norm. Templates are
normalized, sealed and associated with profile + encoder version; the protector
must authenticate both as associated data and handle nonce/key rotation safely.
Open failures fail closed; no plaintext fallback, forced matches or automatic
biometric template drift. Different model/preprocessing versions never compare;
changing the encoder requires an explicit enrollment/migration policy.

Defaults: cosine threshold .86, ambiguity gap .08, quality .60, 1200 ms voiced
audio. These are configurable, uncalibrated starting values, not probabilities,
measured accuracy or anti-spoofing/liveness guarantees. Threshold settings are
runtime calibration; only guest-tracking enablement persists in schema v5.
Calibrate thresholds for the selected encoder and actual device. Training or a
synthetic unit test cannot establish real-world false-accept/reject rates.

Limits: 48 profiles, 3 templates each, 1024 sealed bytes per template, 2048
encounters, 4 profiles/8 encounters per page, 8 session slots, 192-dimensional
embeddings. Full history rejects new observations transactionally; there is no
silent eviction or invented history. Forgetting voice data frees guest/history
space. All calls are serialized on the runtime thread; templates are read one at
a time. No new dependencies, heap allocations in matching, ambient recording,
real messaging actions or physical biometric measurements were introduced.
