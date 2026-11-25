# Original provisional assets

The familiar's original 13 x 12 LCD cat face is encoded in
`presentation/ember_sprite.cpp` and distributed under the repository MIT license.
All expressions share the same silhouette. Eyes and mouth are gaps that reveal
the green LCD background. The same masks are compiled for simulator and firmware.

Fonts in the simulator are the Montserrat assets included with the pinned LVGL
dependency; preserve its bundled notices when redistributing. `fonts`, `audio`
and `animations` are reserved for future separately versioned assets; no voice
or audio asset is included in Genesis.

The active face renderer uses the approved 13 x 12 filled LCD cat masks in
`presentation/ember_sprite.cpp`; eye/mouth cutouts reveal the same LCD background.
The former body sprite is retired. Shared LVGL presentation and firmware render
these masks directly, including activity phases and playback-driven mouth poses.
