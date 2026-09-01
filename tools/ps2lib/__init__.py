"""ps2lib — shared host-side building blocks for the PS2 asset/level pipeline.

Modules:
  mesh     dedup / stripify / stitch / verify + BKM2 mesh baking (vec4 strips)
  tim2     TIM2 texture encoders (rgba32 / pal8 / pal8 with a transparent index)
  ps2a     the 2080-byte AssetFileHeader (.ps2a) writer
  mapparse Valve-220 .map parsing: brushes -> planes -> polygons with UVs

These are consumed by tools/cook_assets.py and tools/compile_level.py so both
produce byte-identical geometry/textures through one code path.
"""
