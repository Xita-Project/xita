# Xita app icon

Generated with the built-in image_gen tool. The source is kept at
`assets/branding/xita-icon-source.png`; the app uses `sce_sys/icon0.png`.

Generation prompt:

Use case: logo-brand. Asset type: production app icon for Xita, an original Xbox game runtime on PlayStation Vita. Create one polished square raster icon, 1024 by 1024. Subject: a single powerful sculpted X emblem, centered, four broad angular tapered arms with precise chamfered edges and a compact dark diamond-shaped central intersection. Retain the established vivid Xbox-era lime green identity. Materials: emerald and electric lime enamel faces, restrained dark graphite bevels, subtle dimensional relief and precise edge highlights. Backdrop: opaque near-black graphite with a very subtle radial green light behind the emblem. Composition: emblem occupies the central 70 percent, all four arm tips well within a centered circular safe area so it remains intact in the Vita bubble; perfectly balanced and immediately recognizable at 128 by 128 and 48 by 48. Visual style: refined early-2000s console industrial design, bold silhouette, exceptional clean edge quality, minimal restrained lighting, strong contrast. No text, no wordmark, no console hardware, no controllers, no characters, no halo ring, no particles, no circuits, no lens flare, no glowing white ball, no thin ornamental lines, no mockup or multiple variations. Fill the square with the dark backdrop; deliver the actual single icon asset.

Final edit prompt:

Use case: precise-object-edit. Input image: edit target, the generated Xita app icon. Make one targeted change: reduce the complete X emblem to 82 percent of its current size while keeping it perfectly centered on the same square canvas. Extend the existing near-black graphite background naturally around it. Every tip must fit comfortably inside the inscribed circle, with margin for a Vita home-screen bubble crop. Preserve the exact angular silhouette, proportions, emerald and lime enamel faces, graphite bevels, restrained highlights, dark central diamond and subtle green backdrop glow. Do not redesign the X or add any text, border, ring, particles or other elements. Deliver a single square finished icon asset.

Export (ImageMagick):

```sh
magick assets/branding/xita-icon-source.png -resize 128x128 -alpha off -strip -define png:color-type=3 PNG8:sce_sys/icon0.png
```

