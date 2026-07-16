# SpeedyNote upstream

- Repository: <https://github.com/alpha-liu-01/SpeedyNote>
- Release: `v1.5.0`
- Commit: `dd5386366b4b1a51a6e960491feb82e777fbdcb2`
- License: GPL-3.0
- Verified: 2026-07-16

QuizApp vendors this exact source revision. Upstream source remains unmodified in
`upstream/`; QuizApp-specific behavior belongs in adapters outside that folder.
This keeps source attribution auditable and makes future upstream comparisons
mechanical.

## Reused core

- `source/core/Document*`, `Page*`, `DocumentViewport*`, `TouchGestureHandler*`
- `source/layers/VectorLayer.h`
- `source/strokes/VectorStroke.h`, `StrokePoint.h`
- `source/objects/InsertedObject*` and supported object implementations
- page thumbnails, action bars, kinetic scrolling and Android historical input

## Deliberate boundaries

- QuizApp does not reuse SpeedyNote's monolithic `MainWindow` as the learning app shell.
- QuizApp domain services never include SpeedyNote UI headers.
- The PDF provider is abstracted. Qt 6.9.3 does not ship Qt PDF for the Android
  target used by QuizApp, so Android will use a PDFium provider. MuPDF is not
  imported into QuizApp's default build.
- Google ML Kit handwriting OCR is not a default dependency. OCR remains a
  separately declared optional capability.
- Butterfly and Logseq remain interaction references only; AGPL source is not copied.
