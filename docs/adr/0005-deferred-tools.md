# ADR 0005: durable typed tool requests and an optional phone executor

Status: accepted, 2026-11-25. Extends ADR 0004's narrow response format.

Inference proposes operations; it does not perform them. Keep a bounded, durable
ledger on the watch, independently of inference, with two calls per response.
The local registry initially provides `save_note` and `set_reminder`; optional,
locally enabled phone adapters can offer `set_alarm` and `send_message`.
Names, arguments, times and every capability are validated before an atomic
enqueue. Autonomous Think cannot authorize tools; a user request is required.
Approval, cancellation and capability configuration are local APIs, not model tools.

`execute_at` controls dispatch time; an alarm's `arguments.at` controls when it
should ring. Usually send `set_alarm` immediately so the phone can register a
future alarm without needing the watch connected when it rings. A future message
can remain queued on the watch until its dispatch time. No LLM stays active or
wakes at the deadline. Expiry prevents stale phone actions from running later.
Times are UTC Unix seconds; natural-language/timezone interpretation needs a
validated adapter. Do not silently interpret a bare local time as UTC.

Jobs have queued, needs-confirmation, dispatched, succeeded, failed, uncertain
and cancelled states. Messages require local confirmation of the exact immutable
recipient reference, text and schedule. The provider cannot approve its own call.
The phone must resolve recipient references and implement channel-specific
permissions; syntactic identifier validation is not recipient verification.

Local entry creation and job success share a SQLite transaction. Persist the
dispatched state before crossing the phone boundary. Wait for an authenticated
execution result, not a transport ACK, before reporting success. A timeout/crash
after dispatch produces uncertain status and never automatic replay. Repeated
IDs with identical arguments are idempotent; conflicting reuse is rejected.
Retain phone results until the watch commits and acknowledges them. A phone-side
durable operation ledger and status query must resolve uncertain outcomes without
re-executing them. This cannot guarantee exactly-once external side effects.

Rejected alternatives: model-owned timers need persistent inference/connectivity;
RAM-only callbacks lose work on reboot; generic remote function names or scripts
bypass capability validation; blind retries can duplicate messages. The cost of
the durable design is extra bounded SQLite work and explicit uncertainty after
ambiguous delivery. Existing Scheduler remains for ephemeral in-process callbacks.

Resource policy: 64 retained jobs, 128 notes/reminders, four entries per page,
two jobs/results per page, 255-byte text, 63-byte recipient reference, 39-byte ID,
one-year scheduling horizon, ten-second phone result timeout. A full ledger fails
explicitly; pruning/archival is not implemented, since discarding deduplication
history silently would permit replay. Idle scans are capped to once per minute;
pending immediate/offline work currently retries its eligibility check each second.
Hardware wake alarms, haptics and power draw remain unvalidated.

Personality is compiled from `assets/personality/agent.md`; it affects the bounded
cognitive prompt and Italian local replies. It is not an authority mechanism.
Real inference, Android handlers, authenticated transport and a full assistant UI
are still integration work. Tests exercise the actual core with a fake phone;
they do not claim a working APK, SMS delivery or a physical alarm.
