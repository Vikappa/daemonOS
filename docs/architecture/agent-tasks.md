# Scheduled agent tasks and service interfaces

The core runs due task -> inference -> typed tools -> durable results -> follow-up
inference -> durable notice. The tests use an injected clock, scripted provider
and fake bridge. No real search/email/messaging API is invoked.

## Local scheduler API

All calls run on the Runtime thread; adapters must outlive Runtime. Configure
external operations locally before scheduling tasks that need them:

```cpp
ToolPolicy policy;
policy.services = tool_bit(ToolName::web_search);
runtime.assistant().phone(&bridge, policy);

AgentTaskRequest task;
task.id.assign("morning-briefing");
task.instruction.assign("Cerca le notizie e raccontami i risultati con le fonti.");
task.execute_at = due_epoch;       // caller already resolved UTC/timezone
task.expires_at = due_epoch + 900;
task.grants = tool_bit(ToolName::web_search);
task.wake_on_complete = true;
Status scheduled = runtime.schedule_agent_task(task);
```

| API | Purpose |
|---|---|
| `scheduler().get(id, task)` | State, accepted turns, starts and complete result |
| `scheduler().list(offset, page)` | Up to four stored tasks |
| `scheduler().cancel(id)` | Stop pending work without claiming to unsend effects |
| `agent_result()` | Full notice currently offered to presentation |
| `acknowledge_agent_result(id)` | Durably dismiss a result |
| `next_wake_at(epoch)` | Earliest task/tool/reminder deadline; -1 none, 0 immediate work |

The model-facing `schedule_agent_task` takes `text` and `at`; its top-level
`expires_at` becomes the task deadline. Direct local requests allow 383 instruction
bytes; the model tool allows 255. The model cannot set grants. Tasks are one-shot,
within a year; unresolved local-time/recipient ambiguities belong to the caller.

States are scheduled, inferring, waiting_tools, completed, failed and cancelled.
While waiting, inference is inactive. Only settled job statuses/results/sources
feed the next turn. Invalid calls fail before effects; transaction failures roll
back the whole turn. Provider interruptions can retry the uncommitted turn within
deadline/backoff/budget. Cancellation prevents queued effects; already-dispatched
effects retain uncertain outcomes. Unread final notices survive restart.

Wake policy updates the simulated creature/display policy. Physical RTC wake,
audio/TTS and a complete assistant/approval UI remain adapters/presentation work.
The built-in fixed mock explicitly rejects scheduled routines; use an actual or
scripted provider. `next_wake_at` exposes the interface for a future RTC/power driver.

## APK service contract

`BridgeServices` (`PhoneTools` alias) exposes `supports`, `services`,
`supports_binding`, `authenticated`, `dispatch`, `poll`, `accept_result`.
`BridgeServiceCatalog` validates up to 16 unique bindings. A binding names a
configured adapter/account; do not silently reuse its ID for a different account.

| Operation | Required arguments | APK mapping to implement |
|---|---|---|
| `web.search` | text (query) | Configured search API |
| `email.search` | text (query) | Configured email search API |
| `email.read` | reference | Provider message ID namespace |
| `email.send` | recipient, subject, text | Configured email send API |
| `message.send` | recipient, text | Configured messaging adapter |
| `notification.reply` | reference, text | Configured notification reply adapter |
| `set_phone_alarm` | at, text | Configured phone alarm adapter |

All bridge calls may include `binding`; empty means the configured default.
`set_alarm` and `send_message` remain parser aliases. Recipients/references are
resolved opaque IDs; packages, credentials and provider keys stay outside prompts.
An installed app alone does not prove an operation is available.

For message writes, `assistant().confirmation(job)` returns the full immutable
binding, target/reference, subject, body and schedule. Local UI reviews that object
and calls `confirm(id)`; the model cannot approve itself. The adapter validates
its actual account permissions, recipient reference, availability and expiry too.

`dispatch` means transport acceptance. `poll` peeks a retained `ToolResult`;
the watch commits status/data before `accept_result`. Results allow 767 bytes of
data, 191 of detail and two bounded HTTP(S) source URLs. Larger responses require
an explicit bounded summary/reference. Durable phone-side deduplication uses
watch identity + operation ID; reconnect reconciles outcomes without re-execution.
Opening a compose/share screen must not return send success.

Wire contracts and examples: `cognitive-v3`, `service-catalog-v1`,
`service-result-v1`, `agent-task-v1` under `protocol`. The local task schema is
trusted configuration, not a model permission API. Authenticated serialization,
APK routing and actual app/provider handlers are still to be implemented.
