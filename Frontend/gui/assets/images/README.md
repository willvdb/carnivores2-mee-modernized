# GUI artwork slots

Both images here are tiny authored placeholders generated for this repository
(plain gradients and markers; no game content). They exist so image loading,
aspect fitting, alpha compositing and installation are exercised, not merely
promised. Replace them in place; filenames are the contract. See
`../../docs/ASSET_BRIEF.md` for sizes, safe regions and generation prompts.

| File | Slot | Format |
| --- | --- | --- |
| `lodge_background.png` | Lodge stage background, 16:9 | PNG, RGBA or RGB |
| `expedition_preview.png` | Expedition Console details preview, 4:3 | PNG with alpha |

Decoder: stb_image (PNG only). Missing optional artwork produces a visible
placeholder with the missing filename; it never blanks the screen.
