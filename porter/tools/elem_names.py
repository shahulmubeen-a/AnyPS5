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
    ("Cascaded shadow map/Particle Finalize", "Particle finishing (shadow stage)", "Final step of the particle simulation that runs inside the shadow stage: it writes the particle lists the shadow and main passes draw."),
    ("Cascaded shadow map", "Sun shadows", "The game draws the scene a second time from the sun's point of view, in several rings (cascades) from near to far, to know what is in shadow."),
    ("Shadow Wait for render target", "Shadow wait", "The main queue waits here until the async queues have finished the shadow targets."),
    ("Deferred/Tiled Decals", "Decals", "Painted-on details such as mud, moss, blood and footprints, applied to the screen tile by tile."),
    ("Deferred/Draw high-res terrain", "Terrain", "The ground itself: the high-detail terrain mesh."),
    ("Deferred wetness", "Wetness", "Makes surfaces look wet after rain."),
    ("Deferred", "Scene objects (G-buffer)", "The main scene pass: every visible object (trees, rocks, houses, people, grass) is drawn once to record its colour, normal and material."),
    ("Water Lighting", "Water", "Lighting and reflections of water surfaces (rivers, lakes)."),
    ("Eval non-RT Probes", "Ambient light probes", "Soft ambient light from pre-baked light probes (the non-ray-traced path)."),
    ("Deferred Ambient", "Ambient light", "Sky and bounce light that fills shadows."),
    ("CS Lights", "Local lights (compute)", "Torches, lamps and fires lit by a compute shader."),
    ("PS Lights", "Local lights (pixel)", "Local lights drawn as light volumes with pixel shaders."),
    ("Lighting", "Lighting", "Lights the scene from what the G-buffer recorded."),
    ("ShLightMgr", "Light manager", "Prepares the ambient light data (spherical harmonics) for this frame."),
    ("Velocity", "Motion vectors", "Records how far each pixel moved since the last frame, for motion blur and temporal anti-aliasing."),
    ("Vcull", "GPU visibility culling", "The GPU decides which objects are visible, before they are drawn."),
    ("Depth-only", "Depth pre-pass", "Draws only depth first, so later passes skip hidden pixels."),
    ("Postprocessing/DoF", "Depth of field", "Blurs what is out of focus."),
    ("Postprocessing/Bloom", "Bloom", "The soft glow around bright things."),
    ("Postprocessing", "Post effects", "Screen effects after lighting: glow, blur, colour grading, tone mapping."),
    ("Frame/UI", "HUD and menus", "The interface drawn on top of the picture."),
    ("Post-frame", "End of frame", "Work after the frame: copies and resolves for the next frame."),
    ("Particle Wait For Velocity", "Particle wait", "The particle queue waits for the motion vectors of the main queue."),
    ("Particle Update", "Particles", "Smoke, sparks, dust, leaves and snow, simulated on an async queue."),
    ("Particle CS Frame", "Particles", "Particle simulation on an async queue."),
    ("Ambient Spec Update", "Reflections update", "Updates the reflection (ambient specular) data."),
    ("Async ambient sh", "Ambient light (async)", "Ambient light (spherical harmonics) computed on an async queue."),
    ("Volfog", "Volumetric fog", "Fog and light shafts with depth."),
    ("Async Sky LUT Update", "Sky", "Sky colour tables (atmosphere scattering)."),
    ("Generate Terrain Index Buffers", "Terrain mesh build", "Builds the terrain triangles (tessellation) on an async queue."),
    ("Terrain Deformation Update", "Terrain deformation", "Snow and mud deformation: footprints and trails."),
    ("Update Oct Maps", "Probe maps", "Updates octahedral maps (light or reflection probes)."),
    ("Resolve Shadow Map Slice", "Shadow resolve", "Turns the shadow maps into a screen-space shadow mask."),
    ("Enqueue Shadow Resolve", "Shadow resolve", "Turns the shadow maps into a screen-space shadow mask."),
    ("Directional Shadow Copy", "Shadow copy", "Copies of the sun shadow map."),
    ("Wait for Async Dir Shadow Combine", "Shadow wait (async)", "An async queue waits for the combined sun shadow."),
    ("Dirshadow occlusion", "Shadow occlusion", "Directional shadow occlusion."),
    ("Proxy Set Frame", "Proxy geometry", "Simplified stand-in geometry updated each frame (used for occlusion or lighting)."),
    ("Ripple", "Water ripples", "Ripples on water."),
    ("Cull CS Lights", "Light culling", "Finds which lights touch which screen tiles."),
    ("Clear G-Buffer", "G-buffer clear", "Clears the G-buffer before drawing."),
    ("Upsample to Display Resolution", "Upscaling", "Scales the image up to the display resolution."),
    ("Composite depth bias", "Depth bias composite", "Combines depth with a bias for later passes."),
    ("Clear SSR", "Reflections clear", "Clears the screen-space reflections."),
    ("Clear cut height buffers", "Grass cut buffers", "Clears the buffers that record cut grass."),
    ("Generate Terrain", "Terrain build", "Terrain mesh generation."),
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
