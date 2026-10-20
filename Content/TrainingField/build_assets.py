import os, math, random, json, zipfile
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy.ndimage import gaussian_filter
import trimesh
from trimesh.visual.material import PBRMaterial
from trimesh.visual import TextureVisuals

ROOT=Path('/mnt/data/training_field_assets')
MODELS=ROOT/'Models'/'GLB'
OBJ=ROOT/'Models'/'OBJ'
FOL=ROOT/'Textures'/'Foliage'
TOWER=ROOT/'Textures'/'Tower'
rng=np.random.default_rng(35159)

# Compact, original, procedural art made for the training-field PoC.
def leaf_texture(filename, base, shape='oval'):
    size=512
    im=Image.new('RGBA',(size,size),(0,0,0,0))
    px=np.asarray(im).copy()
    y,x=np.mgrid[0:size,0:size]
    xx=(x-size*.5)/(size*.45); yy=(y-size*.5)/(size*.44)
    if shape=='needle':
        width=0.78 * np.maximum(0, 1-np.abs(yy)**1.3)
        edge=(np.abs(xx) < width*.55)
        # small needles taper down into a serrated outline
        edge &= ((np.cos(yy*30)+np.sin(xx*30)) > -.9)
    else:
        # pointed oval leaf with soft lobes, no solid corners
        outline= (np.abs(xx) < np.maximum(0, (1-np.abs(yy)**1.50))**.68)
        edge=outline & ((np.abs(yy)<.96))
    noise=gaussian_filter(rng.normal(0,1,(size,size)).astype('float32'),2.2)
    vein=np.exp(-(xx/(.028+.013*np.abs(yy)))**2)
    cols=np.zeros((size,size,4),np.uint8)
    vals=np.stack([np.clip(base[i]*(0.89+.2*((yy+1)/2)+noise*.05+vein*.10),0,255) for i in range(3)],axis=-1)
    cols[:,:,:3]=vals.astype('uint8'); cols[:,:,3]=np.where(edge,255,0)
    im=Image.fromarray(cols,'RGBA')
    # soften UV boundaries only enough for alpha-testing to retain the silhouette
    im.save(FOL/filename)

leaf_texture('leaf_wide.png',(62,114,40))
leaf_texture('leaf_light.png',(85,142,51))
leaf_texture('leaf_dark.png',(43,93,45))
leaf_texture('needle.png',(47,92,57),'needle')

# UV tiling trunk textures made without directional light.
def bark_tex(filename, base, birch=False):
    S=512
    x,y=np.meshgrid(np.arange(S),np.arange(S))
    n=gaussian_filter(rng.normal(0,1,(S,S)).astype(np.float32),(3,0.7))
    fine=gaussian_filter(rng.normal(0,1,(S,S)).astype(np.float32),(.65,1.4))
    lines=(np.sin(x*.34+n*8)>0.82).astype(np.float32)
    if birch:
        cracks=((np.sin(y*.45 + np.sin(x*.1))>.91) & (n>.03)).astype(np.float32)
        val=np.clip(.87 + n*.14 - cracks*.75 - lines*.08, 0.12,1)
        color=np.stack([234*val,230*val,214*val],-1)
    else:
        val=np.clip(1 + n*.25 +fine*.12-lines*.20,.5,1.4)
        color=np.stack([base[i]*val for i in range(3)],-1)
    Image.fromarray(np.uint8(np.clip(color,0,255)),'RGB').save(FOL/filename)

bark_tex('bark_oak.png',(117,88,64))
bark_tex('bark_birch.png',(232,227,212),True)
bark_tex('bark_pine.png',(93,69,53))

# Powder coated steel, concrete, and timber photoreal-ish procedural colors.
def surface(filename, color, destination, flecks=.065):
    S=512
    noise=gaussian_filter(rng.normal(0,1,(S,S)).astype(np.float32),1.1)
    base=np.stack([np.clip(c*(1 + noise*flecks),0,255) for c in color],-1)
    Image.fromarray(base.astype(np.uint8),'RGB').save(destination/filename)
surface('steel_grey.png',(101,110,116),TOWER)
surface('roof_dull.png',(63,76,77),TOWER)
surface('concrete.png',(153,148,133),TOWER,.14)

# PBR ground maps: tileable multi-frequency random texture, normal from height field.
def periodic_noise(S, sigmas, coefs):
    val=np.zeros((S,S),dtype=np.float32)
    for sigma,coeff in zip(sigmas,coefs):
        u=gaussian_filter(rng.normal(0,1,(S,S)).astype(np.float32), sigma=sigma, mode='wrap')
        u/=max(np.std(u),1e-6)
        val+=u*coeff
    return val

def ground(dirname,typ):
    S=768 # generate efficient base then upsample to 2K outputs
    n=periodic_noise(S,[.8,2.6,8,27],[.27,.40,.25,.14])
    smooth=periodic_noise(S,[5,23,60],[.18,.23,.13])
    yy,xx=np.mgrid[0:S,0:S]
    if typ=='grass':
        # Mottled green field; stochastic thin grass blades in a tileable base layer.
        base=np.stack([100 + n*16 + smooth*9,128 + n*19 + smooth*12,64 + n*12 + smooth*9],-1)
        heights=.32*n + .23*smooth
        rough=np.clip(.88+.05*n,.73,.96)
    else:
        pebbles=np.clip(n-.40,0,3)
        base=np.stack([136+n*15+smooth*13+pebbles*9,108+n*13+smooth*10+pebbles*9,76+n*10+smooth*9+pebbles*10],-1)
        heights=.32*n+.24*smooth+.15*pebbles
        rough=np.clip(.91+.035*n,.79,.99)
    Image.fromarray(np.uint8(np.clip(base,0,255)),'RGB').resize((2048,2048),Image.Resampling.BICUBIC).save(dirname/'BaseColor.png')
    # Rescale height for stable normal generation. Texture intended as detail normal, not topographic scale.
    dy,dx=np.gradient(gaussian_filter(heights,.8,mode='wrap'))
    strength=2.2 if typ=='dirt' else 2.7
    nx=-dx*strength; ny=dy*strength; nz=np.ones_like(nx)
    norm=np.sqrt(nx*nx+ny*ny+nz*nz)
    normal=np.uint8(np.clip((np.stack([nx/norm,ny/norm,nz/norm],-1)*.5+.5)*255,0,255))
    Image.fromarray(normal,'RGB').resize((2048,2048),Image.Resampling.BICUBIC).save(dirname/'Normal_DX.png')
    Image.fromarray(np.uint8(rough*255),'L').resize((2048,2048),Image.Resampling.BICUBIC).save(dirname/'Roughness.png')
    # AO only at microscopic recesses; avoid lighting baked into albedo.
    occl=np.clip(.98-np.maximum(0,-heights)*.035,.79,1)
    Image.fromarray(np.uint8(occl*255),'L').resize((2048,2048),Image.Resampling.BICUBIC).save(dirname/'AO.png')
    # Thumbnail, not for mapping
    thumb=Image.open(dirname/'BaseColor.png'); thumb.thumbnail((512,512)); thumb.save(dirname/'preview.png')
    print('ground',typ)

ground(ROOT/'Textures'/'Ground_Grass','grass')
ground(ROOT/'Textures'/'Ground_Dirt','dirt')
Image.fromarray(np.zeros((32,32),dtype=np.uint8),'L').save(ROOT/'Textures'/'Metallic_0.png')

class MeshBuild:
    def __init__(self): self.v=[]; self.uv=[]; self.f=[]
    def vertices(self,p,uv,faces):
        base=len(self.v); self.v.extend(np.asarray(p,dtype=float).tolist())
        self.uv.extend(np.asarray(uv,dtype=float).tolist())
        self.f.extend((np.asarray(faces,dtype=int)+base).tolist())
    def frustum(self,a,b,r0,r1,segments=10,tiles=1):
        a=np.array(a,float);b=np.array(b,float);axis=b-a; length=np.linalg.norm(axis)
        if length<1e-5: return
        d=axis/length; ref=np.array([1.,0.,0.]) if abs(d[0])<.85 else np.array([0.,1.,0.]); u=np.cross(d,ref);u/=np.linalg.norm(u);v=np.cross(d,u)
        p=[];uv=[];f=[]
        for t,(pos,r) in enumerate([(a,r0),(b,r1)]):
            for j in range(segments+1):
                th=j*math.tau/segments
                p.append(pos + r*(math.cos(th)*u+math.sin(th)*v));uv.append([j/segments*1.0,t*tiles])
        for j in range(segments):
            i=j;k=j+segments+1
            f.extend([[i,i+1,k],[i+1,k+1,k]])
        self.vertices(p,uv,f)
    def ellipsoid(self,center,radii,subdiv=1):
        mesh=trimesh.creation.icosphere(subdivisions=subdiv, radius=1)
        v=mesh.vertices*np.array(radii)+np.array(center)
        uv=np.zeros((len(v),2),float)
        self.vertices(v,uv,mesh.faces)
    def leaf(self,center,lv,wd,normal,orient=0):
        cen=np.array(center,float);N=np.array(normal,float);N/=max(np.linalg.norm(N),1e-10)
        tmp=np.array([0.,0.,1.]) if abs(N[2])<.88 else np.array([1.,0.,0.]);U=np.cross(N,tmp);U/=np.linalg.norm(U);V=np.cross(N,U)
        u=U*math.cos(orient)+V*math.sin(orient); v=-U*math.sin(orient)+V*math.cos(orient)
        corners=[cen-u*wd*.5-v*lv*.5,cen+u*wd*.5-v*lv*.5,cen+u*wd*.5+v*lv*.5,cen-u*wd*.5+v*lv*.5]
        self.vertices(corners,[[0,0],[1,0],[1,1],[0,1]],[[0,1,2],[0,2,3]])
    def box(self,center,size):
        m=trimesh.creation.box(extents=size);m.apply_translation(center)
        self.vertices(m.vertices,(m.vertices[:,[0,2]]*.15)%1.0,m.faces)
    def beam(self,a,b,w=.08,depth=None):
        a=np.array(a,float);b=np.array(b,float);v=b-a;L=np.linalg.norm(v)
        if L<1e-8: return
        mesh=trimesh.creation.box(extents=[w,depth if depth is not None else w,L])
        T=trimesh.geometry.align_vectors([0,0,1],v/L)
        mesh.apply_transform(T);mesh.apply_translation((a+b)*.5)
        self.vertices(mesh.vertices,(mesh.vertices[:,[0,2]]*.15)%1.0,mesh.faces)
    def mesh(self,material):
        if not self.f:return None
        mesh=trimesh.Trimesh(vertices=np.array(self.v),faces=np.array(self.f),process=False)
        mesh.visual=TextureVisuals(uv=np.asarray(self.uv),material=material)
        return mesh

LEAFMAT={
 'oak':PBRMaterial(name='M_Leaf_Oak',baseColorTexture=Image.open(FOL/'leaf_wide.png'),roughnessFactor=.9,metallicFactor=0,doubleSided=True,alphaMode='MASK',alphaCutoff=.5),
 'birch':PBRMaterial(name='M_Leaf_Birch',baseColorTexture=Image.open(FOL/'leaf_light.png'),roughnessFactor=.92,metallicFactor=0,doubleSided=True,alphaMode='MASK',alphaCutoff=.5),
 'bush':PBRMaterial(name='M_Leaf_Shrub',baseColorTexture=Image.open(FOL/'leaf_dark.png'),roughnessFactor=.88,metallicFactor=0,doubleSided=True,alphaMode='MASK',alphaCutoff=.5),
 'spruce':PBRMaterial(name='M_Needles',baseColorTexture=Image.open(FOL/'needle.png'),roughnessFactor=.94,metallicFactor=0,doubleSided=True,alphaMode='MASK',alphaCutoff=.48),
}
BARKMAT={
 'oak':PBRMaterial(name='M_Bark_Oak',baseColorTexture=Image.open(FOL/'bark_oak.png'),roughnessFactor=.95,metallicFactor=0),
 'birch':PBRMaterial(name='M_Bark_Birch',baseColorTexture=Image.open(FOL/'bark_birch.png'),roughnessFactor=.93,metallicFactor=0),
 'spruce':PBRMaterial(name='M_Bark_Pine',baseColorTexture=Image.open(FOL/'bark_pine.png'),roughnessFactor=.96,metallicFactor=0),
 'bush':PBRMaterial(name='M_Bark_Shrub',baseColorTexture=Image.open(FOL/'bark_oak.png'),roughnessFactor=.97,metallicFactor=0)
}
COREMAT={
 'oak':PBRMaterial(name='M_Canopy_Oak',baseColorFactor=[66,106,44,255],roughnessFactor=.95,metallicFactor=0,doubleSided=True),
 'birch':PBRMaterial(name='M_Canopy_Birch',baseColorFactor=[89,136,53,255],roughnessFactor=.95,metallicFactor=0,doubleSided=True),
 'bush':PBRMaterial(name='M_Canopy_Shrub',baseColorFactor=[54,94,46,255],roughnessFactor=.95,metallicFactor=0,doubleSided=True),
 'spruce':PBRMaterial(name='M_Canopy_Spruce',baseColorFactor=[37,72,47,255],roughnessFactor=.95,metallicFactor=0,doubleSided=True)
}
METALMAT=PBRMaterial(name='M_Powder_Coated_Steel',baseColorTexture=Image.open(TOWER/'steel_grey.png'),metallicFactor=.35,roughnessFactor=.67)
ROOFMAT=PBRMaterial(name='M_Roof',baseColorTexture=Image.open(TOWER/'roof_dull.png'),metallicFactor=.22,roughnessFactor=.78)
CONCRETE=PBRMaterial(name='M_Concrete',baseColorTexture=Image.open(TOWER/'concrete.png'),metallicFactor=0,roughnessFactor=.91)

def randpoint_ellipsoid(center,rad):
    direction=rng.normal(size=3);direction/=np.linalg.norm(direction)
    radius=rng.uniform(.38,1.)**(1/3)
    return np.array(center)+direction*np.array(rad)*radius

# A bush has organic stem branches, opaque canopy masses plus leaf-card micro geometry.
def bush_model(which):
    cfg={1:(1.75,1.05,.8,'bush'),2:(1.3,1.25,1.16,'oak'),3:(1.52,1.90,.93,'birch')}[which]
    width,height,depth,leafkey=cfg
    stems=MeshBuild();cores=MeshBuild();leaves=MeshBuild()
    count={1:6,2:9,3:11}[which]
    centers=[]
    for i in range(count):
        dx=rng.normal(0,width*.21);dy=rng.normal(0,depth*.22)
        zh=float(rng.uniform(height*.48,height*.91)); end=(dx,dy,zh)
        stems.frustum([0,0,0],end,.028, .006,segments=6)
        r=(rng.uniform(width*.16,width*.27),rng.uniform(depth*.19,depth*.28),rng.uniform(height*.18,height*.30))
        centers.append((end,r));cores.ellipsoid(end,r,subdiv=1)
    for end,rad in centers:
        for i in range(85):
            p=randpoint_ellipsoid(end,rad)
            n=rng.normal(size=3)
            leaves.leaf(p,rng.uniform(.13,.23),rng.uniform(.06,.12),n,rng.uniform(0,math.tau))
    return {'trunk':(stems,BARKMAT['bush']), 'canopy':(cores,COREMAT[leafkey]),'leaves':(leaves,LEAFMAT[leafkey])}

def tree_deciduous(kind):
    if kind=='oak':
        height=8.9;trunk_h=4.3; crown=[(0,0,6.5)]; spread=(2.3,2.1,2.2); arms=8; leafnum=135
    else:
        height=10.9;trunk_h=6.5; crown=[(0,0,8.1)]; spread=(1.65,1.48,2.65);arms=12;leafnum=112
    stems=MeshBuild();cores=MeshBuild();leaves=MeshBuild()
    stems.frustum([0,0,0],[0,0,trunk_h],.28 if kind=='oak' else .155,.11 if kind=='oak' else .07,segments=12,tiles=5)
    centers=[]
    for i in range(arms):
        th=i*math.tau/arms+rng.uniform(-.37,.37)
        dr=rng.uniform(.85,1.65) if kind=='oak' else rng.uniform(.45,1.10)
        end=(math.cos(th)*dr,math.sin(th)*dr,rng.uniform(height*.61,height*.91))
        stems.frustum([0,0,trunk_h*.66],end,.10 if kind=='oak' else .047,.022,segments=7,tiles=3)
        r=(rng.uniform(spread[0]*.38,spread[0]*.67),rng.uniform(spread[1]*.38,spread[1]*.67),rng.uniform(spread[2]*.30,spread[2]*.49))
        centers.append((end,r));cores.ellipsoid(end,r,subdiv=1)
    crown=(0,0,height*.78)
    r=(spread[0]*.58,spread[1]*.58,spread[2]*.38)
    centers.append((crown,r));cores.ellipsoid(crown,r,subdiv=1)
    for end,rad in centers:
        for i in range(leafnum):
            p=randpoint_ellipsoid(end,rad)
            n=rng.normal(size=3)
            leaves.leaf(p,rng.uniform(.23,.44) if kind=='oak' else rng.uniform(.16,.31),rng.uniform(.10,.22),n,rng.uniform(0,math.tau))
    return {'trunk':(stems,BARKMAT[kind]),'canopy':(cores,COREMAT[kind]),'leaves':(leaves,LEAFMAT[kind])}

def spruce_model():
    H=12.2; stems=MeshBuild();core=MeshBuild();needle=MeshBuild()
    stems.frustum([0,0,0],[0,0,H],.21,.026,segments=10,tiles=9)
    # Vertical silhouette consists of tapering branch clusters, no hard plastic-looking cones.
    for layer in range(18):
        z=1.42 + layer*.58
        base_radius=2.24*(1-z/H)**.88
        for b in range(7):
            th=b*math.tau/7 + (layer%2)*math.pi/8 + rng.uniform(-.10,.10)
            radius=base_radius*rng.uniform(.78,1.17)
            tip=np.array([math.cos(th)*radius,math.sin(th)*radius,z-.33])
            a=[0,0,z+.07]
            stems.frustum(a,tip,.044,.008,segments=6)
            cen=tip*.68 + np.array([0,0,.14]);r=(radius*.48,radius*.25,.31)
            # drooping needle canopy tuft
            core.ellipsoid(cen, (r[0],r[1],r[2]),subdiv=0)
            for _ in range(9):
                p=randpoint_ellipsoid(cen,(r[0],r[1],r[2]));n=rng.normal(size=3)
                needle.leaf(p,rng.uniform(.33,.58),rng.uniform(.055,.14),n,rng.uniform(0,math.tau))
    return {'trunk':(stems,BARKMAT['spruce']),'canopy':(core,COREMAT['spruce']),'leaves':(needle,LEAFMAT['spruce'])}

def tower_model():
    steel=MeshBuild();roof=MeshBuild();concrete=MeshBuild()
    h=12.2
    def corner(i,z):
        th=math.pi*.25+i*math.pi*.5
        radius=2.0 -(z/h)*.65
        return np.array([math.cos(th)*radius,math.sin(th)*radius,z])
    for i in range(4):
        concrete.box(corner(i,0),(.85,.85,.28))
        steel.beam(corner(i,.1),corner(i,h),.13,.13)
        # cross-braces arranged in bays all around four faces
        for z in [0.9,3.6,6.3,9.0]:
            steel.beam(corner(i,z),corner((i+1)%4,z+2.6),.052)
            steel.beam(corner((i+1)%4,z),corner(i,z+2.6),.052)
        for z in [1.2,4,6.9,9.7,12.05]:
            steel.beam(corner(i,z),corner((i+1)%4,z),.082)
    # platform and kickboard
    steel.box([0,0,12.2],(3.42,3.42,.15))
    for x in [-1.52,1.52]:
        for y in [-1.52,1.52]:
            steel.beam([x,y,12.3],[x,y,13.48],.067)
    for z in [12.7,13.45]:
        for x in [-1.52,1.52]:steel.beam([x,-1.52,z],[x,1.52,z],.045)
        for y in [-1.52,1.52]:steel.beam([-1.52,y,z],[1.52,y,z],.045)
    # roof slanted sheets supported by poles
    roof.box([0,0,14.0],(3.65,3.65,.15))
    for x in [-1.55,1.55]:
        for y in [-1.55,1.55]: steel.beam([x,y,13.45],[x,y,13.98],.055)
    # access ladder along southern face
    for x in [-.38,.38]:steel.beam([x,-1.67,.2],[x,-1.67,12.1],.06)
    for z in np.arange(.6,12,.30):steel.beam([-.38,-1.67,z],[.38,-1.67,z],.043)
    # short radio mast as recognisable structure
    steel.beam([0,0,14.0],[0,0,17.7],.072)
    for z in [15.4,16.8,17.65]:
        steel.beam([-.46,0,z],[.46,0,z],.024)
        steel.beam([0,-.42,z],[0,.42,z],.024)
    return {'steel':(steel,METALMAT),'roof':(roof,ROOFMAT),'concrete':(concrete,CONCRETE)}

# Writing GLB with Z-up geometry transformed to glTF Y-up; OBJ is kept Z-up for classic Unreal import.
def export_obj(path,objects):
    with open(path,'w') as f:
        f.write('# Training field asset; coordinates in metres; Z-up, ground pivot at (0,0,0)\n')
        f.write('mtllib '+path.stem+'.mtl\n')
        vi=0
        for key,(builder,material) in objects.items():
            if not builder.f:continue
            f.write('\no '+key+'\nusemtl '+material.name+'\n')
            for v in builder.v:f.write('v %.6f %.6f %.6f\n'%tuple(v))
            for uv in builder.uv:f.write('vt %.6f %.6f\n'%tuple(uv))
            for a,b,c in builder.f:
                f.write('f %d/%d %d/%d %d/%d\n'%tuple(t for i in [a,b,c] for t in (i+vi+1,i+vi+1)))
            vi+=len(builder.v)
    with open(path.with_suffix('.mtl'),'w') as f:
        f.write('# Materials: link to ../../Textures/Foliage or ../../Textures/Tower\n')
        for key,(builder,material) in objects.items():
            f.write('\nnewmtl '+material.name+'\nKd 0.7 0.7 0.7\nKs 0.0 0.0 0.0\nNs 5\n')
            if material.baseColorTexture is not None:
                matches=list(FOL.glob('*.png'))+list(TOWER.glob('*.png'))
                fn=next((q for q in matches if q.name.lower().startswith(material.name.lower().split('M_')[-1].split('_')[0])),None)
                # Explicit deterministic material mapping
                mm={'M_Bark_Oak':'bark_oak.png','M_Bark_Birch':'bark_birch.png','M_Bark_Pine':'bark_pine.png','M_Bark_Shrub':'bark_oak.png',
                    'M_Leaf_Oak':'leaf_wide.png','M_Leaf_Birch':'leaf_light.png','M_Leaf_Shrub':'leaf_dark.png','M_Needles':'needle.png',
                    'M_Powder_Coated_Steel':'steel_grey.png','M_Roof':'roof_dull.png','M_Concrete':'concrete.png'}
                nm=mm.get(material.name)
                if nm:
                    category='Tower' if material.name in {'M_Powder_Coated_Steel','M_Roof','M_Concrete'} else 'Foliage'
                    f.write('map_Kd ../../Textures/'+category+'/'+nm+'\n')
                    if material.alphaMode=='MASK':f.write('map_d ../../Textures/'+category+'/'+nm+'\n')

def export_asset(name,objects):
    scene=trimesh.Scene()
    stats={}
    for k,(builder,material) in objects.items():
        mesh=builder.mesh(material)
        if mesh is None:continue
        # Rotate -90 about X from internal Z-up to glTF Y-up.
        rot=trimesh.transformations.rotation_matrix(-np.pi/2,[1,0,0])
        mesh.apply_transform(rot)
        scene.add_geometry(mesh,geom_name=k,node_name=k)
        stats[k]=int(len(mesh.faces))
    path=MODELS/(name+'.glb')
    path.write_bytes(trimesh.exchange.gltf.export_glb(scene))
    from Docs.merge_glb_primitives import unify
    unify(path)
    export_obj(OBJ/(name+'.obj'),objects)
    print(name,sum(stats.values()),path.stat().st_size)
    return stats

counts={}
for i in [1,2,3]: counts[f'Bush_{i:02d}']=export_asset(f'Bush_{i:02d}',bush_model(i))
counts['Tree_01_Oak']=export_asset('Tree_01_Oak',tree_deciduous('oak'))
counts['Tree_02_Birch']=export_asset('Tree_02_Birch',tree_deciduous('birch'))
counts['Tree_03_Spruce']=export_asset('Tree_03_Spruce',spruce_model())
counts['Tower_01_Observation']=export_asset('Tower_01_Observation',tower_model())
(ROOT/'Docs'/'mesh_stats.json').write_text(json.dumps(counts,ensure_ascii=False,indent=2))
print('Finished.')
