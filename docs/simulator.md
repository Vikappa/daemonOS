# Desktop simulator

Build and run commands are in the root README. Linux/Ubuntu 24.04 is the tested
host. Watch coordinates are always 410 x 502. The second 460 x 960 LVGL display
is a developer panel. Both are rendered on one thread through a small SDL HAL,
using the same `daemon_presentation` as firmware, partial redraw and ARGB8888 draw buffers. Mouse maps to capacitive
touch; `H`, `Esc`, `Space` simulate home/back/wake buttons. Only left click is
accepted. Closing either window flushes state and the minimal checkpoint.

The lab controls virtual clock speed (1x/60x), one-hour jumps, battery percentage,
charging, motion/steps, simulated connection loss, database faults, memory pressure, finite audio/Wi-Fi/sensor ownership and sleep/wake.
These change runtime inputs; they never write the creature's fields directly.
Battery input is not a discharge model. A connection means the mock provider can
be scheduled; it does not establish BLE or Wi-Fi. Storage fault denies adapter
operations rather than unmounting a host directory. Audio and detailed IMU/GPS/
NFC/LoRa simulations are not implemented.

Time is accelerated only in the simulator clock. Live needs use monotonic time;
the watch displays UTC. Minute quantization is intentional and the sub-minute
remainder is persisted. At a one-hour jump, idle timeout may switch off the watch
display: use **Wake / button**, Space, or a watch interaction. Deep sleep here
is simulated policy; the host still runs and can inject inputs. A real deep-sleep
wake will reconstruct needs using RTC and reconnect BLE.

On the launcher and creature screen, the familiar uses the same 13 x 12 LCD
face. Petting and feeding show a grin, movement shows curiosity, and rest closes
both eyes. Reactions last 2.5 simulated seconds. Sleep, active thinking and
urgent needs take priority. The display stops refreshing while it sleeps.

```sh
./build/desktop/daemon_sim --data-dir /tmp/my-familiar
./build/desktop/daemon_sim --headless --frames 200 --epoch 1700000000 \
  --seed 42 --data-dir /tmp/my-familiar-test --script /tmp/demo.script \
  --screenshot /tmp/ember.bmp --report /tmp/state.json
```

Headless mode still runs real LVGL, SDL display callbacks and input handling.
It sets SDL's dummy video driver, freezes real-time clock progression and adds
exactly 16 virtual ms per frame. `--epoch` is a wall-clock seed; `--seed` is only
for reproducible test identity generation, not production cryptographic keys.
Using the lab speed toggle in headless mode intentionally re-enables real time;
deterministic tests do not use that control.

Scripts contain at most 256 simple commands. Each click holds the real SDL mouse
button for three frames, releases it, and allows LVGL to process the event before
the next command. Targets must exist on the active page. Scripts do not bypass
the UI event path.

```text
click open_creature
click feed
click pet
click rest
advance 3600
click wake
click network
click think
wait 30
```

Other targets: `open_clock`, `open_settings`, `home`, `brightness_down`,
`brightness_up`, `cognition`, `battery_down`, `battery_up`, `charge`, `move`,
`storage`, `suspend`, `advance_hour`, `speed`. `wall EPOCH` changes RTC without
changing monotonic time. Set a sufficiently large `--frames` value; incomplete
scripts fail rather than silently succeeding.

`--debug-screenshot /tmp/lab.bmp` captures the separate developer display.
The report contains identity, state, app, simulated conditions, power state,
rendered-frame count and mouse clicks. Screenshots capture the actual watch
framebuffer before simulated brightness modulation, so even a sleeping display
can be inspected. They do not include the debug panel or desktop window chrome.
The same palette asset is compiled into both targets; replacing its source rebuilds the resource. Standalone asset packaging remains future work.

Habitat adds `--name NAME` (persistent, 1..31 bytes) and `--agent PATH` (<=1023 bytes).
The default agent file is the repository's `config/agent.md`, also embedded in
firmware. Names are independent of DæmonOS branding; Ember remains the default.
The lab has pressure, empty-battery, five-second speaking/Wi-Fi/sensor lease and
contract-inspection controls. Reports include asleep flushes, simulation updates,
active locks, sleep duration, memory peak and queue peak. The watch performs zero
flushes while off; its last framebuffer is retained for inspection.

Real-time execution blocks with SDL event waits and only due work updates the
runtime/view. Touch is delivered on events, not a continuously polled timer.
The lab alone samples diagnostic text at 2 Hz. Its displayed memory is a resource
reservation model, not host RSS or measured device consumption. Speech controls
simulate a power consumer; they do not claim a working audio pipeline.

## LCD activity expressions

The shared watch view renders the approved cat faces, activity phases and speech
mouth poses. `--mock-delay-ms 0..9000` simulates provider response latency. Script
commands `activity install`, `installed`, `phone_offline`, `phone_connecting`,
`phone_ready`, `inference_offline`, `inference_connecting`, `inference_ready`,
`error`, `clear_error`, `listen`, `stop_listen`, `speak`, `voice`, `silence`, and
`stop_speak` inject correlated device events. These are lab hints, not real audio
or network adapters. Ordinary model speech text never starts mouth playback.
