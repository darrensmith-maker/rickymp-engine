# CoreArenaMP v19 — Native KTX2 for NIF/OpenMW resources

## What this patch does

- Adds native KTX2 reading through Khronos `libktx` (`ktx_read`, pinned to KTX-Software 4.4.2).
- NIF/ESM texture references do **not** need editing: `foo.dds`/`foo.tga` transparently prefers `foo.ktx2` with the same basename.
- A failed or unsupported KTX2 load falls back to the original `.dds`, `.tga`, `.png`, `.bmp`, `.jpeg` or `.jpg`.
- Basis Universal / UASTC KTX2 is transcoded on desktop to:
  - BC1 / DXT1 for opaque textures;
  - BC3 / DXT5 for textures carrying alpha;
  - RGBA8 when S3TC is unavailable.
- Direct non-Basis KTX2 is accepted for common uncompressed formats plus BC1/BC2/BC3.
- BC7/BPTC is intentionally rejected in v19 because this OpenMW OSG fork does not provide a complete `osg::Image` handling path for BPTC, while its S3TC path handles size, mipmaps and DXT vertical flipping.
- KTX orientation is normalized to OpenGL bottom-left by physically flipping top-left KTX data, including DXT mip levels, preserving existing NIF UVs.
- Loading screen image extension filtering accepts `.ktx2`.
- File/decompressed-size safety limits are applied before allocating large KTX2 payloads.

## Build

KTX2 is enabled by default with:

```text
-DOPENMW_USE_KTX2=ON
```

KTX-Software 4.4.2 requires CMake 3.22 or newer. Only the static read/transcode library is built; KTX tools/tests/upload helpers are disabled for the ArenaMP build.

## Conversion helper

See:

```text
tools/ArenaMP-KTX2/Convert-TexturesToKTX2.ps1
tools/ArenaMP-KTX2/README_RU.txt
```

The helper keeps directory structure and basename so no NIF rewrite is required.

## Expected memory/performance behavior

- Opaque BC1 target: 4 bits/pixel (same class as DXT1 DDS).
- Alpha BC3 target: 8 bits/pixel (same class as DXT5 DDS).
- Compared with RGBA8, BC1 uses 1/8 the texture payload and BC3 uses 1/4, before the common mip-chain multiplier.
- Compared with already compressed DXT1/DXT5 DDS, VRAM is normally similar, not magically smaller.
- Universal Basis/UASTC adds a CPU transcode step on first load of each texture in a process. ArenaMP's image cache prevents repeated decoding of the same loaded texture during that run, but DDS can still have a lower first-load CPU cost.
- The main benefit is a modern portable source container and potentially smaller distribution/storage, especially when reusing the same KTX2 asset for future Android transcode targets. Actual disk-size change is content/encoder dependent.

## Important quality note

Bulk converting an existing lossy DDS through an uncompressed intermediate and then UASTC/BC1/BC3 is another lossy generation. Prefer original lossless/source textures when available. If DDS is the only source, use the Quality/UASTC preset and keep the DDS fallback until visual verification is complete.
