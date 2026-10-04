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

## BUILD-P19: a realism pass on scenes 3 and 4, paid for out of the frame

The brief was "absolute realism" on both scenes, with the note that scene 4 runs
at 30 fps. The budget answer was not "spend the frame" — it was **find the work
the frame was already doing for nothing, and spend that instead.** Four changes
pay for most of the pass, and two of them are net *reductions* in per-fragment
cost that fix a correctness bug at the same time.

### What the measurements said before anything was changed

Baseline, 1280x720, t=7, both scenes, 4x MSAA:

| | scene 4 (corridor) | scene 3 (pool) |
|---|---|---|
| pixels above 0.93 luma | 0.95% | **13.94%** |
| largest solid blown-out mass | 0.33% (fill 0.41) | **2.40% (fill 0.97)** |
| local-std < 0.004 (no structure) | 40.4% | 34.9% |
| near-field surface local-std (median) | **0.0070** | 0.0044 |

0.0070 is about **1.8 levels out of 255**. That is the "washed out" and
"pixellated" complaint restated as a number: the carriageway was not dark and
not bright, it was *featureless*.

### Scene 4 — the two things that were actually wrong

- **A hard `min()` painted a flat plateau.** The specular clamp was
  `min(D*G*F/(4*NoV)*NoL, 3.0) * 1.35`, so every facet tight enough to exceed
  the cap returned *the same number*. 0.95% of the frame sat above 0.93 luma
  with a median of (1.00, 1.00, 0.88) — a solid 369x20 px mass of one value.
  A plateau is the loudest available "this is a renderer" cue, because a real
  highlight is a smooth shoulder. Replaced with a rational roll to the **same**
  asymptote (`1.35 * s / (1 + s * 0.247)`, limit 4.05): peak energy unchanged,
  plateau gone. Clipping above 0.97 fell **0.794% -> 0.559%**, above 0.99 fell
  **0.511% -> 0.245%**, and the blob's fill dropped 0.41 -> 0.39.
- **The road had no direction, and then it had no amplitude.** Every octave on
  the carriageway was isotropic, sampled on `P.xz` at a single frequency, so
  not one feature ran *along* the road. Added a 12:1 longitudinally-stretched
  octave (0.38 m across by 4.6 m down), transverse paving joints every 4.2 m,
  and a narrower burnished wheel-path core with a halo — because a real tyre
  track has a fairly defined edge where the water and the grime stop too.
  **Then the real finding: P15 had fixed *where* the detail switches off and
  never checked *how strong* it is while it is on.** The amplitudes already
  being computed were simply too small — the 0.77 m octave carried the entire
  texture budget of the road at +-15%, which is not a visible amount. Raised
  to +-31%, added a 25 cm mid octave, and raised the near-field grit. Carriageway
  local-std went **0.0070 -> 0.0099**, and the flat fraction of the bottom
  quarter of the frame went from ~55% to ~25%.
- **Water on a road does not arrive in isotropic blobs.** The camber sheds it
  off the crown, so it collects in the ruts *first*. The old weighting was
  0.85 broad-isotropic and 0.22 rut — very nearly backwards, which made the wet
  patches round stains shaped like the noise function. Swapped.

### Scene 3 — a missing line of maths, not a missing feature

- **The water's normal was never perturbed at all.** This was the largest defect
  in the room. Every optical term — the Cook-Torrance highlight, the mirror
  Fresnel, the reflected tile lookup — was evaluated against a perfectly flat
  (0,1,0). The swell and chop were computed and then used *only to scale
  roughness*, which cannot break a highlight up: it changes the width of one
  lobe, not where the lobe lands. So the fitting's reflection came out as a
  single smooth unbroken band. But the one thing every photograph of a lit pool
  has is a **glitter path** — thousands of separate specular hits, each a
  micro-facet tilted a different way. That is not a texture, it is the
  consequence of a normal that varies. Both fields are products of sines in
  closed form, so the surface **slope** is available in closed form too: four
  extra cosines, no new noise, no new loop, no new texture, faded out with
  distance (a 0.86 m wave is a few pixels at the camera and sub-pixel at the far
  shore, where the surface must integrate back to flat or it aliases).
  Structure in the frame nearly halved: **flat (<0.004) 34.9% -> 17.6%**,
  **flat (<0.015) 62.4% -> 42.6%**, water local-std **0.0044 -> 0.0081**.
- **The mirror had drifted out of register with the room.** `sky_frag.glsl`
  sampled `checker(plane + vec2(uTime * 0.006, 0.0))`; the water sampled
  `checker(plane)`. From the first second the tiles reflected in the water slid
  sideways relative to the tiles overhead, at a rate that grows without bound.
  The entire point of a pool room is that the reflection lines up with the room,
  and a slow drift is exactly the defect a viewer registers as "something is
  wrong" without being able to name it. Free fix.
- **The fitting was a solid white rectangle.** 2.40% of the frame as one
  255x89 px mass at 0.97 fill — a flat plateau with a hard edge. A real troffer
  has a bezel the diffuser is clipped into, a prismatic louvre across its face,
  and a diffuser that dims toward the frame because the acrylic is deeper there.
  Three cheap terms, applied to the sky *and* to the water's copy of the same
  panel so the reflection is the shape of the fitting.
- **The water grid was almost certainly paying for nothing — but the proof is
  blocked, and that is worth recording.** `kWaterResolution` was 220: 48,400
  vertices and 95,922 triangles every frame for a plane that is flat at y=0 and
  never displaced by anything. `object_vert.glsl` does `uModel * aPos` with an
  identity model and no displacement; every ripple, ring and reflection is
  computed analytically in the *fragment* shader from `vWorld`. Now 32 — 1,922
  triangles, a 50x cut in the vertex stage of the pass scene 3 was
  measurably slowest on.
  The obvious justification is that perspective-correct interpolation of a
  linearly-varying attribute across a planar primitive is exact whatever the
  primitive size, so the tessellation cannot change the image. **I could not
  verify that here, and the first attempt to test it produced a confident and
  wrong answer — see the reproducibility section immediately below.** The
  reduction is kept because it is a real, large saving on the slowest scene and
  the image quality measurements at 32 are the best this scene has measured;
  it is *not* kept on a claim of pixel equivalence, because that claim is not
  established.

### The measurement trap in this scene, and why several numbers above are qualified

**Scene 3 does not produce reproducible screenshots.** Two runs of the
*identical binary* at the identical `--shot-times 7`, at the identical width,
differ on **96% of pixels with a maximum channel difference of 248** — the same
magnitude as the difference between grid 32 and grid 220 that I first read as
a tessellation regression. (Scene 4, with the same renderer, reproduces
bit-for-bit, max channel difference 0 over 921,600 pixels.)

The cause is a half-finished fix from BUILD-D8. The screenshot gate was moved
onto the **simulation** clock so `--shot-times` means the same thing at any
frame rate, but the draw calls were left reading wall-clock `now` and feeding
it to the shaders as `uTime`. The dome's checker drift, its caustics, the
water's ripple phases and the hull-foam strength ramp are all therefore
functions of how fast the machine happened to be, not of what was simulated.
On a slow software rasteriser those runs take wildly different wall-clock
times, so the same flag gives visibly different rooms.

Consequence for this section, stated plainly rather than buried: **any scene-3
number resting on a single render against a single render is unreliable unless
it clears the run-to-run spread.** Two runs of one binary give this noise floor:

| metric | run A | run B | spread |
|---|---|---|---|
| flat (<0.004 local-std) | 15.95% | 14.82% | 1.13 pp |
| flat (<0.015 local-std) | 42.20% | 39.05% | 3.15 pp |
| pixels > 0.93 luma | 4.30% | 4.19% | 0.11 pp |
| horizon row (heuristic) | 387 | 31 | unusable |

Against that floor:
- The headline water result — **flat (<0.004) 34.9% -> 17.6%, a 17.3 pp move,
  about 15x the noise** — holds, and the (0.015) move, 19.8 pp against a 3.15
  pp floor, holds with it. The glitter-path fix is real.
- The clipping and glint figures for scene 3 are **inside the noise** and no
  claim rests on them. An earlier draft of this note read the fitting's gain
  reduction as "clipping did not improve"; that reading was noise, and the
  1.5 pp glint spread is larger than the 0.15 pp it was based on.
- The grid-equivalence test is **unresolved**, for the reason above: isolating
  it needs two renders of one build, and scene 3 cannot give two comparable
  ones.

The underlying fix is small and is the obvious next thing to do here — pass the
simulation clock to the draws instead of the wall clock, as BUILD-D8 already
did for the screenshot gate — but it is a harness change, not a realism one,
so it is not smuggled into a commit about what the rooms look like.

### The four things that paid for it

All four are bit-identical in output and strictly cheaper:

- `pow(1-VoH, 5)` and `pow(edge, 6)` in the object shader -> squared-cubed
  chains. Both ran on every fragment of the frame.
- `pow(toSun, 3)` in `hazeColFor` -> two multiplies.
- `pow(1.0 - u, 5.0)` in the water's `F_Schlick`, which is called **twice** per
  water fragment (GGX specular and mirror Fresnel) -> squared-cubed chain.
- **The mirrored-eye frame hoisted out of the 18-iteration hull loop.** The
  reflected ray, its elevation, a squared falloff and a normalised 2D
  direction were being rebuilt *inside* the loop, though every one depends only
  on `V` and `V` is fixed for the whole fragment. The driver is not obliged to
  hoist it: the loop body writes loop-carried accumulators (`hullFoam`, `bump`,
  `hullGhost`), which is exactly the case compilers give up on. Eighteen
  normalises, eighteen square roots and eighteen divisions per water fragment,
  every frame, for a constant.

Net: **strictly cheaper** in scene 3's vertex stage and its hottest fragment
loop, cheaper in scene 4's fragment stage, and the realism above was bought
with the change rather than against it.

### Verification, and what was NOT verified

- The P16 guards are the reason this pass is safe to ship. The material+distance
  tag pass is a pure silhouette measure, and it came back **byte-identical** to
  the P18 state: 456,478 object px, CABLE 39,142 px in 695 components (largest
  24,954), CONCRETE 5,681, STEEL 20,299, sky 50.47%. The detached-component
  check is likewise unchanged — 1,246 components, 3,180 px, the same 53 blobs
  over 8 px, all `CABLE` or `METAL`. So the wire web provably did not fuse, did
  not thin out, and did not gain a stray blob: the pass changed what surfaces
  look *like*, never which pixels they own.
- Frame diff against the P18 render: 29.27% of pixels changed, mean luma
  146.9 -> 146.8 (flat). It got slightly darker only where texture was added —
  pixels below luma 110 went 13.57% -> 14.16%, which is contrast arriving in the
  shadows, not the image going dark.
- **No FPS claim is made.** This machine renders on llvmpipe, a software
  rasteriser, and its frame rate is not a statement about the user's GPU. The
  cost work above is argued from the source — loop bounds, hoisted invariants,
  `pow`/loop/triangle counts — and the direction of every change is stated, but
  **the 30 fps figure itself has to be confirmed by the user.** If scene 4 has
  regressed, the two changes most likely to be responsible are the road's new
  25 cm mid octave and the longitudinally-stretched one; both are gated on
  `octaveRes(foot, freq)` and drop out entirely once a pixel cannot resolve
  them, so they cost nothing in the far half of the corridor.
- Every scene-4 figure in this section was taken against a **bit-reproducible**
  baseline: the same build re-rendered reproduces this scene exactly, so the
  deltas are measurements rather than samples.

## BUILD-P18: the shafts were too fat, and the web was below a pixel

Two complaints about scene 4 at once — "make the pole shafts not stand out" and
"make the wires more evident" — and neither of them turned out to be a shading
bug. Both were measurements.

Reference framing (informational — nothing is fetched at runtime; see the
no-runtime-fetch note under BUILD-P10):

- Japanese aerial cable, why the sky stays obscured, and why the wires are
  insulated rather than bare:
  - https://unseen-japan.com/whats-with-the-chaos-of-japanese-power-lines/
  - "Dense black cables are strung from utility pole to utility pole,
    crisscrossing the areas above streets and obscuring views of the sky."
- Pole shaft gauge — precast concrete distribution shafts run roughly
  190-260 mm across at the base and 120-170 mm at the top; a 6" galvanized HSS
  line is 152 mm at the base and ~110 mm at the top.

- **The shafts were a TRUNK, not a pole.** `TrunkRadius` ran 170 mm at the foot
  to 115 mm at the top: 340 mm across at the base, which is wider than the pole
  is tall in the frame's upper half and roughly 1.3x a real shaft at every
  height. It was also written out a second time, as a bare `0.17f, 0.115f`, in
  `AddPole`'s `AddCylinder` call — so the taper existed twice in the file and
  could drift apart, which is the same class of mistake as P8's duplicated
  stand-off offsets. Both numbers now live in `kShaftRBase`/`kShaftRTop` and
  the shaft mesh is built from them. Measured on the material tag pass at
  1280x720, t=7: **CONCRETE 7645 -> 5681 px (-25.7%)**, STEEL 22501 -> 20299 px
  (-9.8%; less than the shaft alone because STEEL also carries every crossarm,
  bracket, clamp and step bolt on both lines).
- **"Standing out" was not brightness — it was silhouette.** On the shaded frame
  the concrete shaft's median luma is 114.5 against a local sky of 186.4: it was
  already a *dark* vertical against a bright sky, so "darken it" was never the
  lever. What made it read as a hard-edged line was (a) the width, (b) a
  near-uniform value ramp that survives all the way to the horizon, and (c) the
  strongest rim term in the scene sitting on a vertical cylinder. So: the
  concrete staining amplitude roughly doubled and was skewed dark
  (`0.62 + 0.52*streak` against the old `0.84 + 0.28*streak`), and a second,
  much slower 0.35 c/m octave was added. The old streak is a 48 cm feature, so
  past ~60 m it falls under a pixel and every distant pole integrates back into
  one flat bright vertical; the new band is a 2.9 m feature and survives to the
  far pole. Steel lost its rim (sheen 0.42 -> 0.24) and its sun wrap
  (0.22 -> 0.13) — a wrap term lifts the *shadow* side of a cylinder toward the
  light, so on a backlit shaft it is a second edge brightening stacked on top of
  the rim, and together they meant line B was never allowed to go dark.
- **A warm-biased grey reads as timber.** `kConcreteGrey` was
  (0.300, 0.293, 0.278) — R above B. The dusk sun tint is (1.00, 0.66, 0.34) and
  the grade warms highlights, so that bias compounds and the corridor reads as
  a row of wooden sticks. It is now (0.212, 0.216, 0.218): 27% darker and
  neutral-to-cool, so the blue channel leads.
- **The wire web was never a density problem. It was a GAUGE problem.** Sixteen
  strands a bay is the densest cable run in the corridor and it had been there
  since P12 — but each strand was `0.010f + 0.007f * u3`, i.e. **10-17 mm
  radius**. At 1280x720 and a 52-degree FOV that is 0.26 px at 60 m and 0.79 px
  at 20 m: below the 4x-MSAA coverage floor everywhere except the nearest span,
  where the samples quantise to 0, 1/4, 1/2, 3/4, 1 and the wire spends its
  length flickering between a quarter-covered grey hair and nothing at all —
  which the aerial-perspective mix then lifts toward the haze. The density the
  scene was built for was in the geometry and absent from the image. Adding
  more strands would only have produced a mass of them. At 21-32 mm radius
  (42-64 mm cable, which is also the honest number for sixteen gathered
  cross-connects) the strands hold a core. Measured: **CABLE 30730 -> 39142 px
  (+27.4%)**, at a cost of 1.05 percentage points of frame (sky 51.05% ->
  50.47%). Samples went 7 -> 10 as well, for P13's reason: at 2x the gauge a
  1.77 m long segment would have started showing its joints. Every other gauge
  in the corridor moved with it (conductors 0.024-0.032 -> 0.028-0.037, telecom
  bundles 0.018-0.022 -> 0.022-0.027), because the conductors are what the eye
  scales the thinner tiers against; leaving them put the web back to looking
  like string.
- **P16's artefact is a sky-through test, and it was re-run, not assumed.** The
  guard was "does the web still let the sky through?", measured by labelling
  the sky and counting trapped holes: **1343 -> 1163 trapped sky components,
  145274 -> 146474 px of trapped sky (flat), largest single hole 17941 ->
  14512**. Total trapped sky unchanged is the number that matters — a fused
  wall would collapse it. The web is a slightly coarser mesh, not a wall.
- **Thinning the shaft detached nothing.** Every fitting already measures its
  stand-off off `TrunkRadius`, which is the reason P8 made it a function. The
  regression pass ran the P16/P17 detached-component check on both binaries:
  detached components >= 8 px **73 -> 53**, detached pixels **4367 -> 3180**,
  and every remaining blob is still `CABLE` — 1-px wire runs that drop out of
  the tag mask between two crossings, not floating geometry. No METAL, CERAMIC,
  STEEL or CONCRETE blob appeared. Step bolts dropped from 140 mm of stand-off
  to 100 mm, which was the one offset sized against the old shaft.
- **A cable is smooth black PE, not a matte hose** — roughness 0.42 -> 0.33 and
  a touch more sheen, so each span carries a warm filament down its sun-raked
  side. Deliberately modest: `rim` scales with `1 - NoV`, and on a 2 px strand
  nearly every fragment is an edge, so an aggressive sheen would lighten the
  whole cable and cost it exactly the silhouette contrast that made it read.

## BUILD-P17: the flying cylinders were a tube with no radius

- **The artefact was real, small, and in every frame — and it was NOT the
  lattice mast.** P14 concluded from a flood fill that "in scene 4 nothing is
  detached". That fill was seeded from the bottom of the frame and ran at
  1600x900; at the user's actual 1280x720 window, and with a detector that
  does not require open sky *underneath* a fragment, there are **3-9 detached
  objects per frame**, 3-5 px wide and 9-15 px tall, at 9-15 m. Every one of
  them was the same object: the service-drop termination fitting — a dark
  metal boot with a pale ceramic cap, drawn in `AddTelecomBundle`'s `else`
  branch, the case where a drop cannot reach a house anchor.
- **A threshold test cannot see this class, and neither can "is it attached".**
  These fittings sit only ~30-80 luma below the sky, so darkness masks miss
  them; and they hang in front of the *treeline*, so any test demanding sky
  beneath them misses them too. What identifies them is a material + distance
  tag pass: `G = vMat*15` (exact, since 17 materials x 15 == 255) and
  `B = distance in whole metres`. That is what separated a pole fitting at 10 m
  from a pylon insulator string at 140 m, which have the same silhouette.
- **The cause was a normalise function lying to its callers.**
  `Vec3Norm` returns `(0,1,0)` for a zero-length input rather than a zero
  vector. Every "find an axis at right angles to the wire" guard in the scene
  was written `if (Vec3Len(cross(wire, up)) < 1e-4f) use a fallback` — and that
  test **can never fire**, because the substitution hands back a vector of
  length 1. It is harmless for a horizontal wire, whose cross product is
  non-zero anyway. For a **vertical** wire `cross(wire, up)` *is* the zero
  vector, so `s2` came back as `(0,1,0)` — parallel to the wire — `u2` was
  built from `cross(s2, tang)` and came back `(0,1,0)` too, and every ring
  vertex was offset along the wire's own axis instead of radially. The tube was
  built, uploaded, and had **zero area**: 24 vertices, all on the axis, nothing
  rasterised. `AddCylinder` was untouched, so the fitting drew and the wire that
  holds it did not. A hanging cylinder, every frame, at corridor distance.
- **The wire was verified absent, not merely invisible.** It stays invisible at
  a 0.25 m radius — a 50 cm-thick bar is not something MSAA can hide — and a
  dump of every built vertex showed all 24 of the ring vertices at *identical*
  x and z, which is the signature of the collapse and not of a thin tube.
- **Applied: `Vec3PerpTo(v) = normalize(cross(v, ref))`**, with `ref` chosen by
  testing `|v.y|` so the result is never shorter than 0.1. One helper, used in
  `AddWire` and `WirePoint`.
- **The ORDER of that cross product is load-bearing, and the frame said so.**
  The first attempt used `cross(ref, v)`, which is the negation of the
  original, so `cross(side, dir)` flipped, `up` flipped, and `WirePoint`'s
  `-up*(sag)` turned **every catenary in the scene from a sag into a bow** —
  visible immediately as a 5.7% pixel change and fittings missing from a crop
  that should have held them. The corrected version changes 0.25% of the frame.
  A "safe-looking" vector helper is still a change of behaviour, and the
  render is the only thing that notices.
- **Measured, at the user's own 1280x720:** detached fittings at t=7 go from 9
  (351 px, none carrying cable) to 2, and both of those now carry `CABLE` pixels
  — they are joined to their drop wire, and remain separate components only
  because a 1-px wire drops out of the tag mask between the fitting and the
  pole. At t=32, 2 -> 1, likewise carrying cable. In the shaded frame the wire
  is now a continuous stroke running down into the top of every boot.

## BUILD-P16: the sky was yellow, and the tone map was why

- **Hue is a DISPLAYED quantity, and P15 solved it in linear space.** The dome
  is authored in linear HDR and displayed through ACES + gamma 1/2.2. P15's
  comment claimed the airlight had been moved "from 35 deg to 24 deg, i.e. from
  amber to a proper sunset orange" — and the shipped frame still measured
  **41.2 deg**. The reason is that hue had been computed on the LINEAR value.
  This dome is authored at hue 11 deg — deeply orange, ratio 1.00 : 0.20 :
  0.024 — and gamma lifts the near-black green and blue channels far more than
  the bright red, so it arrives on screen at 36 deg. **Gamma moves saturated
  reds toward yellow on the way to the monitor.** A linear fix that looks
  right on paper reads amber on screen, which is exactly how P15 shipped a sky
  that was still yellow.
- **Solve on the output, not the input.** `scripts/skytune.py` reimplements
  `atmosphere()` plus `encodeSky()` and reproduces the shipped frame's mean
  hue to within 2 deg (39.3 modelled vs 41.2 measured), which is close enough
  to tune on. It found that `kSunPath` is the whole story: at 9.8 the dome
  measured 39-50 deg at every elevation the camera frames, at 14.0 it drops
  to 20-30 deg, and by 18 it is a 14 deg salmon and getting darker every step.
- **Applied: `kSunPath` 9.8 -> 14.0 and `kRayGain` 4.35 -> 13.0**, in BOTH pole
  shaders (the object shader mirrors this dome in the wet road, so if the two
  drift the road reflects a different sky than the one it stands under). The
  gain rises with the depth because deepening the extinction darkens the dome;
  measured, mean displayed value 0.585 -> 0.555, peak channel 0.973.
- **Result: hue 41.2 -> 29.6 deg on the 1600x900 still, and 26.0-31.6 deg
  across the four loop frames**, deepening through the loop as the sun sinks.
  Mean green channel dropped 34 levels (174 -> 140) while saturation ROSE
  (0.789 -> 0.799) and clipping fell to 0.01-0.18% with zero black pixels. The
  frame's yellow was mostly green that the tone curve was resurrecting.

### The cylinders: one real defect found and fixed, NOT confirmed as the cause

- **Five pale smooth capsules per pylon, hung off the crossarms.** P15's
  `AddPylon` drew each insulator string as ONE smooth 2.29 m cylinder of
  `kCeramic` (albedo 0.78 — a near-white glaze) from a 0.26 m yoke: a capsule
  dangling from a bar, five per pylon and two pylons. That is the P14 artifact
  rebuilt on a new object, and it is now a disc stack on a dark core, 1.55 m,
  0.30 albedo, with the pole-top insulators keeping their glaze because they
  are 6 m from the lens. This is the P13 coil lesson again: a swept feature
  needs its own structure, not just a smaller gauge.
- **But projecting the strings to screen coordinates says they were too small
  to be the artifact.** At the 80-132 m these pylons actually sit at, a
  string is 5-11 px tall and 1-2 px wide, and sampling the render at the
  projected positions gives a minimum luma of 106-114 in BOTH the P15 and P16
  builds — i.e. the darkest pixel at every insulator location is the lattice
  steel around it, not the string. The pale insulators were measurably not
  what the eye was catching.
- **What IS ruled out**, by the threshold-free flat-sky flood fill run at all
  four shipped loop times (t = 2, 7, 12, 17): nothing detached is
  cylinder-shaped. Every unanchored component in every frame is a HUD glyph or
  a conductor leaving frame.
- **So this one is NOT closed.** The fix is real and an improvement, but the
  remaining "flying cylinders" are something this pass has not identified.

### Locating them: a material-tag pass, which needs no rebuild

- **`vMat` is a `flat` varying, and shaders are loaded at runtime, so tagging
  the object shader by material id gives a pixel-exact mask per object class
  with no C++ recompile.** Temporarily emitting one saturated colour per
  material and rendering t = 7 and t = 17 says exactly which class owns the
  pixels:
  - `kMatCable` (5): **20-27 compact vertical blobs per frame** — more than any
    other class. The worst is 11x19 px sitting **171 px above the horizon**.
  - `kMatCeramic` (6): 168-301 sky pixels and **zero** cylinder-shaped blobs,
    which independently confirms the insulator projection maths above.
  - `kMatSteel`, `kMatConcrete`, `kMatMetal`, walls: every tall blob is within
    1-5 px of the horizon, i.e. the pole lines themselves.
  So the cylinders are **cable**, and printing the magenta mask shows why: they
  are not isolated objects but a dense WEAVE — an 11x19 px block that is ~60%
  filled with crossing 1-2 px strands. Sixteen telecom drops per bay (P12)
  plus 4-6 drop wires per span plus the slack features all cross open sky
  together.
- **The likely root cause is that the slack features are drawn in the wrong
  plane.** `AddSlackCoil` and `AddSlackLoop` both sweep in the **z/y plane**
  (`a.z + radius * sin(ang)`), and the corridor camera looks *down* z. A circle
  in the z/y plane viewed along z projects to its narrow axis — so a loop
  built to read as a loop degenerates into a **vertical bar**. Rotating them
  into the x/y plane would present the loop face to the camera and turn the
  same pixels from an ambiguous tube into an unambiguous ring. This is the
  same shape-versus-gauge lesson as P13 and P14, one plane deeper.
- **IMPLEMENTED, AND THE VERIFICATION FAILED.** Both sweeps were rotated out of
  the view axis: `AddSlackLoop` now sweeps in x/y instead of z/y, and
  `AddSlackCoil`'s helix now winds along the corridor (circle in x/y) instead
  of circling horizontally around a vertical axis. Both are correct — a circle
  whose plane contains the view direction always projects to its narrow axis —
  but re-running the identical tag pass gives **`kMatCable` cylinder blobs
  20 -> 20 at t = 17 and 27 -> 27 at t = 7, with sky-pixel counts identical
  to within 1 px.** The change is real (it moved pixels) and it changed nothing
  the metric counts.
- **Why, and it is not a rounding error.** Projecting only the poles that
  actually spawn a slack feature — `(((int)(z*0.5))%3)==0` for the coil and
  `(((int)(z*0.7))%4)==1` for the loop — puts every slack object at t = 17 on
  screen at (421, 204) and in the band x 354-366, y 257-268. The measured
  blobs are at (566, 48), (563, 51), (606, 12), (472, 139), (497, 107). They
  do not overlap. An earlier attempt to check this compared against poles that
  have no slack feature at all and appeared to confirm the same conclusion; it
  only reached the right answer by accident, which is why the spawn conditions
  are now part of the check.
- **So the cylinders are the TELECOM WEB.** Three bundles per bay on line A
  (P9), each carrying 4-6 drop wires, plus cross-line spans, is 12-18 wires per
  bay crossing open sky. Near the camera that is a dense weave of 1-2 px
  strands — the 11x19 px blob at (566, 48) is ~60% filled. That is the
  "flying cylinders". P12 added the density on purpose, to make the street read
  as a Japanese pole street, and it is the density that has to give.
- **THE CYLINDERS WERE THE TELECOM WEB, and the fix is on the right target.**
  Repurposing `kMatGlow` as a temporary tag id inside `AddTelecomBundle` and
  re-rendering separates the telecom tier from every other `kMatCable` user:
  the class falls **10303 -> 5713 sky px** and its largest blob **71 -> 38 px**.
  Half of the artefact is telecom, not the power conductors.
- **Shipped: drops per bundle `4 + seed%3` -> `2 + seed%2`, and the third
  bundle on line A now runs on every OTHER bay.** Three bundles a bay at 4-6
  drops is 12-18 wires crossing open sky per bay; near the dolly a span
  subtends a large screen angle and the strands fuse into a parallel-stroke
  mass. Halving the drops lets sky through between them, and the even/odd
  third bundle breaks the tier's uniform three-deep wall — real pole lines are
  not uniform, some spans carry a third cable and some do not.
- **Measured on the two gates that matter** (`scripts/cablebars.py`, sky pixels
  and largest-blob area, NOT blob count):
  - t = 17: sky px **10303 -> 8869 (-14%)**, largest blob **71 -> 49 px (-31%)**
  - t = 7: sky px **9873 -> 9288 (-6%)**, largest blob 32 -> 32 px
  The blob COUNT rises 20 -> 22 at t = 17, which is the fragmentation artifact
  documented above and is not a regression.
- **The blob count is the wrong acceptance test.** It counts CONNECTED
  COMPONENTS, so thinning a dense connected mass fragments it into more,
  smaller pieces. A count that rises when the thing you are removing gets
  smaller is not measuring the target. Gate on density — `kMatCable` sky
  pixels and the largest blob's area — and treat the count as diagnostic only.

### The floating mast: a TV aerial parked on the wrong slope of the roof

- **The artefact had to be looked for at the resolution of the artefact.** The
  still is 1600x900 and the loop is 760x428; every earlier sweep that missed
  this one had been run on loop frames, where the mast is 2 px of mush instead
  of a 3 px shaft with two 22-25 px crossbars. Matching the committed still
  against the four committed loop frames (luma correlation 0.927 vs 0.45 for
  its neighbours) also pins the still to `t = 7`, which is what made an
  exact before/after pixel diff possible at all: same frame, so a changed-pixel
  count of 926 out of 1 440 000 is meaningful instead of noise.
- **A material-tag pass named it.** `kMatMetal` (7) is the only class with an
  isolated 2-3 x 54 px fragment in open sky at x1543-1560, y330-383 — a shaft
  with two crossbars. Tagging that class green and `kMatRoof` red in the
  object shader, then projecting `AddHouse`'s roof maths, put it on the
  `AddHouse` TV aerial: 1.35 m of mast, 0.028 -> 0.020 m taper, two 0.60 m
  bars — the only 2 x 54 px object in the scene with that profile.
- **The cause is that the aerial was parked by the HOUSE, not by the RIDGE.**
  `AddHouse` builds a roof whose ridge runs along **x** at `yRidge`, with two
  planes sloping away in **z** down to the eaves. The aerial was at
  `az = z - d * 0.22f`, which puts it a fifth of the way down the near slope,
  where the surface it stands on is already `rh * 0.22 * d / (d/2 + ov)` lower
  than the ridge. Measured on the tag render, that put its foot level with the
  top of the wall in front of it: the visible mast ended *on* the roof's
  silhouette line with no roof plane around it, which is the shape the eye
  reads as detached. The water tank, 1.5 m away on the same roof, did not have
  this problem because it parks at `z + d * 0.10f` with its legs based at
  `yRidge - 0.45f` — close in to the ridge and sunk well below the surface.
- **The fix is placement, not gauge.** The mast moved to `az = z - d * 0.06f`,
  the foot to `yRidge - 0.45f` — the same treatment the tank legs already get.
  Nothing about its size, its bars or the roof changed. Tag render, before and
  after, same frame:
  - before: `kMatMetal` at x1530-1560, y330-383; the roof's top edge is FLAT
    across x1536-1589 at y384 and the mast's last pixel sits exactly on that
    line, with open sky immediately to the right of the foot at x1552-1553.
  - after: `kMatMetal` at x1487-1515, y343-385; the roof edge now descends to
    the left of the foot (y386 from x1499, y387 from x1481), so the mast's base
    runs 8 px DOWN INTO the roof mass instead of stopping on its outline, and
    `kMatRoof` is continuous on both sides of and directly beneath the foot.
- **The lesson is the one P8 already paid for.** A leg based at `yRidge` hovered
  in the air; the tank legs were fixed by sinking them to `yRidge - 0.45`. An
  aerial is a leg with a crossbar on it, and it had been given the ridge
  coordinate without the ridge's sink. Anything mounted on a pitched surface has
  to be measured off that surface, and on this roof "that surface" is a
  function of `z`, not a constant.

## BUILD-P15: three complaints, one pass

### "There are still some cylinders in the air"

- **When three consecutive passes fix the same complaint without changing
  the read, the SHAPE is wrong, not its gauge.** P13 thickened the slack
  coil. P14 thickened the cell mast's legs, diagonals and belts and added
  panel antennas. Both were correct about the geometry. Neither changed what
  the eye could assemble, and that is the signal to stop adjusting and
  change the object.
- **P14's own fix created the artifact it was fixing.** The panel antennas it
  added were 0.42 m wide and TEN METRES tall, hung 0.46 m off a truss whose
  0.11 m stand-offs are sub-pixel at the 120-160 m these masts actually sit
  at. A debug render (flat sky + magenta radomes) put four pale 1-2 px bars
  per mast on screen with visible sky between them and the structure holding
  them. That IS "cylinders in the air", verbatim — P14 shipped it.
- **Placement has to be tuned to the shot window, not the whole loop.** The
  dolly runs z = 4..165 m, but the still and the loop are shot at t = 2..17
  s, i.e. z = 9..48 m. The pylons were originally placed for the loop's
  worst case and consequently sat 190-230 m away through the entire shot
  window, where a 44 px crossarm is washed almost into the haze. Re-placed
  against the shot window, the same objects are 103-173 m out: 46-62 px
  crossarms, 100-133 px towers.
- **Silhouette class beats member thickness.** The replacement is a lattice
  TRANSMISSION PYLON, not a re-gauged cell mast, and the reason is entirely
  about what the shape is made of:
  - a SPLAYED A-FRAME base (9 m of feet on a 26 m tower) tapers to 6 px at
    the shoulder — an unmistakable triangle;
  - TWO LONG HORIZONTAL CROSSARMS. 18 m of horizontal at 100 m is ~60 px
    across the frame. A horizontal bar is the one shape a vertical tube can
    never be confused with;
  - insulator strings on visible yokes at the boom tips;
  - the pale radomes and the whip cluster are simply gone.
  Measured on the new render, each crossarm darkens the sky by 55-87 luma
  levels along its whole length (it was ~30 before, i.e. a smudge).
- **Lattice steelwork at dusk is DARK.** The first pass used the same 0.30
  albedo as the poles; behind the aerial-perspective ramp that arrives at the
  eye only a few levels below the sky, and the whole tower read as a faint
  smudge. Dropped to 0.17, the crossarm is an actual bar.
- It is also the more truthful object: a lattice transmission pylon behind a
  Japanese distribution line is a far more characteristic sight than a cell
  mast, and it carries the scale P12 wanted without needing to be legible
  member by member.
- **A detector that cannot see the old artifact cannot certify the new one.**
  Every threshold-based sweep run over the new frames reported zero floating
  bars — and then reported zero over the OLD frames too, which visibly had
  them. The old panels sat only ~30 luma below the sky, so any mask with an
  absolute darkness floor is blind to exactly the artifact under investigation.
  What settles it is P14's threshold-free method: force the sky shader to a
  constant, treat every non-constant pixel as geometry, and flood-fill that
  mask from the bottom edge. Measured at t = 2 s and t = 17 s (the two ends of
  the shipped loop) at 760x428: geometry 167,655 / 158,615 px, detached
  4,275 / 3,466 px = 2.6% / 2.2%, and **every** detached component is either a
  HUD glyph (identical 13-15 px cells on an exact 16 px pitch at y 16..30) or
  a conductor leaving frame (h/w 0.29-0.56 — wide and flat). The tallest
  detached thing in either frame has h/w 1.71 and is clipped by the top edge.
  Not one detached component is cylinder-shaped.

### "Make the sky orange"

- **The frame was at hue 49.9 deg — yellow-amber, not orange.** Orange lives
  near 30 deg, and the gap is almost entirely the GREEN channel: the airlight
  band was (0.40, 0.14, 0.036), whose green is 35% of its red, and green that
  strong is exactly what the eye reads as "yellow". Measured on the shot
  window after the change: **hue 49.9 -> 41.6 deg, saturation 0.749 -> 0.768,
  mean green channel 203.8 -> 176.0.**
- **Mie is the only grey in the model, so it is the cheapest saturation
  lever.** Cutting kBetaM 0.0125 -> 0.0092 (26%) moves the frame's saturation
  without touching its brightness — the one change that cannot wash the sky
  out, which is the failure mode of every other "make it hotter" attempt.
- **The sun-path optical depth is the reddest knob.** kSunPath 8.0 -> 9.8
  decides how much blue is gone by the time the light reaches the scene, and
  deepening it keeps the red while burning the blue: orange, not yellow.
- **Multiple-scattering blue was creeping into the visible band.** Its ramp
  now starts at 0.78 (48 deg) instead of 0.55, so the frame the camera
  actually sees carries none of the blue floor while the zenith still goes
  blue if the user ever looks straight up.
- Clouds were the largest non-sky area in a dusk frame, so their colour sets
  the frame's white balance as surely as the dome does: crown
  (1.30, 0.96, 0.62) -> (1.36, 0.845, 0.475), belly pulled to a red-shifted
  shadow, and the sun disc itself made orange so the brightest point in the
  frame agrees with the band behind it.

### "The scene looks pixellated without detail"

- **The detail was not missing — it was being switched off.** Every fine
  octave in the object shader was gated on `near = 1 - smoothstep(16, 64,
  dist)`, pure DISTANCE, so the entire far half of the corridor — road,
  verges, house walls, the next pole down — rendered as a flat wash of
  unmodulated albedo. That is a gate, not a shortage of detail.
- **Distance is the wrong test.** Whether an octave is usable depends on
  whether ONE PIXEL can resolve it, and the driver already knows that
  exactly: `fwidth()` gives the fragment's world-space footprint. A 20 m
  ground feature is still resolvable at 250 m; a 2 cm aggregate speckle is not
  resolvable at 8 m. So each octave is now gated by
  `octaveRes(foot, freq) = 1 - smoothstep(0.35, 1.10, footprint * freq)`,
  which keeps it alive exactly as long as the pixel it lands in can resolve
  it and kills it one octave before it would alias into shimmer. One
  footprint serves every octave in the shader, so it is also cheaper.
- **A surface with no feature size has no scale.** A new 7 m ground mottling
  octave sits UNDER the grit: the verges are the largest flat area in the
  frame and were reading as paper, and a feature that survives from the camera
  to the horizon is what gives the plane a size.
- Zinc spangle, concrete spalling and timber grain are 3-20 cm features that
  the old distance gate switched off at 64 m, which is why every shaft past
  the second pole read as a bare tube. All three now run on resolution.
- 8-bit banding on the wide, smooth haze ramp was the other half of
  "pixellated": a two-tap triangular dither at 3 levels instead of a single
  1.2-level one.

### Scene 3 splashes: the timeline, not the shape

The splash shaders were already physically careful — GGX glints, Fresnel
film, Beer-Lambert thickness, crawling tear fields. What was wrong was
**when** things happened, and it is the same class of error as a crown drawn
pre-formed:

- **The crown was already finished at t = 0.** `height` and `spike` were both
  written once at spawn, so the first frame a splash existed it was a
  fully-formed, fully-torn star at full height. High-speed footage of a real
  Worthington crown says otherwise: the rim at t=0 is a SMOOTH, almost
  circular collar thrown clear of the displaced volume; it rises as a clean
  cylinder for 60-100 ms; the fingers only appear when the cavity underneath
  pinches off and the sheet loses its support, 150-250 ms in. The crown now
  runs that four-stage timeline, and the tearing amplitude is weighted by
  HEIGHT ON THE SHEET so the base stays a solid collar while the free lip
  tears first.
- **Traced and confirmed** (pot 5, instrumented run):
  `t=0.008 h=0.09 spike=0.00 droplets live 0 / staged 159` ->
  `t=0.10 h=3.60 spike=0.26 live 0` -> `t=0.30 h=3.60 spike=1.00 live 74 /
  staged 85` -> `t=0.51 h=3.19 live 159 / staged 0` -> `t=1.11 h=2.00`.
  The live droplet count climbing 0 -> 10 -> 74 -> 148 -> 159 is the staged
  release working.
- **The droplets were 7-16 cm ACROSS.** Radii of 36-80 mm are not spray, they
  are hailstones: a 16 cm ball of water hanging over a 1 m crown reads as a
  balloon, and there were only 46-66 of them, so the eye counted every one.
  Real Worthington ejecta is 4-25 mm and there are hundreds of it, distributed
  as a power law rather than uniformly. Now 5-26 mm on a `u^3` draw (a haze
  of fine mist plus a few fat beads near the axis), roughly twice the count,
  with size correlated with launch height so the corona is graded rather than
  uniform.
- **A gate inherited from the old sizes would have silently switched off two
  whole effects.** The landing code gated its micro-ring AND its secondary
  ejecta on `radius > 0.03f` — a threshold sized for the old 36 mm drops.
  With 5-26 mm ejecta that test can never pass, so every splash-back droplet
  the scene is known for would have vanished with no error anywhere. The
  threshold is now stated against the new distribution (0.009 m) rather than
  inherited from the old one.
- **Ejection is staggered, and a staged strand is not drawn.** The lip thins
  and pinches first, so the highest strands leave first and the base tears
  last; release time tracks height on the sheet over ~0.3 s. Submitting a
  strand still inside its staging delay would stamp a frozen blob at the
  release point — the same "object hanging in the air" class of bug, in a
  different uniform.
- **A water drop is a lens, not a cotton ball.** The droplet fragment was one
  soft gaussian, so every strand read as the same fuzzy smudge at every size.
  It now has a dark limb where the surface curves away and total internal
  reflection sends the ray back down, plus an off-centre caustic hotspot —
  the two features that make a few hundred small sprites read as water.

## BUILD-P14: the hanging cylinders were a lattice mast, drawn too thin

- **"Cylinders in the sky" did not mean a floating object — it meant a real
  object drawn too thinly to read as a structure.** A flood fill of a flat-sky
  debug render, seeded from the bottom of the frame, settles it: in scene 4
  *nothing* is detached. Every silhouette either reaches the ground or runs off
  the top edge of the frame. The unanchored set is HUD glyphs and the wire web
  leaving frame. So the artefact had to be found by looking at what was drawn,
  not at what was disconnected.
- **The cell mast presented as six vertical tubes.** At 50 m the truss braced
  with 26 mm members — about 0.6 px — and after 4x MSAA they simply vanished.
  What survived was the four 85 mm legs plus the whip strokes: six verticals,
  tapering, against open sky. The legs were *wider on screen than the members
  that make a mast a mast*, so the only silhouette the eye could assemble was a
  row of cylinders. The tell was visible in the raw pixel data: at y 160-179 the
  mast was six separate strokes at x 641, 653, 657, 664, 668, 672.
- **Gauging and spacing pull against each other.** Members now clear a pixel
  (legs 125 mm, diagonals 70 mm, belts 58 mm) but the bracing still steps
  every 1.9 m, which at corridor distance is ~12 px between levels. An
  intermediate build stepped 1.15 m and welded the truss into a solid tapering
  wedge — same silhouette class, worse story. Both matter: thickening alone is
  not the fix.
- **Panel antennas earn their keep.** Four vertical radomes on stand-offs in
  the upper third give a cell mast its unmistakable read, and stop the
  silhouette being four sticks with a fork on top. The stand-offs matter as
  much as the panels: a panel with nothing tying it back to the truss is a slab
  floating beside the mast, which is the same class of bug.
- **The masts were also in the wrong place.** The dolly runs z = 4..121 m at
  x = +2, so a mast at (17.5, 101) passed within 15 m of the lens — a 24 m
  truss filling the frame with legs smeared off the edge. Both now sit ahead of
  the dolly's whole range at the same screen bearing they had before
  (|dx| ~ 0.78 * dz), so framing is preserved while the nearest approach
  relaxes from 15 m and 34 m to 52 m and 72 m.

## BUILD-P13: the coil was the hanging cylinders

- **A helix built from coarse segments IS a row of hanging cylinders.** The
  slack coil from build P12 was swept at five samples per turn with a 17 mm
  cable radius: every segment was a 38 cm long, 3.4 cm thick stubby tube with a
  visible joint to the next one, and six of them hung off the side of the pole
  in a row. On screen that is exactly "4-6 hanging cylinders in the sky" — the
  detail meant to sell the pole was the artifact. Twelve samples a turn and a
  11.5 mm cable puts every segment under the size at which the beading is
  legible, and the silhouette is unchanged. The lesson generalises: any swept
  feature has a sample density requirement, and "enough to look smooth" has to
  be measured against the on-screen size of one segment, not chosen by eye.
- **The sky went decisively amber.** Mean saturation 0.367 -> 0.439; the upper
  frame is now (0.89, 0.78, 0.22). The Rayleigh single-scatter gain came up, the
  horizon airlight band got hotter, the cloud crown and belly were warmed so the
  clouds stop greying the sky out, and the blue-at-altitude ramp now starts at
  31 degrees instead of 26 — above the frame the camera actually sees, so the
  visible sky stays orange all the way up while the zenith still goes blue.

## BUILD-P12: matching a reference photograph (scene 4)

The reference is a street-level worm's-eye shot of a Japanese utility pole. What
was taken from it — and what was deliberately left alone:

TAKEN:
- **The shaft is CONCRETE, not timber and not steel.** Build P9 rebuilt the poles
  as galvanized steel; the reference is unmistakably a weathered precast concrete
  shaft. Line A is now concrete and line B stays steel, which is what a real
  street has and what stops fifteen identical shafts reading as one repeated
  prop. Concrete only reads as concrete if it is DIRECTIONAL: vertical water
  staining running down from every bracket, horizontal form-board lifts from
  the mould, spalled patches showing darker aggregate, and a grime line washed
  up the first two metres.
- **The web, not the conductors.** Six neat distribution conductors read as a
  power line DIAGRAM. What fills the corners of the reference frame is sixteen
  thin black telecom drops and cross-connects strung between the same two poles
  at different heights, slack, and not in a plane. Sixteen per bay, deterministic
  so the benchmark stays comparable, each one tube.
- **Slack cable.** The flat wound helix hung off a bracket, and the big circular
  bight at a drop point, are the two most recognisable objects on a real pole and
  cost nothing per pixel. Both are terminated — a coil whose cable simply stops is
  the same "floating cylinder" mistake as the unterminated stub from build P10.
- **The aim point moved UP.** Aiming at 5.5 m framed the least interesting three
  metres of the shaft. The subject of the shot is the top of the pole.
- **Something tall and thin in the far distance.** Two lattice cell masts. A
  street with no vertical beyond the pole line has no scale.

LEFT ALONE: the sky. The reference is a clear blue afternoon; this scene's amber
dusk atmosphere was established in build P10 and the user asked for the wires and
the upper pole, explicitly not the sky.

## BUILD-P11: scene 1 gets a place to stand in, and a camera that moves

Realism takeaways applied — the failures here were *structural*, not shading:

- **A void is not an environment.** Scene 1 cleared to a flat colour and that was
  the entire world: no horizon, no depth cue, no light in the sky for the
  shadows to come from. It now carries a small equirectangular sky GENERATED ON
  THE CPU (128x64, no file to ship, nothing to download) — vertical gradient,
  horizon haze, and a sun glow placed at the scene's own fixed sun vector, so
  the light in the sky and the shadows on the floor finally agree. It is drawn
  as an inverted sphere around the current eye position with no program bound:
  fixed-function textured geometry, so the scene still costs what it cost.
- **The far floor and the sky have to be the same colour.** The floor's aerial
  perspective faded to `uSkyColor` while the dome faded somewhere else, and the
  join was a visible band. Both now start at exactly (0.86, 0.80, 0.70).
- **Fog that finishes too early is worse than no fog.** `smoothstep(9, 22)` had
  every pixel past 22 m at the fog colour, which turned the far two-thirds of
  every frame into one flat wash and removed every depth cue in the shot. It is
  now exponential out to the horizon, and it brightens toward the sun because
  backlit air forward-scatters.
- **A floor is the largest surface in almost every benchmark frame, and a flat
  one reads as a backdrop.** Power-trowelled concrete now has burnish sweeps in
  long arcs (a uniform noise has no feature size and reads as film grain),
  exposed aggregate in the near field only (left running, it aliases into a
  shimmering band at distance), saw-cut control joints with a chamfered arris,
  a real sun specular lobe and a grazing sheen. Measured on the near floor, the
  standard deviation went from a near-flat sheet to 19/255 with a gradient RMS
  of 6.6 — i.e. the surface finally has texture and therefore has scale.
- **A GL 2.1 shader cannot invent a camera.** There is no built-in eye position
  in GLSL 1.2, so the view vector and the distance term were unavailable to the
  floor until `uEyePos` was added and lifted straight out of the modelview
  matrix's translation column. Specular, sheen and aerial perspective all need it.
- **The flyover has to keep the horizon in frame.** With a 50-degree vertical
  FOV, any camera elevation above ~25 degrees pushes the horizon off the bottom
  edge, and the shot becomes floor-to-the-edges with no sky in it at all. The
  first pass of the flyover peaked at 44 degrees and every one of its three acts
  was a picture of a floor.
- **A texture unit is global state, not a sampler-local one.** Binding the sky
  dome to unit 0 in the render loop left the gun shader sampling the sky as its
  base colour every frame, because a GLSL sampler remembers its unit even after
  the active unit moves on. The dome lives on unit 7 now.

## BUILD-P10: photorealism pass (scenes 3 and 4)

References consulted (informational — nothing is fetched at runtime):

- Physically based skies, single-scattering analytic model, Rayleigh + Mie
  (Henyey-Greenstein), air-mass approximation `1/(h + k)`:
  - https://en.wikipedia.org/wiki/Rayleigh_scattering
  - https://en.wikipedia.org/wiki/Mie_scattering
  - https://en.wikipedia.org/wiki/Henyey%E2%80%93Greenstein_phase_function
- Water caustics — GPU Gems Ch. 2, "Rendering Water Caustics" (aesthetics-driven
  real-time caustics) and Martin Renou, "Real-time rendering of water caustics":
  - https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-2-rendering-water-caustics
  - https://medium.com/@martinRenou/real-time-rendering-of-water-caustics-59cda1d74aa
- Cook-Torrance microfacet terms (GGX NDF, Smith visibility, Schlick Fresnel):
  - https://en.wikipedia.org/wiki/Cook%E2%80%93Torrance_shading_model
  - https://disneyanimation.com/publications/physically-based-shading-at-disney/
- ACES filmic tone mapping (Narkowicz fit) and the split-tone film grade:
  - https://en.wikipedia.org/wiki/Academy_Color_Encoding_System
- Poleline hardware still referenced for the pole-top fittings: CPUC *Construction
  Requirements for Pole Line Guys* (two insulators in the overhead guy, one in the
  anchor guy) and JEA *Guys and Anchors* (3/8" and 7/16" galvanized guy strand).

Realism takeaways applied — **measurement over eyeballing**:

- **Nothing beats coverage antialiasing for a wire tangle.** The frame is 3-4 cm
  cylinders crossing a bright sky; without MSAA every conductor is a hard
  stair-step and the corridor reads as vector art. 4x MSAA costs nothing per
  fragment on any driver from the GMA 950 up because it runs at sample rate, not
  pixel rate, and it is the single largest step toward "photograph" in this pass.
  It is requested with a **fallback**: a driver that refuses the multisample
  attribute gets its window rebuilt without it rather than dropping the scene,
  and the mode in force is printed at startup so it is never a guess.
- **A sunset sky is an atmosphere, not a colour ramp.** The old dome was three
  stops with an fbm smeared over it. It is now single-scattering Rayleigh + Mie
  with the sun's own extinction along its own path — so the sky reddens BY
  ITSELF as `uSunDir` sinks, instead of a keyframed palette — plus a small
  multiple-scattering floor, without which a one-scatter model predicts a
  monochrome red sky with no blue in it anywhere. Measured: clipped pixels
  (>0.97 luma) fell from **4.20% to 0.59%** at an unchanged saturation, i.e. the
  whole dynamic range moved under the shoulder instead of onto a clip wall.
- **Aureole is a knife, not a blanket.** Mie is grey and desaturating, so it has
  to be small: the first pass used a realistic-looking aerosol coefficient with
  a 1.25 gain and the entire dome came back cream. Real clean-air sunset skies
  keep the aureole inside ~10 degrees and the rest of the sky saturated.
- **Horizon airlight is the one term a one-scatter model cannot supply.** The
  single-scatter integral has to FADE toward the horizon (the view extinction
  kills it) but a real sunset horizon is the BRIGHTEST part of the sky, because
  that light has bounced until it is isotropic. It is also the surface the whole
  corridor silhouettes against, so its level and colour are the two numbers the
  scene reads from.
- **A wet road is high contrast, not uniformly shiny.** Fresnel runs to ~0.6 at
  this camera's grazing angle, so a broad wetness term turned the entire
  carriageway into one sheet of reflected horizon and the tarmac disappeared —
  the road reflection now samples the SAME analytic sky the sky pass uses, but
  only in patches, so bright mirror pools sit next to dry textured stone.
- **A road that meets the ground in a straight line is a giveaway.** Cast kerbs
  (0.32 m wide, 16 cm proud, their own material with exposed aggregate and
  standing water in the gutter) give the corridor something to catch light and
  something to cast a shadow.
- **Specular had to become a real microfacet lobe.** Blinn-Phong with an exponent
  mapped from roughness cannot make a highlight that is both tight and
  energy-sane, which is why neither the damp road nor the galvanised steel ever
  produced a convincing glint. D·G·F costs ~20 flops, which is affordable here.
- **Grade it like film.** A physically shaded frame still looks rendered until a
  split tone is applied — cool shadows, warm highlights, and a small saturation
  lift, because per-channel atmosphere maths pulls colour out of everything.
- **A light source that is invisible is not a light source.** The pool room was
  lit by an unexplained asymmetry: a gradient brighter in one direction with no
  fitting anywhere to justify it. There is now a luminous ceiling panel with a
  mullion cross, and its reflection lies stretched down the water — the single
  most photographic thing the room can contain. The light was also dropped from
  53 degrees to 31 degrees elevation, because the panel's reflection only lands
  inside the frame if the source does.
- **Caustics have to be thresholded, not powered.** `pow(q1*q2, 6)` over two
  half-sine waves is ~2e-4 almost everywhere: only the exact peaks survive and
  the room gets no light at all. A smoothstep on the product is what produces
  filaments. And on a dome the unroll must be in the room's own ANGULAR grid —
  a world-space projection compresses to sub-pixel slivers at grazing incidence
  and produces no filaments at all.
- **Anything mounted on a leaning surface must be measured off that surface.**
  The junction-can enclosure was still pinned to the pole's GROUND position
  (base.x/base.z) with a flat 0.20 m standoff, so on every leaning pole it slid
  up to 8 cm off the side of the shaft it is bolted to — with its lid seam and
  label block floating with it. It now hangs off two flat straps from the leaned
  axis at the height it is mounted, measured against TrunkRadius.
- Reference images are informational. No image is downloaded at runtime; do not add an image fetch to the binary without explicitly wiring it through Convex/actions and the user's Keys/API keys.
- Preferred live references for the user's own tuning (not fetched by the app):
  - Real pool photos where a bright object (ball, teapot, fruit) is dropped into still water and the reflection reads as a darkened, broken-up mirror.
  - Dusk ocean photos with a defined sun disc, narrow glitter path, dark off-path water, foam only on breaking crests, and horizon converging into the sky band.
