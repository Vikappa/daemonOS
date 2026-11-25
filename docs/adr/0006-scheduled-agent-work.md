# ADR 0006: scheduled agent turns and provider-neutral services

Status: accepted, 2026-11-25. Extends ADR 0005.

Distinguish a stored-text reminder, a phone alarm, and a scheduled agent workflow.
Persist one-shot task instructions, execution window, operation grants and wake
policy on the watch. At the deadline Runtime lends its single provider for a
bounded turn, waits without active inference for durable tool outcomes, then
continues with those actual outcomes and sources. No mandatory phone/backend or
additional dependency is introduced.

The model's `schedule_agent_task` captures local policy; it cannot supply grants.
Every subsequent call intersects immutable task grants with current policy and
configured service bindings. A task cannot schedule children. Account/recipient
access remains an APK adapter responsibility; service declarations never grant
permissions the watch has not enabled.

Commit a turn's jobs and task advancement transactionally. Recovery restarts only
uncommitted inference, or resumes waiting on the existing jobs. Operation IDs
derive from task serial/turn/slot, not model-generated IDs. Repeated identical
external effects in the same task reuse a terminal outcome or cancel a redundant
proposal while the original is pending. Distinct dispatch times remain distinct.
Uncertain delivery never causes blind replay. Persist final speech/unread notice
until local acknowledgement. Wake policy affects the simulated creature/display;
`Runtime::next_wake_at` exposes deadlines for a future RTC adapter, not hardware proof.

Choose typed operations and configured binding IDs: `web.search`, `email.search`,
`email.read`, `email.send`, `message.send`, `notification.reply`, `set_phone_alarm`.
The APK will map them to actual apps/accounts/providers. Retain execution results
until the watch commits and acknowledges them. Sources/search/mail are untrusted
data. A compose/share screen opening is not proof of sending. Message writes
still require local confirmation of their full immutable payload.

Alternatives rejected: fixed callbacks cannot perform reasoning; phone-owned task
state breaks watch ownership; separate inference engines duplicate ownership and
budgets; unrestricted agent loops drain resources; app package names in prompts
couple the creature to implementations.

Budgets: 32 retained tasks; four accepted turns (tool calls in the first three),
eight provider starts; existing timeout/backoff; one-second continuation spacing.
Independent requests retain the minute interval. Idle scans are once per minute;
offline/disabled/low-battery due work rechecks within 30 seconds or at expiry, with
relevant environment events forcing an immediate check. Context grows from 4096
to 8192 bytes and response from 2048 to 4096 for service results/metadata; these
are PSRAM design budgets, not measured embedded resources.

This is one-shot UTC scheduling, not cron/DST interpretation. Full ledgers fail
explicitly; automatic archival remains future work. Real inference, APK API
handlers, authenticated wire adapters, presentation/audio and hardware wake
remain separate integrations. The built-in fixed mock refuses scheduled work
rather than fabricating a successful search; tests supply a scripted provider.
