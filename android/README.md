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
