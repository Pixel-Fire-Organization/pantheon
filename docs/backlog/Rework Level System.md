# Rework Level System

This document outlines the proposed architectural changes to the level compiler and engine streaming systems to optimize memory usage, improve performance on the PS2, and streamline the level design workflow in TrenchBroom.

## 1. Topological / Portal-Based Sectorization
**The Problem:** The current rigid grid-based sectorization loads a fixed radius of sectors around the player, wasting RAM and IO bandwidth on geometry occluded by walls.
**The Solution:**
- Move away from fixed grid squares (`_sector_size`) to a topological, graph-based map structure.
- Level designers place `visportal` planes in TrenchBroom at doorways and choke points.
- The map compiler (`compile_level.py`) builds an exact Potentially Visible Set (PVS) graph. The engine will only load and render sectors that are topologically connected and visible through these portals, saving massive amounts of memory.

## 2. Strict Compiler Constraints
**The Problem:** Without limits, designers may inadvertently create massive, unoptimized sectors that crash the console.
**The Solution:**
- Implement a hard polygon or byte limit per sector during the Python map cook phase.
- If a sector exceeds the limit, the compiler throws a fatal error and refuses to build.
- This forces designers to actively place visportals and optimize density during the blockout phase, pushing the optimization burden to authoring time rather than runtime.

## 3. Dedicated Memory Budget & Hybrid Terrain
**The Problem:** Handling open outdoor environments with portals is inefficient.
**The Solution:**
- Dedicate a fixed ~12MB pool of the PS2's 32MB main RAM specifically for level data.
- Use a **Hybrid System**: Indoor structures use portal-based sectorization, while open outdoor terrain uses coarse grid chunks.
- Distant outdoor chunks are masked by fog and heavily rely on the LOD1 rendering system to fit within memory.

## 4. Efficient Entity Tracking
**The Problem:** Finding which custom-shaped portal sector an entity belongs to requires expensive math (point-in-polygon tests).
**The Solution:**
- **Simulation (CPU):** Use a Euclidean distance check (e.g., a 300-unit simulation radius, similar to Minecraft). Entities outside this radius are put to sleep to save CPU.
- **Rendering (Culling):** Entities simply track when their bounding box crosses a `visportal` plane. When they cross, they update their sector membership. This guarantees they are culled correctly by the PVS system with almost zero CPU overhead.

## 5. Compile-Time Terrain & Water (Displacements)
**The Problem:** Building a standalone terrain engine requires massive C++ engine changes (custom LODs, streaming, physics) and introduces stitching seams between terrain and buildings.
**The Solution:**
- Keep all terrain and water authoring inside TrenchBroom using standard brushes to ensure perfect, watertight stitching with architecture.
- Introduce `func_terrain` and `func_water` entities.
- A flat brush acts as a bounding box. The entity properties specify a heightmap texture.
- At compile time, `compile_level.py` displaces the brush geometry into complex slopes and valleys (similar to Source Engine displacements).
- Because it compiles down to standard static geometry, the engine's existing LOD and streaming systems handle the terrain automatically with zero C++ changes.

## 6. WYSIWYG Editor Sync for Terrain
**The Problem:** Designers cannot see the final displaced terrain inside TrenchBroom while working, making it hard to place trees, props, and buildings accurately.
**The Solution:**
- Create a background script (or editor hotkey) that reads the `func_terrain` heightmap and generates a temporary `.obj` model file in a `_temp_models/` folder.
- The `func_terrain` entity's `model` property points to this `.obj` file.
- TrenchBroom's auto-reload feature instantly displays the generated terrain mesh in the 3D viewport.
- During the final map cook, `compile_level.py` ignores the `.obj` proxy and bakes the raw geometry into the BSP sectors.
