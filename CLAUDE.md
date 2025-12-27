# CLAUDE.md - Voxel Engine Project Guide

This document provides essential context for Claude Code when working in this repository.

## Project Overview

VoxelEngine is a Minecraft-inspired voxel game engine written in C++17 using OpenGL 4.6, with an in-progress Vulkan backend. It features deferred rendering, GPU culling, procedural terrain generation, and a comprehensive graphics settings system.

## Build System

**CMake-based build** with FetchContent for dependencies:

```bash
# Configure (from project root)
mkdir build && cd build
cmake ..

# Build with Visual Studio (Windows)
cmake --build . --config Release

# Or using VS CMake directly:
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build "G:/Minecraft Clone/build" --config Release
```

**Executables produced:**
- `build/bin/Release/VoxelEngine.exe` - Main game
- `build/bin/Release/VoxelLauncher.exe` - OpenGL launcher (Windows)
- `build/bin/Release/VulkanTest.exe` - Vulkan backend test

## Project Structure

```
src/
  main.cpp              # Application entry, game loop, inline shaders (6000+ lines)
  core/
    Config.h            # GameConfig with all settings, hardware detection
    Player.h            # Player physics, camera, inventory
    Camera.h            # View/projection matrices
    Raycast.h           # Block picking raycasting
    CrashHandler.h      # Exception handling, crash logs
  world/
    Block.h             # Block types enum and properties
    Chunk.h             # 16x256x16 chunk data, lighting
    World.h             # Chunk management, generation coordination
    TerrainGenerator.h  # Procedural terrain with biomes
    WorldPresets.h      # Generation presets (amplified, superflat, etc.)
    TerraMath.h         # Custom expression parser for terrain equations
    WorldSaveLoad.h     # World persistence
    ChunkThreadPool.h   # Multi-threaded chunk generation
  render/
    VertexPool.h        # 512MB persistent-mapped vertex buffer pool
    BinaryGreedyMesher.h # Optimized chunk meshing
    GPUCulling.h        # Compute shader GPU frustum/occlusion culling
    ShaderCache.h       # Shader program caching
    MeshOptimizer.h     # meshoptimizer integration
    ChunkMesh.h         # Per-chunk mesh data
    TextureAtlas.h      # Block texture atlas
    Screenshot.h        # Screenshot capture
    rhi/                # Render Hardware Interface abstraction
      RHI.h             # Master include
      RHIDevice.h       # Abstract device factory
      RHIBuffer.h       # Buffer abstraction
      opengl/           # OpenGL backend implementation
      vulkan/           # Vulkan backend implementation (in progress)
    DeferredRenderer.h  # Deferred rendering pipeline
    Renderer.h          # Abstract renderer interface
  ui/
    MenuUI.h            # UI primitives (buttons, sliders, text input, dropdowns)
    MainMenu.h          # Main menu screen
    PauseMenu.h         # In-game pause menu
    SettingsMenu.h      # Graphics/gameplay settings
    ControlsScreen.h    # Keybind configuration
    WorldSelectScreen.h # World selection/creation
    WorldCreateScreen.h # World generation settings
    DebugOverlay.h      # F3 debug information
    LoadingScreen3D.h   # 3D loading screen
    PanoramaRenderer.h  # Menu background panorama
  input/
    KeybindManager.h    # Configurable keybinds with primary/secondary keys
  benchmark/
    BenchmarkSystem.h   # Performance benchmarking

shaders/
  forward/              # Forward rendering shaders
    chunk.vert/frag     # Terrain rendering
    water.vert/frag     # Water with animation
    shadow.vert/frag    # Shadow mapping
  deferred/             # Deferred rendering shaders
    gbuffer.vert/frag   # G-Buffer generation
    composite.vert/frag # Deferred compositing
    zprepass.vert/frag  # Depth pre-pass
  postprocess/          # Post-processing effects
    ssao.vert/frag      # Screen-space ambient occlusion
    fsr_easu.vert/frag  # FSR upscaling (EASU pass)
    fsr_rcas.frag       # FSR sharpening (RCAS pass)
    bloom_*.frag        # Bloom effect
  compute/              # Compute shaders
    hiz_downsample.comp # Hi-Z mipmap generation
    occlusion_cull.comp # GPU occlusion culling
  sky/
    sky.vert/frag       # Procedural sky rendering
  effects/
    precipitation.vert/frag  # Rain/snow particles
```

## Key Dependencies

All fetched via CMake FetchContent:
- **GLFW 3.4** - Windowing and input
- **GLM 1.0.1** - Math library
- **GLAD** - OpenGL loader (in vendor/glad)
- **stb** - Image loading (stb_image)
- **FastNoiseLite** - Procedural noise for terrain
- **meshoptimizer** - Mesh optimization
- **Vulkan-Headers** - Vulkan API headers
- **volk** - Vulkan meta-loader
- **VMA** - Vulkan Memory Allocator
- **glslang** - GLSL to SPIR-V compiler

## Architecture Notes

### Rendering Pipeline

1. **Deferred Rendering** (default):
   - G-Buffer pass: Position, Normal, Albedo, Depth
   - SSAO pass (optional, configurable samples)
   - Shadow mapping with cascade shadows (1-4 cascades)
   - Compositing with lighting
   - Post-processing: FSR upscaling, bloom, vignette

2. **GPU Culling**:
   - Hi-Z occlusion culling via compute shaders
   - Frustum culling
   - Indirect draw commands

3. **Vertex Pool**:
   - 512MB persistent-mapped buffer
   - Sub-allocated for chunk meshes
   - Avoids per-frame buffer uploads

### World Generation

- Chunks are 16x256x16 blocks
- Multi-threaded generation via ChunkThreadPool
- Biome system with temperature/humidity mapping
- Custom equation support via TerraMath expression parser
- Presets: default, amplified, superflat, mountains, islands, caves

### Configuration

- `GameConfig` in Config.h contains all settings
- Auto-tuning based on hardware detection (GPU tier)
- Saved to `settings.cfg`
- Graphics presets: Low, Medium, High, Ultra, Custom

### Keybinds

- `KeybindManager` supports primary and secondary keys per action
- Developer keybinds (Wireframe, Fly, Noclip) excluded from UI
- Debug combinations (F3+G, F3+B, etc.) for development

## Common Tasks

### Adding a New Block Type

1. Add to `BlockType` enum in `Block.h`
2. Add properties in `BlockProperties` array
3. Add texture coordinates in texture atlas

### Adding a New Setting

1. Add field to `GameConfig` in `Config.h`
2. Add save/load handling in `save()` and `load()` methods
3. Add UI control in `SettingsMenu.h`

### Adding a New Shader

1. Create shader files in appropriate `shaders/` subdirectory
2. For OpenGL: Load via `ShaderCache::createCachedProgram()`
3. For Vulkan (WIP): Compile to SPIR-V via glslang

### Modifying Terrain Generation

- Edit `TerrainGenerator.h` for height/biome algorithms
- Use `WorldPresets.h` for preset configurations
- Custom equations parsed by `TerraMath::ExpressionParser`

## Vulkan Backend Status

The Vulkan backend is **in progress**. Current state:
- RHI abstraction layer defined (`src/render/rhi/`)
- OpenGL backend wrapped in RHI
- Vulkan device/swapchain creation implemented
- Shader compilation to SPIR-V via glslang
- Feature parity work ongoing

See plan file at `~/.claude/plans/elegant-squishing-sky.md` for implementation roadmap.

## Debug Keys (In-Game)

- **F3** - Toggle debug overlay
- **F3+G** - Toggle chunk borders
- **F3+B** - Toggle block hitboxes
- **F3+H** - Toggle advanced tooltips
- **F3+P** - Toggle pause on focus loss
- **F3+F** - Cycle render distance
- **F3+C** - Copy player coordinates
- **F11** - Toggle fullscreen
- **F2** - Screenshot

## Code Style

- C++17 standard
- Header-only design for most components
- Singleton pattern for managers (KeybindManager, ShaderCache)
- GLM for math (vec3, mat4, etc.)
- GLFW for input constants (GLFW_KEY_*, GLFW_MOUSE_BUTTON_*)

## Performance Considerations

- Chunk meshing is multi-threaded
- GPU culling reduces draw calls significantly
- FSR upscaling available for lower-end hardware
- SSAO runs at configurable resolution scale (0.25x to 1.0x)
- Indirect drawing reduces CPU overhead

## Testing

- `tests/VulkanTest.cpp` - Vulkan initialization test
- In-game benchmark mode available via BenchmarkSystem
- Benchmark results saved to `benchmark_*.txt`
