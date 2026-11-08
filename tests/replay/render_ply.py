# Blender (background) render of a PLY: front view, flat grey material, white background; holes show as white gaps.
# usage: blender -b -P render_ply.py -- in.ply out.png [width]
import bpy, sys, math
args = sys.argv[sys.argv.index("--") + 1:]
src, out = args[0], args[1]; W = int(args[2]) if len(args) > 2 else 1800
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.ply_import(filepath=src)
obj = bpy.context.selected_objects[0]
# centre the object and find its extent
bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS'); obj.location = (0, 0, 0)
dims = obj.dimensions; ext = max(dims.x, dims.y, dims.z)
mat = bpy.data.materials.new("grey"); mat.use_nodes = True; bsdf = mat.node_tree.nodes["Principled BSDF"]; bsdf.inputs["Base Color"].default_value = (0.30, 0.29, 0.28, 1); bsdf.inputs["Roughness"].default_value = 0.9
obj.data.materials.append(mat)
scene = bpy.context.scene; scene.render.engine = 'BLENDER_EEVEE_NEXT' if hasattr(bpy.types, 'RenderSettings') and 'BLENDER_EEVEE_NEXT' in [i.identifier for i in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
scene.render.resolution_x = W; scene.render.resolution_y = int(W * max(dims.y, 1) / max(dims.x, 1)) if dims.x > dims.y else W; scene.render.resolution_y = min(max(scene.render.resolution_y, 600), 2600)
scene.render.film_transparent = False; world = bpy.data.worlds.new("w"); scene.world = world; world.use_nodes = True; world.node_tree.nodes["Background"].inputs[0].default_value = (1, 1, 1, 1); world.node_tree.nodes["Background"].inputs[1].default_value = 1.0
# orthographic camera looking down -Z at the XY plane (Artec scans: the stone face is roughly the XY plane seen from +Z)
cam_data = bpy.data.cameras.new("cam"); cam_data.type = 'ORTHO'; cam_data.ortho_scale = ext * 1.08; cam_data.clip_start = 0.1; cam_data.clip_end = ext * 20
cam = bpy.data.objects.new("cam", cam_data); scene.collection.objects.link(cam); cam.location = (0, 0, ext * 3); cam.rotation_euler = (0, 0, 0); scene.camera = cam
light_data = bpy.data.lights.new("sun", 'SUN'); light_data.energy = 1.6; light = bpy.data.objects.new("sun", light_data); scene.collection.objects.link(light); light.rotation_euler = (math.radians(35), math.radians(20), 0)
scene.view_settings.view_transform = 'Standard'; scene.render.filepath = out; bpy.ops.render.render(write_still=True); print("RENDERED", out)
