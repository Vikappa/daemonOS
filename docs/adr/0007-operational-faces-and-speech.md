# ADR 0007: operational LCD faces and playback-driven mouth motion

Status: accepted, 2026-11-25.

Extend the approved 13 x 12 mask renderer without altering its cat silhouette,
filled ink or LCD-colored cutouts. Add seven operational faces; only feature masks
animate. Move the portable renderer into `daemon_portrait` so the simulator,
tests and mask-export tool use identical pixels. Do not generate new raster
art that changes the established code-native pixel system.

Runtime selects operational status from service lifecycle events, configured
phone link state, inference phase/response arrival, failures and capture state.
These are local facts, not model-selected emotions. Critical errors and explicit
activities take precedence; the original need-based moods and brief interaction
cues resume afterward. Phone absence and remote inference failure remain distinct.

Use a small bounded SpeechMotion controller driven by actual playback start,
normalized audio-envelope levels and playback stop. Text generation must not
start lip motion. Utterance/capture IDs protect against stale callbacks. Silent
or stale envelopes close the mouth; adapters lacking an envelope get a bounded
timing pattern. This is coarse pixel mouth motion, not phoneme-accurate lip sync.
Adapters post to the Runtime thread; they never mutate LVGL or core concurrently.

Add a uint32 event token, growing host Event from 8 to 12 bytes; the queue remains
64 fixed events. No new dependency or domain heap allocation is introduced.
Static frames do not invalidate unnecessarily; blinking and feature/mouth changes
drive updates. Listening/playback influence simulated power state. Real services,
microphone, TTS, audio timing and physical power/wake behavior still need adapters
and device validation. Deterministic core and real SDL/LVGL pixel tests cover the
new states, unchanged silhouette, mouth-only motion, silence and cancellation.
