# Xenon artwork

`xenon-mark.png` is the unchanged user-supplied logo. Its gaps are transparent,
not white artwork. The original is also retained at `1x/Xenon Logo.png`.

`xenon-icon.png` is a rounded navy tile made with the built-in image-generation
tool from that reference. The revised image removes the first draft's incorrect
white separators. `xenon-icon.ico` packages this image at 16, 20, 24, 32, 40, 48,
64, 128 and 256 pixels for Windows. Rebuild that format with
`./scripts/build-icon.ps1`; no image-generation service is required for builds.

Final image-edit prompt (built-in tool, not the CLI fallback):

> Image1 is the edit target (rounded navy app icon). Image2 is the original Xenon
> mark reference. Correct Image1 only: REMOVE ALL WHITE STROKES AND WHITE FILLS
> between the colored facets. The original Image2 has TRANSPARENT GAPS between
> separate colored pieces, NOT white artwork. In the finished icon, these
> separating diagonal and horizontal gaps must be plain deep navy matching the
> tile background exactly. There must be absolutely no white X, no white stripe,
> no white border or gray line between facets. Keep the angular teal/cyan/blue
> facets and their recognizable original layout. Keep the dark navy rounded-square
> tile and transparent exterior. Clean crisp flat app icon; simplify away
> bevels/shadows and stray cyan pixels outside tile. One centered full icon, no
> text or extra shapes.

The generated tile is a derivative, not a pixel-identical enlargement of the
180-by-181 original. The original artwork remains available for other layouts.
