# OCR rasterizer golden baselines

Baseline PNGs for the rasterizer regression tests (`OcrGoldenTests`, OCR Phase 4D).
Each `*.png` is the normalized monochrome strip produced by `rasterizeStrokes()`
for a fixed synthetic stroke case (see `source/ocr/OcrGoldenTests.h`).

The test compares the live render against these baselines with a tolerance:
image dimensions must match exactly (the primary normalization lock), while
per-pixel intensity may differ slightly to absorb antialiasing jitter across Qt
patch versions.

## Generating / refreshing baselines

Baselines are produced by running the test (requires a debug build,
`-DENABLE_DEBUG_OUTPUT=ON`):

```bash
# First run auto-creates any missing baselines (the case passes with a
# GENERATED note); commit the resulting PNGs.
./speedynote --test-ocr-golden

# Force-regenerate ALL baselines after an intentional rasterizer change:
SPEEDYNOTE_GEN_GOLDEN=1 ./speedynote --test-ocr-golden
```

Regenerate only after a deliberate change to `OcrStrokeRasterizer`, and review
the image diff before committing. Baselines are generated on a developer machine
(macOS for Phase 4D); cross-platform rendering differences are expected and are
why the comparison is tolerance-based.
