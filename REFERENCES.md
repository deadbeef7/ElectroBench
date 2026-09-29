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

## Notes on staging

- Reference images are informational. No image is downloaded at runtime; do not add an image fetch to the binary without explicitly wiring it through Convex/actions and the user's Keys/API keys.
- Preferred live references for the user's own tuning (not fetched by the app):
  - Real pool photos where a bright object (ball, teapot, fruit) is dropped into still water and the reflection reads as a darkened, broken-up mirror.
  - Dusk ocean photos with a defined sun disc, narrow glitter path, dark off-path water, foam only on breaking crests, and horizon converging into the sky band.
