# GUI artwork brief (evaluation slice)

Artwork is optional and replaceable. Filenames are the contract; the layout,
hotspot bounds and text are owned by the RML/RCSS and the application, never
baked into images. Do not paint labels, hunter data, selection highlights or
buttons into artwork. Do not use original Carnivores 2 assets, HUNTDAT
content or third-party mod art.

Both files live under `Frontend/gui/assets/images/` in the checkout and under
`share/c2-frontend-gui/images/` in a staged install. The tiny placeholders
checked in today are generated gradients so loading, fitting, alpha and
installation are exercised; replace them in place.

## Slot 1: `lodge_background.png` (lodge stage)

| Property | Value |
| --- | --- |
| Aspect | exactly 16:9 |
| Recommended pixels | 2560 x 1440 (minimum 1920 x 1080) |
| Format | PNG, RGB or RGBA (alpha is composited over `#1c2320`) |
| Fit | `contain` inside the 16:9 stage; the stage is letterboxed/pillarboxed inside the window with a near-black fill (`#0b0d0f`) |
| Decoder | stb_image, PNG only; no color management, sRGB assumed |

The stage is a pre-rendered lodge/museum interior in the atmosphere of the
original game (warm incandescent lamps, dark timber, trophy wall, a map table,
dusk through a window), not a glossy redesign and not a walkable 3D scene.
Keep lighting direction consistent across future expedition art: key light
from the upper left, warm; fill cool blue-green from the window.

### Normalized destination bounds (fractions of stage width/height)

Hotspots are real focusable elements positioned in stage percentages. Compose
the art so each region reads as a natural place to interact (a door, a desk,
a console) and keep the interiors of these boxes free of fine detail and
text; the application draws a translucent panel and a label over them.

| Element id | Role | left | top | width | height |
| --- | --- | --- | --- | --- | --- |
| `dest-expeditions` | Expeditions (Expedition Console) | 0.08 | 0.52 | 0.24 | 0.18 |
| `dest-profile` | Profile / Setup | 0.38 | 0.52 | 0.24 | 0.18 |
| `dest-exit` | Exit | 0.68 | 0.52 | 0.24 | 0.18 |
| (unavailable) | Trophies, visibly unavailable | 0.08 | 0.74 | 0.24 | 0.10 |
| (unavailable) | Statistics, visibly unavailable | 0.38 | 0.74 | 0.24 | 0.10 |
| `lodge-context` | Hunter/source/status text panel | 0.02 | 0.80..0.98 | 0.96 | text height |

Safe regions: keep the top 8% and the outer 2% free of anything essential
(window decorations and the fitted edge). The bottom 20% is covered by the
context panel; treat it as floor/foreground. Nothing interactive is ever
cropped because the whole stage is always inside the window.

### Ready-to-use prompt (lodge)

> Pre-rendered interior of a 1990s big-game hunting lodge on a tropical island,
> painted-backdrop style like a classic point-and-click adventure, 16:9, wide
> shot at eye level. Dark timber walls, a long map table in the lower left
> third, a radio/expedition console desk in the lower middle third, a heavy
> exit door with a brass handle in the lower right third, a trophy wall of
> empty mounting plaques above, warm incandescent lamps as key light from the
> upper left, cool dusk light through a window at upper right. No people, no
> animals, no text, no signs, no UI, no logos. Muted greens, browns and amber;
> slightly grainy, calm, quiet mood. Keep the lower center band uncluttered.

## Slot 2: `expedition_preview.png` (console details preview)

| Property | Value |
| --- | --- |
| Aspect | 4:3 |
| Recommended pixels | 640 x 480 (minimum 320 x 240) |
| Format | PNG with alpha; the element is 160 x 120 dp, `contain` fit over `#0f1214` with a 1 dp border |
| Purpose | A generic expedition vignette shown for any selected expedition in this slice |

Eventually each expedition may carry its own preview and console dressing;
this slot proves the loading path. Keep the subject centered and legible at
160 dp wide.

### Ready-to-use prompt (preview)

> Small painted vignette, 4:3, of a misty island coastline seen from a
> hunting outpost jetty at dawn, distant jungle ridge, no creatures, no
> people, no text. Muted teal and amber palette matching a dark green UI.
> Soft vignette edges fading to transparent.

## Verification after replacing artwork

```sh
cmake --build build/gui-dev --target c2-frontend-gui
SDL_VIDEODRIVER=offscreen build/gui-dev/gui/c2-frontend-gui --self-test --capture /tmp/gui-captures
```

Inspect `01-lodge.png`, `08-lodge-narrow-tall.png`, `09-lodge-ultrawide.png`
and `05-console-details.png`. A missing file shows a labeled placeholder
instead of a blank stage; a corrupt PNG logs `PNG decode failed` and falls back
the same way.
