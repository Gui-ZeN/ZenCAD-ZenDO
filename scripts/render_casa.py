# -*- coding: utf-8 -*-
# R36 (protótipo) — "O Fotógrafo": Casa Ensō no Cycles, headless.
# Uso: blender --background --factory-startup --python render_casa.py -- <gltf> <out.png> <cx> <cy> <cz> <tx> <ty> <tz>
import bpy, sys, math

argv = sys.argv[sys.argv.index("--") + 1:]
GLTF, OUT = argv[0], argv[1]
CAM = tuple(float(v) for v in argv[2:5])
TGT = tuple(float(v) for v in argv[5:8])

# cena limpa
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene

bpy.ops.import_scene.gltf(filepath=GLTF)

# ---- Cycles + GPU se houver ----
sc.render.engine = 'CYCLES'
sc.cycles.samples = 128
sc.cycles.use_denoising = True
prefs = bpy.context.preferences.addons['cycles'].preferences
gpu_ok = False
for dtype in ('OPTIX', 'CUDA'):
    try:
        prefs.compute_device_type = dtype
        prefs.get_devices()
        for dev in prefs.devices:
            dev.use = True
        if any(d.use and d.type != 'CPU' for d in prefs.devices):
            sc.cycles.device = 'GPU'
            gpu_ok = True
            break
    except Exception:
        continue
print("GPU:", gpu_ok)

# ---- céu Nishita: fim de tarde ----
world = bpy.data.worlds.new("Ceu")
world.use_nodes = True
sc.world = world
nt = world.node_tree
nt.nodes.clear()
sky = nt.nodes.new("ShaderNodeTexSky")
sky.sky_type = 'NISHITA'
sky.sun_elevation = math.radians(30)
sky.sun_rotation = math.radians(235)   # sol vindo de sudoeste (fachada acesa)
sky.sun_intensity = 1.0
bg = nt.nodes.new("ShaderNodeBackground")
out = nt.nodes.new("ShaderNodeOutputWorld")
nt.links.new(sky.outputs['Color'], bg.inputs['Color'])
nt.links.new(bg.outputs['Background'], out.inputs['Surface'])
sc.view_settings.exposure = -5.6        # Nishita é FÍSICO: compensa o filme

# chão infinito (o terreno é uma ilha; sem isso o horizonte fica preto)
bpy.ops.mesh.primitive_plane_add(size=400, location=(8, 5, -0.32))
chao = bpy.context.active_object
mch = bpy.data.materials.new("Chao")
mch.use_nodes = True
p = next(n for n in mch.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
p.inputs['Base Color'].default_value = (0.55, 0.52, 0.46, 1)
p.inputs['Roughness'].default_value = 0.9
chao.data.materials.append(mch)

# ---- materiais: água vira espelho, vidro vira vidro ----
def base_color(mat):
    if not mat.use_nodes:
        return None
    for n in mat.node_tree.nodes:
        if n.type == 'BSDF_PRINCIPLED':
            if n.inputs['Base Color'].is_linked:
                return None            # tem textura — deixa quieto
            c = n.inputs['Base Color'].default_value
            return (c[0], c[1], c[2]), n
    return None

for mat in bpy.data.materials:
    r = base_color(mat)
    if not r:
        continue
    (cr, cg, cb), node = r
    if cb > cr * 1.3 and cg > cr:              # os dois azuis da cena
        if cb < 0.4:                            # ÁGUA (escura): espelho úmido
            node.inputs['Roughness'].default_value = 0.03
            node.inputs['Metallic'].default_value = 0.2
            try:
                node.inputs['Transmission Weight'].default_value = 0.35
                node.inputs['IOR'].default_value = 1.33
            except KeyError:
                pass
        else:                                   # VIDRO das janelas
            node.inputs['Roughness'].default_value = 0.02
            try:
                node.inputs['Transmission Weight'].default_value = 0.9
                node.inputs['IOR'].default_value = 1.45
            except KeyError:
                pass

# rugosidade honesta pro resto (reboco/telha/madeira ficam foscos)
for mat in bpy.data.materials:
    if mat.use_nodes:
        for n in mat.node_tree.nodes:
            if n.type == 'BSDF_PRINCIPLED' and n.inputs['Base Color'].is_linked:
                n.inputs['Roughness'].default_value = 0.75

# ---- câmera ----
cam_data = bpy.data.cameras.new("Cam")
cam_data.lens = 32
cam = bpy.data.objects.new("Cam", cam_data)
sc.collection.objects.link(cam)
cam.location = CAM
tgt = bpy.data.objects.new("Alvo", None)
tgt.location = TGT
sc.collection.objects.link(tgt)
tr = cam.constraints.new('TRACK_TO')
tr.target = tgt
tr.track_axis = 'TRACK_NEGATIVE_Z'
tr.up_axis = 'UP_Y'
sc.camera = cam

# ---- render ----
sc.render.resolution_x = 1920
sc.render.resolution_y = 1080
sc.view_settings.view_transform = 'AgX'
sc.view_settings.look = 'AgX - Medium High Contrast'
sc.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("RENDER OK:", OUT)
