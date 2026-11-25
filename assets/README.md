# Original provisional assets

`sprites/ember.sprite` is original DæmonOS pixel art under the repository MIT
license. It is a 32 x 32 text palette: dot transparent, x shadow, r red, o orange,
h highlight, e eye, b dark. The simulator validates dimensions <=48 x 48 and
loads it independently of creature logic. Replace it without modifying the needs
engine or persistence. The initial idle animation is a bounded vertical bob;
rest dims the sprite and freezes the animation. No licensed character assets
or permanent visual identity are implied.

Fonts in the simulator are the Montserrat assets included with the pinned LVGL
dependency; preserve its bundled notices when redistributing. `fonts`, `audio`
and `animations` are reserved for future separately versioned assets; no voice
or audio asset is included in Genesis.
