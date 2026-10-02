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

## LainBench (scene 4) — Lain-style utility corridor

Realism takeaways applied:

- Utility-pole wire sag is a catenary, not a straight line: every conductor hangs on a parabola
  (with cosh tails at the ends), and each insulator point on a crossarm carries its own curve, so
  the wire wall reads as three-dimensional instead of a flat harp.
- Power lines are seen against the SKY: poles and wires must silhouette against a bright dusk
  gradient, with aerial haze progressively swallowing the far spans into the horizon colour.
- A low amber sun, a drifting white cloud deck and warm gravel bounce give the scene one coherent
  light story, and a filmic knee keeps the bright sky rolling off instead of clipping flat.

## LainBench (scene 4) — BUILD-P7 realism pass

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
- **A catenary parameterised from its midpoint has to be BASED at the midpoint.** The classic form
  `a + dir*((t-0.5)*len) - up*(sag*(1-4(t-0.5)^2))` looks right and is not: `x` runs from
  -len/2 to +len/2, so adding it to `a` (the span START) draws every wire half a span too early,
  ending in mid air between poles. Every wire in scene 4 was wrong this way for its whole life, and
  no amount of correct insulator-top attachment could hide it, because the attachment points were
  never where the wire began and ended. The only reason it survived inspection is that it is
  invisible to the eye at a glance and obvious to a distance check: `|WirePoint(t=0) - a|`.
- Anything mounted on a curved surface (a spool on a tapering trunk, a bracket on a leaning pole)
  must be measured off that surface at the height it is mounted, not offset by a constant — the
  taper is what made a fixed 0.24 m offset float 0.11 m clear of the bark.
- Anything added inside a solid is invisible, not wrong: an emissive panel at 0.33 m inside a
  0.42 m-deep cabinet renders nothing at all, and reads as "the feature is missing" rather than
  "the feature is misplaced". Place new detail on the OUTSIDE face of its housing.
- A generic attachment audit is worth more than eyeballing: register every support point, record
  every wire endpoint, and report the distance from each end to the nearest support. An end that
  matches nothing IS a floating wire. Tagging the endpoints by call site turns the list into a
  to-do list in one run.

## LainBench (scene 4) — BUILD-P9: steel poles, denser wiring, no birds

Realism takeaways applied:

- **Delete detail that cannot survive its own scale.** Fifty-one perched birds, each a hand's-width
  blob crossing a bright sky, were the loudest non-photographic tell in the frame — and the eye does
  not read them as birds at 30 m, it reads them as dirt on the lens. Sub-metre dressing is only worth
  geometry when the camera is close enough for the silhouette to be legible; otherwise the same
  polygon budget buys a wire span that crosses the whole frame.
- **A material identity is worth more than surface detail.** The shafts were rebuilt as hot-dip
  galvanized steel, and the first attempt reused the existing metal branch — and looked identical to
  the timber it replaced. Zinc is a bright, semi-specular, vertically weathered surface; running it
  through a generic metal term gave the same dark post back. A pole only reads as steel if the shader
  knows what zinc does: crystalline spangle, chalky bloom, rain-washed streaks, rust creeping out of
  the base plate. Measured, the neutral (unsaturated) share of the upper frame went from 0.12% to
  3.4% once the branch existed — that number, not the geometry, is what "metallic" means on screen.
- **A polished cylinder needs a wrapped diffuse term.** Backlit, a Lambert shaft facing away from the
  sun collapses to a flat cut-out; letting the sun wrap 0.22 around it keeps the roundness readable
  exactly where the corridor silhouettes hardest.
- **Dead hardware is worse than no hardware.** Build P8 built a lower crossarm, hung braces on it and
  ran no conductors along it at all. The cheapest realism win in the whole scene was giving that tier
  two insulators and two spans — the pole now reads as carrying a load instead of displaying fittings.
- **Attachments must be measured off the surface they touch, every time.** The P8 rule ("measure off
  TrunkRadius, not a fixed offset") had to be reapplied to a dozen new parts: the step bolts start 2 cm
  inside the shaft, the number plate sits ON its band strap, the ID legend block sits ON the plate,
  the earth-strand clips run from the shaft centre out to the strand, and the cut-out fuses hang off
  a bracket that starts inside the arm web. Each of those gaps is under a centimetre, which is
  precisely the scale at which "attached" stops being true.
- **Keep the hardware clear of the arm it hangs from.** Moving the transformer can out of the arm's
  own z plane (it used to run straight through the telecom arm below it) and putting the cut-outs on
  the arm FRONT (z + 0.17, clear of the knee braces at z + 0.05) is what makes the top of the pole
  read as a dense assembly rather than a few parts intersecting.
- **Wire count is a cheap silhouette lever.** Six tiers per bay instead of four, and a third telecom
  bundle on the middle bracket (which had been carrying nothing), deepened the tangle without a single
  extra triangle of cost per pixel.
- **Lit windows are the suburban cue.** A third of the street-facing panes are emissive at dusk, with
  glazing bars over them. Emissive geometry is only believable if the frame behind it still exists —
  the bars and the sill are what stop a lit pane reading as a sticker.

- Reference images are informational. No image is downloaded at runtime; do not add an image fetch to the binary without explicitly wiring it through Convex/actions and the user's Keys/API keys.
- Preferred live references for the user's own tuning (not fetched by the app):
  - Real pool photos where a bright object (ball, teapot, fruit) is dropped into still water and the reflection reads as a darkened, broken-up mirror.
  - Dusk ocean photos with a defined sun disc, narrow glitter path, dark off-path water, foam only on breaking crests, and horizon converging into the sky band.
