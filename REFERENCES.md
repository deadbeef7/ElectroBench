# ElectroBench — Hyper-Realism Pass: Reference Notes

This file records the reference imagery and realism principles used to stage the pool-room (scene 3) and dusk-ocean (scene 2) hyper-realism pass. It is informational only — no runtime asset is fetched from the network. The scene still runs offline from its bundled shaders and `assets/teapot.obj`.

## Pool room (scene 3) — teapot + water reflections

- Utah teapot history / canonical model:
  - https://graphics.cs.utah.edu/teapot/
  - https://en.wikipedia.org/wiki/Utah_teapot

- Water reflections (darkening, absorption, mirror-vs-body):
  - https://gurneyjourney.blogspot.com/2007/12/water-reflections-part-1.html
  - https://dearingcampbell.blogspot.com/2017/09/reflections-in-water.html
  - https://onlineartlessons.com/class/the-12-secrets-of-reflective-water/

Realism takeaways applied:
- Whole-room filmic ACES (Narkowicz) tonemapping — sky, water and teapots share one camera grading, so overshoots roll off like film instead of clipping into flat white sheets (the single biggest CGI tell the render-critique battery exposed).
- A reflected object seen through water should read as a dim, tinted ghost, not a full bright replica. The water body absorbs and tints the reflection; where the surface is rippled the image breaks up and disappears.
- Reflections in water are darker than the object because some light penetrates instead of bouncing.
- Water body reads as saturated blue/cooler than the sky near the camera and blends toward the sky band at the horizon, not toward the walls' warm tile.
- Foam and expanding rings have a brighter advancing lip and softer wash inside the ring — not a flat stain.

## Dusk ocean (scene 2) — sea + sky + clouds

- Sea glitter path, fresnel, horizon sheen, foam, subsurface:
  - shader/ps14/sea_frag.glsl (in-repo, already heavily worked)

- Sky + volumetric clouds:
  - shader/ps14/sky_frag.glsl (in-repo)

Realism takeaways applied:
- Tight sun disc + small warm halo, attenuated by cloud cover.
- Glitter path: tight sparkles with a softer warm sheen under them, gated to the sun azimuth column and cloud shadows.
- Horizon sheen: far sea mirrors the sky band above it rather than fading into a haze knob.
- Haze tinted toward a cool water body so far water still reads as water.

## Pool room (scene 3) — BUILD-D7 waterline realism pass

Realism takeaways applied:

- A hull sitting in water drags a bright aerated contact collar and the
  surface climbs (meniscus) where it meets the hull — an object that floats
  or parks "cleanly" in perfectly flat water reads as pasted on.
- Reflections must anchor at the OBJECT: a reflection smear starts at the
  object's own waterline contact and lies along the mirrored view ray,
  darkened and broken apart by surface disturbance. Analytic per-fragment
  grazing darkening (the old ghost) does not survive scrutiny because it
  does not know where anything is.
- After a cavity collapses the surface keeps boiling for a couple of
  seconds — outgassed air pops into small weak rings around the impact
  before the pool returns to glass.
- Droplets landing at speed throw tiny secondary droplets back up
  (rain-on-water behaviour), hard-capped so a storm cannot avalanche.
- A live crown drags bright churn across the surface around its own base;
  the churn fades with the crown's life rather than switching off.

## Pool room (scene 3) — BUILD-D8 subsurface bubble plumes

Realism takeaways applied:

- A splash does not end at the surface: the collapsing cavity entrains air
  and a plume of bubbles keeps rising under the impact point for a couple
  of seconds — the water between splashes must not be empty.
- Bubbles have to be drawn at their PARALLAX-corrected apparent position
  (the eye ray meets the surface early of the point directly above the
  bubble at grazing angles), otherwise the specks stamp like decals and
  slide wrongly with the low camera.
- A plume must dissipate: staggered start depths and rise rates thin the
  plume naturally, and every bubble pops back into a micro-ring at the
  surface — visual energy is bookkept, never left dangling.
- One timebase for everything: plume wobble, pot spawn waves and the
  screenshot/bench harness all ride the fixed-substep sim clock. On a
  slow renderer wall time and sim time diverge and any straggler on the
  wall clock breaks determinism.

## Power Lines (scene 4) — Lain-style utility corridor

Realism takeaways applied:

- Utility-pole wire sag is a catenary, not a straight line: every conductor hangs on a parabola
  (with cosh tails at the ends), and each insulator point on a crossarm carries its own curve, so
  the wire wall reads as three-dimensional instead of a flat harp.
- Power lines are seen against the SKY: poles and wires must silhouette against a bright dusk
  gradient, with aerial haze progressively swallowing the far spans into the horizon colour.
- A low amber sun, a drifting white cloud deck and warm gravel bounce give the scene one coherent
  light story, and a filmic knee keeps the bright sky rolling off instead of clipping flat.

## Power Lines (scene 4) — BUILD-P7 realism pass

- Backlit air forward-scatters: haze toward the sun is brighter than haze away from it, and it
  thins with altitude. A single constant fog colour is one of the loudest "this is CG" tells in a
  shot with the sun in frame — but it has to stay weak, or the whole mid-distance turns into one
  featureless cream sheet.
- Ambient is a hemisphere, not a constant: at dusk an up-facing surface sees the bright sky dome
  and a wall facing the ground sees only warm bounce. Flat ambient is why everything looks painted
  with the same bucket.
- Contact darkening near the ground is the cheap stand-in for ambient occlusion; without it, boxes
  sit ON the plane instead of IN it.
- One material id per vertex beats guessing from the normal: bark grain, siding courses, pantile
  courses, tarmac aggregate and wheel-path polish, damp dirt patches — each surface needs its own
  story.
- A road is one contiguous quad with its paint on a separate level. Build P6 tiled it out of
  pieces whose extents did not meet, and a 35 cm strip of bare bright gravel ran the whole length
  of the corridor — at the camera's grazing angle that read as a pale diagonal band lying across
  the road.
- Shadows should multiply the surface, not stamp over it: alpha-blended so the painted lines
  darken too, with a decaying alpha ramp along the strip for a penumbra.
- A wire that stops in mid air reads as broken no matter how good the sag curve is. Every
  conductor belongs on an insulator bell (at the exact height of the glaze, not 5 cm above it),
  every drop on an eave bracket or a termination ferrule.
- The cloud deck projection dir.xz/dir.y explodes as dir.y approaches zero; clamping it is what
  stops distant cumulus smearing into horizontal streaks along the horizon.
- Draw the sky LAST, depth-tested, so its expensive fbm only runs on the pixels the world did not
  cover. On a pre-SSE CPU that pays for a much richer object shader.
- **A material id must be a `flat` varying.** As a smooth varying it is perspective-correctly
  interpolated, and a constant `12.0` does not survive that division intact — it arrives as
  11.9999995, the equality test fails, and the surface silently falls through to the wrong shader
  branch. Here that meant the ground-shadow pass fell through to the opaque branch, whose alpha of
  1.0 made the multiplicative blend `dst*(1-srcAlpha)` multiply the road by ZERO: hard black slabs
  lying across the street. The rounding is driver-specific, so this class of bug looks fine on the
  machine that wrote it and ships as garbage everywhere else.
- When a pass uses `glBlendFunc` to multiply (`ZERO, ONE_MINUS_SRC_ALPHA`), the source RGB is
  irrelevant and the source ALPHA is everything. A mis-routed alpha therefore does not look like a
  wrong colour, it looks like a black hole — so verify the alpha, not the colour, when debugging.

## Notes on staging

- Reference images are informational. No image is downloaded at runtime; do not add an image fetch to the binary without explicitly wiring it through Convex/actions and the user's Keys/API keys.
- Preferred live references for the user's own tuning (not fetched by the app):
  - Real pool photos where a bright object (ball, teapot, fruit) is dropped into still water and the reflection reads as a darkened, broken-up mirror.
  - Dusk ocean photos with a defined sun disc, narrow glitter path, dark off-path water, foam only on breaking crests, and horizon converging into the sky band.
