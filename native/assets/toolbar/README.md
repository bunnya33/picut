The 16 px toolbar glyph masks are derived from the toolbar reference image
provided by the user on 2026-10-16. The light gray background was removed while
preserving the original edge coverage. The program embeds these PNG masks in
Qt resources, recolors them for normal/selected/disabled states, and supplies
scaled sizes for high DPI screens. No temporary reference image is needed at
runtime.

Reference layout: 32 x 32 buttons, 16 x 16 icons, two 5 px separator slots,
1 px blue (#2080f0) square border, light gray (#f2f2f2) background.

The text-bold, text-italic, text-outline glyph masks and the five text control
icons come from the user's additional inline-text reference on 2026-10-16.
The text toolbar uses its 20-color palette and ordering. Since v0.5.53, the
five cropped text-control PNGs are reference assets only; the controls are
drawn with QPainter in annotation_control_icons.h to avoid stray opaque pixels.
They retain blue/white styling at 14 logical pixels with multiple DPI sizes.
All annotation rotation controls share one circular-arrow glyph.
