# Layout References

This file records design references used for QuizApp layout work.

## 2026-07-07 Quiz View Spacing

- Mobbin: mobile learning and quiz app screen hierarchy, especially separating header actions from the reading area.
- component.gallery: card, radio option, and feedback panel structure patterns.
- React Bits: light interaction rhythm only. No external React dependency is used in QuizApp.

Implementation notes:

- Keep QuizApp single-file and offline-capable for APK packaging.
- Use native CSS instead of CDN components or external icon scripts.
- Prioritize readable question blocks, generous option hit targets, and separated answer/explanation feedback.

## 2026-07-07 Tablet Landscape Layout

- Apple Human Interface Guidelines: Split Views and Layout guidance for using horizontal space as multiple panes on iPad-sized layouts.
- Android Developers: Window size classes guidance for switching behavior at expanded-width layouts instead of relying on device names.

Implementation notes:

- Tablet landscape uses a two-pane quiz card: question stem on the left, options and answer feedback on the right.
- Phone and narrow portrait layouts keep the existing single-column flow.
- The app remains single-file and offline-capable; no external component runtime is added.

## 2026-07-07 Tool Panels And Themes

- Material Design 3 color roles: treat color as semantic roles across surfaces, text, borders, and states instead of changing only the primary button color. Reference: https://m3.material.io/styles/color/roles
- daisyUI themes: use a named theme preset model with light and dark variants, but keep QuizApp dependency-free and implement it with local CSS variables. Reference: https://daisyui.com/docs/themes/
- Mobbin and component.gallery: use collapsible product panels and compact action rows for mobile/tablet app surfaces where repeated action buttons would crowd the page.

Implementation notes:

- Home and subject tool groups share one collapsible panel renderer.
- Chapter actions support a flat mode and a compact "expand modes" mode.
- Settings stores the default expanded/collapsed behavior and theme preset choices in local storage.
