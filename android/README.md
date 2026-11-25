# Optional Android Bridge contract

No Android application is implemented or built in Genesis. The planned bridge
uses Kotlin, Jetpack Compose, Coroutines/Flow, native Bluetooth LE and
CompanionDeviceManager; use a foreground service only when required by Android
background connection/audio rules. Pin SDK/library versions when implementation
starts and verify current Android permission requirements at that time.

Modules: device association/session, authenticated Daemon Link transport,
configurable inference proxy, interchangeable STT/TTS, notification forwarding
and opt-in backup/configuration. The watch constructs prompts and validates
responses/actions. The bridge never owns the creature database or simulation.
Do not add a mandatory account or backend. Provider keys belong in protected
storage and must not be forwarded to model context.

Removing/replacing the app must leave the watch identity and history intact.
Reconnect negotiates a new session after watch deep sleep. Audio capture is
push-to-talk; microphone permission must not imply always-on recording.
Notifications, backups and remote paid-provider use need explicit configuration.
See `../protocol/README.md` for transport/session limits and unimplemented gates.

## Deferred phone tool executor

The C++ `PhoneTools` contract is now consumed by the watch's durable dispatcher.
An Android implementation is still absent. The bridge must advertise only
handlers it actually implements, after authentication and local configuration:

| Tool | Arguments | Meaning of successful result |
|---|---|---|
| `set_alarm` | `text`, `at` (UTC epoch) | The selected alarm handler confirms registration |
| `send_message` | `text`, opaque `recipient` reference | The configured messaging channel confirms its defined send operation |

Do not advertise generic control of every messaging app. A compose/share screen
opening is not proof of sending. Each channel needs its own adapter, verified
recipient resolution, permission handling and explicit completion semantics.
Choose and validate those Android APIs/permissions when implementing the APK.

Receive `tool_request` with the watch operation ID and immutable arguments;
check capability, expiry and recipient reference again on the phone. Keep a
durable operation ledger keyed by watch identity + operation ID, independent of
BLE session IDs. Retain `tool_result` until `tool_result_ack` proves that the
watch committed it. `tool_status_request` retrieves an existing outcome without
performing the action again. Do not infer success from a connection or ACK.
The watch sends a message only after local confirmation; the model has no API
for granting that authorization. Reconnection must reconcile results, not replay
uncertain effects. The C++ fake in `tests/assistant_tests.cpp` verifies the watch
contract only; it is not an Android or transport implementation.

## Service catalog and scheduled workflows

The watch now consumes `BridgeServices` / `BridgeServiceCatalog` and runs scheduled
agent workflows. Bindings such as `mail.personal`, `chat.personal` and
`search.primary` will map to actual API adapters in the APK. Supported operations:
`web.search`, `email.search`, `email.read`, `email.send`, `message.send`,
`notification.reply`, `set_phone_alarm`. Local watch policy must enable them.
Never silently reassign a binding to a different account/recipient namespace.

Return actual bounded results and sources; the watch persists them and resumes
inference. The APK does not own instructions, grants, scheduling or creature state.
The core confirmation API exposes the entire immutable send payload; presentation
must show binding/account, subject, target/reference, body and schedule.

See [scheduler/service APIs](../docs/architecture/agent-tasks.md) and the protocol
schemas/examples. No real email, WhatsApp, search, alarm or authenticated wire
handler is implemented in this repository yet.

## Voice identity boundary

The portable watch core now implements a local voice directory and permission
checks; see [voice directory](../docs/architecture/voice-directory.md). Android is
still an interface contract, not a working APK.

Provide a versioned speaker encoder for the agreed 192-float normalized embedding,
quality/voiced duration/overlap metadata, STT and a unique utterance capture ID.
Bind transcript and voice metadata to the same explicit capture, over authenticated
transport. Neither spoken names nor JSON actor/role claims constitute identity.
Do not expose the internal C++ session handles as BLE bearer tokens.

Implement fresh profile-bound local PIN/biometric evidence and a template protector
using authenticated encryption with protected keys. The core deliberately has no
plaintext/permissive production defaults. The watch owns the authoritative
profile, encounter, grant and task records; key custody/recovery must preserve that
ownership and define offline behavior. Android account bindings must enforce the
watch-assigned principal scope and never substitute the owner's default account.
Complete enrollment/directory/approval UI and audio cancellation on lock remain
integration work. Voice recognition alone never unlocks mail or message sending.
