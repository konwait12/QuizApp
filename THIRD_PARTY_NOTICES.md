# Third-party notices

## Google Material Design Icons

- Project: <https://github.com/google/material-design-icons>
- Source commit: `819d78680a849ceef4c78f863d8753e3160b7c89`
- License: Apache License 2.0

The native shell vendors selected Material Symbols SVG assets under
`native/resources/icons/`. The application recolors these assets at runtime for
light, dark, and selected states.

## SpeedyNote

- Project: <https://github.com/alpha-liu-01/SpeedyNote>
- Source revision used for the port: `6b2bbc15127b0a8a8c046e7f4168a598c976ee3b`
- License: GNU General Public License v3.0 or later
- Upstream language/runtime: C++17 and Qt 6

`notebook/speedynote-notebook.js` is a modified JavaScript port for QuizApp. It
uses the upstream `Document -> Page -> VectorLayer -> VectorStroke / InsertedObject`
architecture and viewport interaction rules. It is not an official SpeedyNote
build and is not endorsed by the SpeedyNote maintainers.

The corresponding source for the QuizApp port is distributed in this repository.
The full GPL text is in `LICENSE`.

## perfect-freehand

- Project: <https://github.com/steveruizok/perfect-freehand>
- Version: 1.2.3
- License: MIT

The vendored browser build is in `vendor/perfect-freehand.min.js`. Its license is
in `vendor/perfect-freehand.LICENSE.txt`.

## ts-fsrs

- Project: <https://github.com/open-spaced-repetition/ts-fsrs>
- Version: 5.4.1
- License: MIT

The vendored UMD build is in `vendor/ts-fsrs.umd.js`. QuizApp uses it only for
review-card scheduling through `review/fsrs-review.js`. Its license is in
`vendor/ts-fsrs.LICENSE.txt`.

## fsrs-rs

- Project: <https://github.com/open-spaced-repetition/fsrs-rs>
- Version: 6.6.0
- License: BSD 3-Clause License

The native Qt client links the pinned scheduler through the small C ABI bridge
in `native/third_party/fsrs-bridge/`. The bridge exposes only scheduling value
types and uses the official FSRS implementation for four-rating interval and
memory-state calculation. Dependency versions are locked in `Cargo.lock`.
The complete license text is in
`native/third_party/fsrs-bridge/LICENSE.fsrs-rs`.

## PDF.js

- Project: <https://github.com/mozilla/pdf.js>
- Package: `pdfjs-dist`
- Version: 3.11.174
- License: Apache License 2.0
- Package integrity: `sha512-TdTZPf1trZ8/UFu5Cx/GXB7GZM30LT+wWUNfsi6Bq8ePLnb+woNKtDymI2mxZYBpMbonNFqKmiz684DIfnd8dA==`

The vendored browser build and worker are in `vendor/pdfjs/pdf.min.js` and
`vendor/pdfjs/pdf.worker.min.js`. QuizApp uses them to render user-selected PDF
pages as notebook backgrounds. The upstream license is in
`vendor/pdfjs/LICENSE.txt`.

## pdf-lib

- Project: <https://github.com/Hopding/pdf-lib>
- Package: `pdf-lib`
- Version: 1.17.1
- License: MIT
- Package integrity: `sha512-V/mpyJAoTsN4cnP31vc0wfNA1+p20evqqnap0KLoRUN0Yk/p3wN52DOEsL4oBFcLdb76hlpKPtzJIgo67j/XLw==`

The vendored browser build is in `vendor/pdf-lib/pdf-lib.min.js`. QuizApp uses
it to generate multi-page PDF files that contain notebook backgrounds, visible
handwriting layers, and inserted objects. The upstream license is in
`vendor/pdf-lib/LICENSE.md`.

## marked

- Project: <https://github.com/markedjs/marked>
- Version: 15.0.12
- License: MIT
- Package integrity: `sha512-8dD6FusOQSrpv9Z1rdNMdlSgQOIP880DHqnohobOmYLElGEqAL/JvxvuxZO16r4HtjTlfPRDC1hbvxC9dPN2nA==`

The vendored UMD build is in `vendor/marked/marked.umd.js`. QuizApp uses it to
parse Markdown notebook objects. The upstream license is in
`vendor/marked/LICENSE.md`.

## KaTeX

- Project: <https://github.com/KaTeX/KaTeX>
- Version: 0.16.22
- License: MIT
- Package integrity: `sha512-XCHRdUw4lf3SKBaJe4EvgqIuWwkPSo9XoeO8GjQW94Bp7TWv9hNhzZjZ+OH9yf1UmLygb7DIT5GSFQiyt16zYg==`

The vendored browser build, stylesheet and fonts are in `vendor/katex/`.
QuizApp uses them to render editable LaTeX formula objects. The upstream
license is in `vendor/katex/LICENSE.txt`.

## html2canvas

- Project: <https://github.com/niklasvh/html2canvas>
- Version: 1.4.1
- License: MIT
- Package integrity: `sha512-fPU6BHNpsyIhr8yyMpTLLxAbkaK8ArIBcmZIRiBLiDhjeqvXolaEmDGmELFuX9I4xDcaKKcJl+TKZLqruBbmWA==`

The vendored browser build is in `vendor/html2canvas/html2canvas.min.js`.
QuizApp uses it to capture the browser-rendered Markdown and KaTeX result as a
movable notebook object. The upstream license is in
`vendor/html2canvas/LICENSE.txt`.

## Cytoscape.js

- Project: <https://github.com/cytoscape/cytoscape.js>
- Version: 3.33.1
- License: MIT
- Package integrity: `sha512-iJc4TwyANnOGR1OmWhsS9ayRS3s+XQ185FmuHObThD+5AeJCakAAbWv8KimMTt08xCCLNgneQwFp+JRJOr9qGQ==`

The vendored browser build is in `vendor/cytoscape/cytoscape.min.js`. QuizApp
uses it for the interactive graph of notebooks, tags, linked questions and
external resources. The upstream license is in
`vendor/cytoscape/LICENSE.txt`.
