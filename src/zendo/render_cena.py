# -*- coding: utf-8 -*-
# R36 — "O Fotógrafo" do Zendo: cena Cycles montada headless a partir da
# vista corrente do viewport (câmera + sol da Bandeja). Este arquivo é um
# ASSET do app (deploy em assets/render_cena.py) e roda DENTRO do Blender:
#   blender --background --factory-startup --python render_cena.py -- \
#       <cena.gltf> <saida.png> <ex> <ey> <ez> <tx> <ty> <tz> \
#       <sol_elev_graus> <sol_azim_graus> <fov_y_graus> <largura> <altura>
# Fotometria (lições do protótipo): o céu Nishita é FÍSICO → exposure -5.6;
# o terreno é uma ilha → chão infinito; AgX pro tonemapping.
import bpy, sys, math, os

argv = sys.argv[sys.argv.index("--") + 1:]
GLTF, OUT = argv[0], argv[1]
EYE = tuple(float(v) for v in argv[2:5])
TGT = tuple(float(v) for v in argv[5:8])
ELEV, AZIM, FOVY = float(argv[8]), float(argv[9]), float(argv[10])
RESX, RESY = int(argv[11]), int(argv[12])
SAMPLES = int(argv[13]) if len(argv) > 13 else 128
INTERIOR = int(argv[14]) if len(argv) > 14 else 0
HDRI = argv[15] if len(argv) > 15 else ""     # R46: céu real ("" = Nishita)

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
bpy.ops.import_scene.gltf(filepath=GLTF)

# ---- Cycles + GPU se houver (OPTIX → CUDA → CPU) ----
sc.render.engine = 'CYCLES'
sc.cycles.samples = SAMPLES
sc.cycles.use_denoising = True
# POLÍTICA DELIBERADA (R46, reafirmada na R49) — não é workaround esquecido.
# Nasceu de um OOM real (1080p+HDRI numa GPU de 6 GB dividindo VRAM com o
# desktop; o OIDN na GPU disputa memória com o render e já reclamava até no
# céu Nishita), mas fica valendo para TODAS as máquinas de propósito:
#   · o Fotógrafo tira UMA foto, não é render farm — o denoise na CPU custa
#     segundos, e tile 512 num still 1080p custa poucos por cento;
#   · a alternativa "detectar VRAM e decidir" é adivinhação: o Cycles não
#     expõe VRAM livre, ler nvidia-smi seria parsear stdout (a armadilha que
#     a R47 recusou no curl), e VRAM compartilhada/laptop mente;
#   · "tentar GPU e cair pra CPU no erro" não funciona: OOM no Cycles às
#     vezes derruba o processo inteiro — o except nunca roda — e no caso
#     recuperável custa um render perdido.
# Confiabilidade em 100% das máquinas > a última gota de quem tem 24 GB.
try:
    sc.cycles.denoising_use_gpu = False
except AttributeError:
    pass
sc.cycles.use_auto_tile = True
sc.cycles.tile_size = 512
prefs = bpy.context.preferences.addons['cycles'].preferences
for dtype in ('OPTIX', 'CUDA'):
    try:
        prefs.compute_device_type = dtype
        prefs.get_devices()
        for dev in prefs.devices:
            dev.use = True
        if any(d.use and d.type != 'CPU' for d in prefs.devices):
            sc.cycles.device = 'GPU'
            break
    except Exception:
        continue

# ---- céu: HDRI real (R46) ou físico Nishita com o sol da Bandeja ----
world = bpy.data.worlds.new("Ceu")
world.use_nodes = True
sc.world = world
nt = world.node_tree
nt.nodes.clear()
bg = nt.nodes.new("ShaderNodeBackground")
out = nt.nodes.new("ShaderNodeOutputWorld")
usa_hdri = bool(HDRI) and HDRI != "-" and os.path.isfile(HDRI)
if usa_hdri:
    # céu fotografado: a luz e o horizonte vêm da imagem; o azimute pedido
    # vira rotação do mundo (a elevação do sol é a da foto — fixa).
    env = nt.nodes.new("ShaderNodeTexEnvironment")
    env.image = bpy.data.images.load(HDRI)
    mapn = nt.nodes.new("ShaderNodeMapping")
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mapn.inputs['Rotation'].default_value[2] = math.radians(180.0 - AZIM)
    nt.links.new(tc.outputs['Generated'], mapn.inputs['Vector'])
    nt.links.new(mapn.outputs['Vector'], env.inputs['Vector'])
    nt.links.new(env.outputs['Color'], bg.inputs['Color'])
    try:                       # mapa de importância menor = menos VRAM
        world.cycles.sample_map_resolution = 512
    except AttributeError:
        pass
else:
    sky = nt.nodes.new("ShaderNodeTexSky")
    sky.sky_type = 'NISHITA'
    sky.sun_elevation = math.radians(max(3.0, ELEV))
    sky.sun_rotation = math.radians(AZIM)
    sky.sun_intensity = 1.0
    sky.air_density = 1.1
    sky.dust_density = 0.9
    nt.links.new(sky.outputs['Color'], bg.inputs['Color'])
nt.links.new(bg.outputs['Background'], out.inputs['Surface'])
# R40: INTERIOR = filme mais sensível (a luz que entra pela janela é fração
# da do céu aberto); exterior mantém a fotometria física de sempre.
# R46: HDRIs do Poly Haven são normalizados — fotometria própria (calibrada).
if usa_hdri:
    sc.view_settings.exposure = 0.2 if INTERIOR else -0.4
else:
    sc.view_settings.exposure = -1.7 if INTERIOR else -5.4

# ---- chão infinito (washi) ----
bpy.ops.mesh.primitive_plane_add(size=600, location=(TGT[0], TGT[1], -0.33))
chao = bpy.context.active_object
mch = bpy.data.materials.new("Chao")
mch.use_nodes = True
p = next(n for n in mch.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
p.inputs['Base Color'].default_value = (0.55, 0.52, 0.46, 1)
p.inputs['Roughness'].default_value = 0.9
chao.data.materials.append(mch)

# ---- texturas: fosco honesto + relevo de graça (bump da própria imagem) ----
for mat in bpy.data.materials:
    if not mat.use_nodes:
        continue
    for n in mat.node_tree.nodes:
        if n.type != 'BSDF_PRINCIPLED':
            continue
        if n.inputs['Base Color'].is_linked:
            n.inputs['Roughness'].default_value = 0.75
            src = n.inputs['Base Color'].links[0].from_node
            if src.type == 'TEX_IMAGE':
                bump = mat.node_tree.nodes.new('ShaderNodeBump')
                bump.inputs['Strength'].default_value = 0.35
                bump.inputs['Distance'].default_value = 0.02
                mat.node_tree.links.new(src.outputs['Color'],
                                        bump.inputs['Height'])
                mat.node_tree.links.new(bump.outputs['Normal'],
                                        n.inputs['Normal'])

# ---- R44: cor verdadeira nos vertex colors ----
# O Zendo grava as cores sRGB CRUAS no COLOR_0, mas o glTF especifica o
# atributo como LINEAR — o Cycles lia o valor cru como linear e TUDO saía
# lavado (as copas das árvores denunciaram). Gamma 2.2 depois do nó de
# atributo devolve a cor que o usuário viu no viewport. A detecção de
# vidro/água abaixo lê o ATRIBUTO da malha (cru) — não é afetada.
for mat in bpy.data.materials:
    if not mat.use_nodes:
        continue
    nt = mat.node_tree
    for n in list(nt.nodes):
        if n.type not in ('VERTEX_COLOR', 'ATTRIBUTE'):
            continue
        out = n.outputs.get('Color')
        if not out:
            continue
        alvos = [lk for lk in list(nt.links) if lk.from_socket == out]
        if not alvos:
            continue
        g = nt.nodes.new('ShaderNodeGamma')
        g.inputs['Gamma'].default_value = 2.2
        nt.links.new(out, g.inputs['Color'])
        for lk in alvos:
            dest = lk.to_socket
            nt.links.remove(lk)
            nt.links.new(g.outputs['Color'], dest)

# ---- VIDRO e ÁGUA de verdade (R39): as cores do Zendo viajam como VERTEX
# COLORS num material único — então a troca é POR OBJETO, pela cor média.
def novo_material(nome, base, transm, rough, ior):
    m = bpy.data.materials.new(nome)
    m.use_nodes = True
    p = next(x for x in m.node_tree.nodes if x.type == 'BSDF_PRINCIPLED')
    p.inputs['Base Color'].default_value = (*base, 1.0)
    p.inputs['Roughness'].default_value = rough
    try:
        p.inputs['Transmission Weight'].default_value = transm
        p.inputs['IOR'].default_value = ior
    except KeyError:
        pass
    return m

MAT_VIDRO = novo_material("ZenVidro", (0.90, 0.96, 0.97), 1.0, 0.03, 1.45)
MAT_AGUA = novo_material("ZenAgua", (0.18, 0.45, 0.48), 0.55, 0.03, 1.33)
REF_VIDRO = (0.659, 0.780, 0.816)     # (168,199,208)/255 — sondado no glTF
REF_AGUA = (0.337, 0.553, 0.608)      # (86,141,155)/255

def perto(c, ref, tol=0.05):
    return all(abs(c[i] - ref[i]) < tol for i in range(3))

for ob in bpy.data.objects:
    if ob.type != 'MESH' or not ob.data.color_attributes:
        continue
    ca = ob.data.color_attributes[0]
    n = len(ca.data)
    if not n:
        continue
    med = tuple(sum(d.color[i] for d in ca.data) / n for i in range(3))
    if perto(med, REF_VIDRO):
        ob.data.materials.clear()
        ob.data.materials.append(MAT_VIDRO)
    elif perto(med, REF_AGUA):
        ob.data.materials.clear()
        ob.data.materials.append(MAT_AGUA)

# ---- câmera = a vista do viewport ----
cam_data = bpy.data.cameras.new("Cam")
cam_data.sensor_fit = 'VERTICAL'
cam_data.angle_y = math.radians(FOVY)
cam = bpy.data.objects.new("Cam", cam_data)
sc.collection.objects.link(cam)
cam.location = EYE
alvo = bpy.data.objects.new("Alvo", None)
alvo.location = TGT
sc.collection.objects.link(alvo)
tr = cam.constraints.new('TRACK_TO')
tr.target = alvo
tr.track_axis = 'TRACK_NEGATIVE_Z'
tr.up_axis = 'UP_Y'
cam_data.dof.use_dof = True
cam_data.dof.focus_object = alvo
cam_data.dof.aperture_fstop = 9.0
sc.camera = cam

# R40: modo INTERIOR — flash rebatido do fotógrafo: uma area light suave na
# posição da câmera (um pouco acima), apontando pro alvo. Preenche as sombras
# sem matar a direção da luz que entra pelas janelas.
if INTERIOR:
    li = bpy.data.lights.new("Preenchimento", type='AREA')
    li.size = 2.6
    li.energy = 650.0
    ob = bpy.data.objects.new("Preenchimento", li)
    ob.location = (EYE[0], EYE[1], min(EYE[2] + 0.8, 2.7))
    sc.collection.objects.link(ob)
    trl = ob.constraints.new('TRACK_TO')
    trl.target = alvo
    trl.track_axis = 'TRACK_NEGATIVE_Z'
    trl.up_axis = 'UP_Y'

# ---- render ----
sc.render.resolution_x = RESX
sc.render.resolution_y = RESY
sc.view_settings.view_transform = 'AgX'
sc.view_settings.look = 'AgX - Medium High Contrast'
sc.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("RENDER OK:", OUT)
