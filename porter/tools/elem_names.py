"""Plain-English names for Yotei's pass labels (debug-marker stacks) and queues, for the element map.

Each entry: (substring of the pass path, short name, one or two plain sentences). The first match wins, so more
specific entries come first. The texts are best guesses from the marker names and from what the screenshots show
when the element is skipped; the map says so.
"""

QUEUES = {
    "q0": "main graphics queue",
    "q20": "async compute queue 0x20 (particles)",
    "q30": "async compute queue 0x30 (shadow resolve, probe maps)",
    "q38": "async compute queue 0x38 (ambient light, fog, light culling)",
    "q48": "async compute queue 0x48 (sky)",
    "q50": "async compute queue 0x50 (terrain)",
    "q51": "async compute queue 0x51 (cloth, own Vulkan queue)",
    "q52": "async compute queue 0x52 (proxy geometry)",
    "q53": "async compute queue 0x53",
}

PASSES = [
    ("Cascaded shadow map/Particle Finalize", "Particle finishing (shadow stage)", "Final step of the particle simulation, run inside the shadow stage: it writes the particle lists that the shadow and main passes draw."),
    ("Cascaded shadow map/Grass", "Grass shadows", "Grass drawn into the sun's shadow map."),
    ("Cascaded shadow map", "Sun shadows", "The game draws the scene a second time from the sun's point of view, in several rings (cascades) from near to far, to know what is in shadow."),
    ("Shadow Wait for render target", "Shadow wait", "The main queue waits here until the async queues have finished the shadow targets."),
    ("Water Deferred", "Water surfaces", "Rivers and lakes drawn into the scene."),
    ("Water Lighting", "Water lighting", "Lighting and reflections of water surfaces."),
    ("Water Particles", "Water particles", "Splashes and spray."),
    ("Deferred/Tiled Decals", "Decals", "Painted-on details such as mud, moss, blood and footprints, applied to the screen tile by tile."),
    ("Deferred/Draw high-res terrain", "Terrain", "The ground itself: the high-detail terrain mesh."),
    ("Deferred/Grass", "Grass", "Grass blades and clumps, built by a compute job and then drawn."),
    ("Deferred/Deferred Emissive Particles", "Glowing particles", "Embers and sparks that give off light."),
    ("Deferred/Deferred Particles", "Particles in the scene pass", "Smoke, dust and leaves drawn into the main scene pass."),
    ("Deferred wetness", "Wetness", "Makes surfaces look wet after rain."),
    ("Deferred", "Scene objects (G-buffer)", "The main scene pass: every visible object (trees, rocks, houses, people, grass) is drawn once to record its colour, normal and material."),
    ("Eval non-RT Probes", "Ambient light probes", "Soft ambient light from pre-baked light probes (the non-ray-traced path)."),
    ("Deferred Ambient", "Ambient light", "Sky and bounce light that fills shadows."),
    ("Cull CS Lights", "Light culling", "Finds which lights touch which screen tiles, on an async queue."),
    ("CS Lights", "Local lights (compute)", "Torches, lamps and fires lit by a compute shader."),
    ("PS Lights", "Local lights (pixel)", "Local lights drawn as light volumes with pixel shaders."),
    ("Lighting", "Lighting", "Lights the scene from what the G-buffer recorded."),
    ("ShLightMgr", "Light manager", "Prepares the ambient light data (spherical harmonics) for this frame."),
    ("Interior Volumes", "Interior light volumes", "Light and fog volumes for the inside of buildings; one draw that waits for the graphics card."),
    ("Velocity", "Motion vectors", "Records how far each pixel moved since the last frame, for motion blur and temporal anti-aliasing."),
    ("Vcull", "GPU visibility culling", "The GPU decides which objects are visible, before they are drawn."),
    ("Proxy Set Fill Streams", "Proxy streams", "Fills the per-instance data streams for proxy (stand-in) geometry."),
    ("Proxy Set Frame", "Proxy geometry", "Simplified stand-in geometry updated each frame (used for occlusion or lighting)."),
    ("Depth-only", "Depth pre-pass", "Draws only depth first, so later passes skip hidden pixels."),
    ("Terrain Deformer Depth", "Snow and mud imprints", "Depth of everything that presses into snow and mud, for footprints and trails."),
    ("Terrain Deformation Update", "Terrain deformation", "Snow and mud deformation: footprints and trails."),
    ("Terrain Clipmap Update", "Terrain clipmap", "Updates the multi-resolution terrain height and texture maps around the camera."),
    ("Displacement", "Displacement", "Displaces surfaces (snow, ground detail) by a height map."),
    ("CS Mesh", "Compute meshes", "Meshes built by a compute job each frame."),
    ("Composite depth bias", "Depth bias composite", "Combines depth with a bias for later passes."),
    ("SSDO", "Ambient occlusion (SSDO)", "Screen-space directional occlusion: soft contact shadows in corners and under objects."),
    ("Separable Scattering", "Skin scattering", "Light passing through skin and leaves (subsurface scattering)."),
    ("Forward-shaded Opaque", "Forward-shaded objects", "Objects lit directly while drawn (special materials)."),
    ("Postprocessing/SSGI", "Screen-space light bounce", "Screen-space global illumination: light bouncing off nearby surfaces."),
    ("Postprocessing/Hair", "Hair", "Hair rendering pass."),
    ("Postprocessing/CutHeight", "Cut grass", "Tracks where grass was cut or flattened."),
    ("Postprocessing/DoF", "Depth of field", "Blurs what is out of focus."),
    ("Postprocessing/Bloom", "Bloom", "The soft glow around bright things."),
    ("Postprocessing/HDR Tonemap", "Tone mapping", "Turns the high-dynamic-range picture into screen colours."),
    ("Postprocessing/Translucent Particles", "Translucent particles", "See-through particles (smoke, steam) drawn after lighting."),
    ("Postprocessing", "Post effects", "Screen effects after lighting: glow, blur, colour grading, tone mapping."),
    ("Frame/UI", "HUD and menus", "The interface drawn on top of the picture."),
    ("Post-frame", "End of frame", "Work after the frame: copies and resolves for the next frame."),
    ("Particle Wait For Velocity", "Particle wait", "The particle queue waits for the motion vectors of the main queue."),
    ("Particle Emit", "Particle emission", "Spawns new particles (smoke, sparks, leaves) on an async queue; one of the heaviest GPU programs."),
    ("Particle Update", "Particle simulation", "Moves the particles on an async queue."),
    ("Particle Finalize", "Particle finishing", "Final step of the particle simulation."),
    ("Particle CS Frame", "Particles", "Particle simulation on an async queue."),
    ("Ambient Spec Update", "Reflections update", "Updates the reflection (ambient specular) data."),
    ("Async ambient sh", "Ambient light (async)", "Ambient light (spherical harmonics) computed on an async queue."),
    ("Volfog", "Volumetric fog", "Fog and light shafts with depth."),
    ("Async Sky LUT Update", "Sky", "Sky colour tables (atmosphere scattering)."),
    ("Async Sh Grid Update", "Ambient light grid", "Updates the 3D grid of ambient light samples."),
    ("Async Clouds Update", "Clouds", "Cloud simulation and lighting."),
    ("SSR", "Screen-space reflections", "Reflections taken from the picture itself."),
    ("Generate high-res terrain instances", "Terrain build (instances)", "Picks the high-detail terrain patches around the camera, on an async queue."),
    ("Generate Terrain Index Buffers", "Terrain build (triangles)", "Builds the terrain triangles (tessellation) on an async queue."),
    ("Update Oct Maps", "Probe maps", "Updates octahedral maps (light or reflection probes)."),
    ("Resolve Shadow Map Slice", "Shadow resolve", "Turns the shadow maps into a screen-space shadow mask."),
    ("Enqueue Shadow Resolve", "Shadow resolve", "Turns the shadow maps into a screen-space shadow mask."),
    ("Screen Space Shadows", "Screen-space shadows", "Small contact shadows computed from the picture."),
    ("Directional Shadow Combine", "Sun shadow combine", "Combines the sun's shadow cascades into one mask, on an async queue (one job of 35-40 ms)."),
    ("Directional Shadow Copy", "Shadow copy", "Copies of the sun shadow map."),
    ("Wait for Async Dir Shadow Combine", "Shadow wait (async)", "A queue waits for the combined sun shadow."),
    ("Dirshadow occlusion", "Shadow occlusion", "Directional shadow occlusion."),
    ("CS Cloth", "Cloth", "Cloth simulation (capes, banners) on its own queue; one of the heaviest GPU users."),
    ("Ripple", "Water ripples", "Ripples on water."),
    ("Clear G-Buffer", "G-buffer clear", "Clears the G-buffer before drawing."),
    ("Upsample to Display Resolution", "Upscaling", "Scales the image up to the display resolution."),
    ("Clear SSR", "Reflections clear", "Clears the screen-space reflections."),
    ("Clear cut height buffers", "Grass cut buffers", "Clears the buffers that record cut grass."),
    ("Sky", "Sky", "The sky dome."),
]


def describe(label):
    """Returns (queue name, short name, plain text) for a label "q<id>|m1/m2/..."."""
    queue, _, path = label.partition("|")
    for needle, name, text in PASSES:
        if needle in path:
            return QUEUES.get(queue, queue), name, text
    if not path:
        return QUEUES.get(queue, queue), "Unnamed work", "Work on this queue outside any named pass (mostly waits for other queues)."
    return QUEUES.get(queue, queue), path.split("/")[-1], ""
