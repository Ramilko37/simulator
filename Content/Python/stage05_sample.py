"""Hand-composed first-gate grove. Offline assets; native Static Mesh Foliage."""
import unreal as u, json, math, random
from pathlib import Path
import stage05_environment as e
ROOT=e.ROOT; SRC=ROOT/'AssetsSource/Environment/Revision02'; BASE=e.BASE+'Sample'; EV=e.EV/'sample';EV.mkdir(exist_ok=True)
TREES=['SM_Tree_OakSample_'+c for c in 'ABC'];BUSHES=['SM_Bush_SampleLow','SM_Bush_SampleMid'];GRASS=['SM_Grass_SampleMeadow','SM_Grass_SampleDry'];NAMES=TREES+BUSHES+GRASS

def mesh(name):
 a=u.load_asset(BASE+'/Meshes/'+name+'/StaticMeshes/'+name);assert a,name;return a

def asset(name,cls,factory,folder='Materials'):
 path=BASE+'/'+folder+'/'+name
 a=u.load_asset(path)
 return a or e.TOOLS.create_asset(name,BASE+'/'+folder,cls,factory)

def tex(name,file,kind='color'):
 path=BASE+'/Textures/'+name;t=u.load_asset(path)
 if not t:
  task=u.AssetImportTask();task.filename=str(file);task.destination_path=BASE+'/Textures';task.destination_name=name;task.automated=True;task.save=False;e.TOOLS.import_asset_tasks([task]);t=u.load_asset(path)
 assert t
 t.set_editor_property('srgb',kind=='color');t.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP if kind=='normal' else u.TextureCompressionSettings.TC_MASKS if kind=='data' else u.TextureCompressionSettings.TC_DEFAULT)
 if kind=='normal':t.set_editor_property('flip_green_channel',True)
 if name in ['T_Oak','T_Ash']:
  t.set_editor_property('do_scale_mips_for_alpha_coverage',True);t.set_editor_property('alpha_coverage_thresholds',u.Vector4(0,0,0,.4))
 e.save(t);return t

def make_material(name,bc=None,normal=None,rough=None,leaf=False,grass=False,arm=False):
 path=BASE+'/Materials/'+name;existing=u.load_asset(path)
 if existing:e.M.delete_all_material_expressions(existing)
 m=asset(name,u.Material,u.MaterialFactoryNew());m.set_editor_property('two_sided',leaf or grass)
 if leaf or grass:m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
 if leaf:m.set_editor_property('blend_mode',u.BlendMode.BLEND_MASKED);m.set_editor_property('opacity_mask_clip_value',.4)
 def ex(cls,**kw):return e.expression(m,cls,**kw)
 color=ex(u.MaterialExpressionVertexColor) if grass else ex(u.MaterialExpressionTextureSample,texture=bc)
 assert e.M.connect_material_property(color,'' if grass else 'RGB',u.MaterialProperty.MP_BASE_COLOR)
 if leaf:
  # EZ-Tree uses Three.js flipY textures; match that convention without changing source atlases.
  uv=ex(u.MaterialExpressionTextureCoordinate,v_tiling=-1.0);assert e.M.connect_material_expressions(uv,'',color,'UVs')
  fade=ex(u.MaterialExpressionPerInstanceFadeAmount);mask=ex(u.MaterialExpressionMultiply)
  assert e.M.connect_material_expressions(color,'A',mask,'A');assert e.M.connect_material_expressions(fade,'',mask,'B')
  assert e.M.connect_material_property(mask,'',u.MaterialProperty.MP_OPACITY_MASK)
 if leaf or grass:
  amount=ex(u.MaterialExpressionConstant,r=.22);sub=ex(u.MaterialExpressionMultiply)
  e.M.connect_material_expressions(color,'' if grass else 'RGB',sub,'A');e.M.connect_material_expressions(amount,'',sub,'B');e.M.connect_material_property(sub,'',u.MaterialProperty.MP_SUBSURFACE_COLOR)
 if normal:
  n=ex(u.MaterialExpressionTextureSample,texture=normal,sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL);e.M.connect_material_property(n,'RGB',u.MaterialProperty.MP_NORMAL)
 if rough:
  r=ex(u.MaterialExpressionTextureSample,texture=rough,sampler_type=u.MaterialSamplerType.SAMPLERTYPE_MASKS);e.M.connect_material_property(r,'G' if arm else 'R',u.MaterialProperty.MP_ROUGHNESS)
  if arm:e.M.connect_material_property(r,'R',u.MaterialProperty.MP_AMBIENT_OCCLUSION)
 else:
  r=ex(u.MaterialExpressionConstant,r=.78);e.M.connect_material_property(r,'',u.MaterialProperty.MP_ROUGHNESS)
 zero=ex(u.MaterialExpressionConstant,r=0);e.M.connect_material_property(zero,'',u.MaterialProperty.MP_METALLIC)
 e.M.recompile_material(m);e.save(m);return m

def materials():
 src=SRC/'EZTree/src/app/public/textures';bark=src/'bark/Bark001_1K-JPG'
 oak=tex('T_Oak',src/'leaves/oak.png');ash=tex('T_Ash',src/'leaves/ash.png')
 bc=tex('T_Bark',bark/'Bark001_1K-JPG_Color.jpg');n=tex('T_Bark_N',bark/'Bark001_1K-JPG_NormalGL.jpg','normal');r=tex('T_Bark_R',bark/'Bark001_1K-JPG_Roughness.jpg','data')
 make_material('M_Bark',bc,n,r);make_material('M_Oak',oak,leaf=True);make_material('M_Ash',ash,leaf=True);make_material('M_Meadow',grass=True)
 rock=SRC/'Boulder01/textures';bc=tex('T_Rock',rock/'boulder_01_diff_2k.jpg');n=tex('T_Rock_N',rock/'boulder_01_nor_gl_2k.jpg','normal');r=tex('T_Rock_ARM',rock/'boulder_01_arm_2k.jpg','data');make_material('M_Rock',bc,n,r,arm=True)
 print('SAMPLE_MATERIALS_COMPLETE')

def configure():
 report=[]
 for name in NAMES:
  m=mesh(name);tree=name in TREES;bush=name in BUSHES
  mats=[u.load_asset(BASE+'/Materials/M_Bark'),u.load_asset(BASE+'/Materials/'+('M_Ash' if name==BUSHES[1] else 'M_Oak'))] if tree or bush else [u.load_asset(BASE+'/Materials/M_Meadow')]
  n=m.get_editor_property('nanite_settings');n.enabled=False;m.set_editor_property('nanite_settings',n)
  for i,mat in enumerate(mats):assert mat;m.set_material(i,mat)
  for i in range(1,4 if tree else 3 if bush else 2):
   src=mesh(name+'_LOD'+str(i))
   for j,mat in enumerate(mats):src.set_material(j,mat)
   assert e.SM.set_lod_from_static_mesh(m,i,src,0,True)==i
  for i in range(e.SM.get_lod_count(m)):
   settings=e.SM.get_lod_build_settings(m,i);settings.set_editor_property('recompute_normals',False);settings.set_editor_property('recompute_tangents',True);e.SM.set_lod_build_settings(m,i,settings)
  # Author-provided LOD geometry; only change screen thresholds, never reduce leaves generically.
  sizes=[1,.19,.075,.027] if tree else [1,.13,.045] if bush else [1,.015]
  assert e.SM.set_lod_screen_sizes(m,sizes)
  for i,size in enumerate(sizes):
   red=e.SM.get_lod_reduction_settings(m,i);red.set_editor_property('percent_triangles',1.0);red.set_editor_property('percent_vertices',1.0);e.SM.set_lod_reduction_settings(m,i,red)
  # Source model slots retain material correspondence across authored LODs.
  if tree:
   h=[1100,930,810][TREES.index(name)]*.51;r=[22,19,17][TREES.index(name)];e.collision(m,([],[e.struct(u.KSphylElem,center=u.Vector(0,0,h/2),radius=r,length=h-2*r)]))
  else:e.collision(m,([],[]))
  e.save(m);b=m.get_bounding_box();tris=[m.get_num_triangles(i) for i in range(e.SM.get_lod_count(m))]
  assert max(tris)<(20000 if tree else 5000 if bush else 300),(name,tris)
  assert b.min.z>=-1,(name,b.min)
  report.append(dict(name=name,triangles=tris,bounds_min=list(b.min.to_tuple()),bounds_max=list(b.max.to_tuple()),materials=[s.material_interface.get_path_name() for s in m.static_materials],simple_collision=e.SM.get_simple_collision_count(m)))
 (EV/'asset-check.json').write_text(json.dumps(report,indent=2));print('SAMPLE_CONFIGURE_COMPLETE',report)

def configure_rock():
 m=mesh('SM_Rock_Sample');n=m.get_editor_property('nanite_settings');n.enabled=False;m.set_editor_property('nanite_settings',n);m.set_material(0,u.load_asset(BASE+'/Materials/M_Rock'))
 reduction=e.SM.get_lod_reduction_settings(m,0);reduction.set_editor_property('percent_triangles',.044);e.SM.set_lod_reduction_settings(m,0,reduction)
 assert e.SM.set_lods(m,u.StaticMeshReductionOptions(auto_compute_lod_screen_size=False,reduction_settings=[u.StaticMeshReductionSettings(percent_triangles=.044,screen_size=1),u.StaticMeshReductionSettings(percent_triangles=.3,screen_size=.08),u.StaticMeshReductionSettings(percent_triangles=.09,screen_size=.025)]))==3
 e.SM.remove_collisions(m);assert e.SM.add_simple_collisions(m,u.ScriptCollisionShapeType.NDOP26)>=0;e.save(m)
 tris=[m.get_num_triangles(i) for i in range(e.SM.get_lod_count(m))];assert max(tris)<=3000,tris
 (EV/'rock-check.json').write_text(json.dumps(dict(triangles=tris,bounds_min=list(m.get_bounding_box().min.to_tuple()),bounds_max=list(m.get_bounding_box().max.to_tuple()))));print('SAMPLE_ROCK_COMPLETE',tris)

def foliage(name):
 tree=name in TREES;grass=name in GRASS
 ft=asset('FT_'+name[3:],u.FoliageType_InstancedStaticMesh,u.FoliageType_InstancedStaticMeshFactory(),'Foliage')
 props=dict(mesh=mesh(name),scaling=u.FoliageScaling.UNIFORM,scale_x=u.FloatInterval(.85,1.15),scale_y=u.FloatInterval(.85,1.15),scale_z=u.FloatInterval(.85,1.15),align_to_normal=False,align_max_angle=0,random_pitch_angle=0,random_yaw=True,ground_slope_angle=u.FloatInterval(0,25),enable_density_scaling=False,enable_cull_distance_scaling=not tree,cast_shadow=not grass,evaluate_world_position_offset=False,cull_distance=u.Int32Interval(0,0) if tree else u.Int32Interval(6000,9000) if grass else u.Int32Interval(10000,16000))
 for k,v in props.items():ft.set_editor_property(k,v)
 body=e.struct(u.BodyInstance,collision_profile_name='BlockAll' if tree else 'NoCollision',collision_enabled=u.CollisionEnabled.QUERY_AND_PHYSICS if tree else u.CollisionEnabled.NO_COLLISION)
 body.set_editor_property('collision_responses',e.struct(u.CollisionResponse,response_array=[e.struct(u.ResponseChannel,channel='GroundSurface',response=u.CollisionResponseType.ECR_IGNORE)]));ft.set_editor_property('body_instance',body);e.save(ft);return ft

def place():
 assert not any(c.static_mesh and '/Environment/Sample/' in c.static_mesh.get_path_name() for a in e.ACT.get_all_level_actors() if isinstance(a,u.InstancedFoliageActor) for c in a.get_components_by_class(u.FoliageInstancedStaticMeshComponent)), 'Sample already exists; preserve edits'
 assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
 ctx=e.context();gate=ctx[0][1];rng=random.Random(5101803);counts={};roots=[];rejects=0
 # Four unequal oak groups climb from the first-gate meadow towards the existing central hill.
 # Coordinates are offsets from the actual first gate, in cm; clearings and route come from actors.
 groups=[(-1400,3500),(-200,5500),(900,7700),(1500,10500)]
 offsets=[(-500,-220),(230,0),(-50,640),(700,530)]
 tree_points=[]
 for gi,(gx,gy) in enumerate(groups):
  for j,(ox,oy) in enumerate(offsets):tree_points.append((TREES[(gi+j)%3],gate.x+gx+ox,gate.y+gy+oy,rng.uniform(.88,1.08)))
 def transform(name,x,y,scale):
  nonlocal rejects
  if not e.allowed(x,y,e.radius(mesh(name),scale),ctx):rejects+=1;return None
  hit=e.trace(x,y)
  if not hit:rejects+=1;return None
  p,n=hit;p.z-=.5
  return u.Transform(location=p,rotation=u.Rotator(pitch=0,yaw=rng.uniform(0,360),roll=0),scale=u.Vector(scale,scale,scale))
 for name in TREES:
  trans=[]
  for n,x,y,scale in tree_points:
   if n!=name:continue
   t=transform(name,x,y,scale);assert t,(name,x,y);trans.append(t);roots.append(t.translation)
  u.InstancedFoliageActor.add_instances(e.world(),foliage(name),trans);counts[name]=len(trans)
 # Understorey follows only selected edges, leaving open windows through each group.
 edges=[(-1860,3440),(-910,4270),(-520,5100),(650,6050),(330,7500),(1950,8020),(950,10100),(2240,10900)]
 for ni,name in enumerate(BUSHES):
  trans=[];target=18 if ni==0 else 12
  for attempt in range(400):
   if len(trans)==target:break
   gx,gy=edges[(attempt//3)%len(edges)];x=gate.x+gx+rng.gauss(0,110);y=gate.y+gy+rng.gauss(0,95);s=rng.uniform(.85,1.1)
   if any(math.hypot(x-t.translation.x,y-t.translation.y)<95 for t in trans):continue
   if any(math.hypot(x-r.x,y-r.y)<90 for r in roots):continue
   t=transform(name,x,y,s)
   if t:trans.append(t)
  assert len(trans)==target,(name,len(trans));u.InstancedFoliageActor.add_instances(e.world(),foliage(name),trans);counts[name]=len(trans)
 # Dense irregular islands at the grove edge, with an open winding meadow between them.
 for ni,name in enumerate(GRASS):
  trans=[];target=1600 if ni==0 else 200
  for attempt in range(target*20):
   if len(trans)==target:break
   gx,gy=rng.choice(edges);angle=rng.uniform(0,math.tau)
   radial=rng.uniform(240,440) if ni else 300*math.sqrt(rng.random())
   x=gate.x+gx+math.cos(angle)*radial;y=gate.y+gy+math.sin(angle)*radial*.72
   if any(math.hypot(x-r.x,y-r.y)<70 for r in roots):continue
   # A scalloped edge and variable density avoid a painted circular stamp.
   if rng.random()<.15*(1+math.sin(x/70)*math.cos(y/90)):continue
   t=transform(name,x,y,rng.uniform(.85,1.15))
   if t:trans.append(t)
  assert len(trans)==target,(name,len(trans));u.InstancedFoliageActor.add_instances(e.world(),foliage(name),trans);counts[name]=len(trans)
 for a in e.ACT.get_all_level_actors():
  if isinstance(a,u.InstancedFoliageActor):
   for c in a.get_components_by_class(u.FoliageInstancedStaticMeshComponent):
    if c.static_mesh and '/Environment/Sample/' in c.static_mesh.get_path_name():c.set_collision_response_to_channel(e.GROUND,u.CollisionResponseType.ECR_IGNORE)
 # Two asymmetric rock groups; larger stones sit into the terrain, smaller fragments lead the eye uphill.
 rock=mesh('SM_Rock_Sample');rock_positions=[(260,6710,1.3),(390,6790,.65),(120,6800,.48),(2060,9340,1.0),(2200,9420,.55),(1980,9460,.4)]
 for i,(x,y,scale) in enumerate(rock_positions):
  x+=gate.x;y+=gate.y;assert e.allowed(x,y,e.radius(rock,scale),ctx);hit=e.trace(x,y);assert hit;p,n=hit
  b=rock.get_bounding_box();p.z-=b.min.z*scale+8
  actor=e.ACT.spawn_actor_from_object(rock,p,u.Rotator(pitch=0,yaw=rng.uniform(0,360),roll=0));actor.set_actor_scale3d(u.Vector(scale,scale,scale));actor.set_actor_label('E5 Sample - Boulder %02d'%i);actor.set_folder_path('E5 Sample');actor.set_editor_property('tags',[u.Name('E5Prop')]);c=actor.static_mesh_component;c.set_collision_profile_name('BlockAll');c.set_collision_response_to_channel(e.GROUND,u.CollisionResponseType.ECR_IGNORE);c.set_cull_distance(0)
 counts['SM_Rock_Sample']=len(rock_positions)
 for i,root in enumerate(roots[::3]):
  a=e.ACT.spawn_actor_from_class(u.DecalActor,u.Vector(root.x,root.y,root.z+15),u.Rotator(pitch=-90,yaw=rng.uniform(0,360),roll=0));a.set_actor_label('E5 Sample - Root ground %02d'%i);a.set_folder_path('E5 Sample');c=a.get_component_by_class(u.DecalComponent);c.set_decal_material(u.load_asset(e.BASE+'Materials/M_GroundPatch_'+str(i%2)));c.set_editor_property('decal_size',u.Vector(80,220,170));c.set_editor_property('fade_screen_size',.001)
 assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
 (EV/'placement.json').write_text(json.dumps(dict(seed=5101803,counts=counts,ground_rejections=rejects,anchor_first_gate_cm=list(gate.to_tuple()),groups_relative_cm=groups),indent=2));print('SAMPLE_PLACED',counts)

def validate():
 ctx=e.context();counts={};failures=[];clearance=1e30;ground=0;lods={}
 for a in e.ACT.get_all_level_actors():
  if isinstance(a,u.InstancedFoliageActor):
   for c in a.get_components_by_class(u.FoliageInstancedStaticMeshComponent):
    m=c.static_mesh
    if not m or not m.get_path_name().startswith(e.BASE):continue
    name=m.get_name();counts[name]=counts.get(name,0)+c.get_instance_count();tree=name.startswith('SM_Tree')
    assert c.get_collision_response_to_channel(e.GROUND)==u.CollisionResponseType.ECR_IGNORE
    assert c.get_collision_enabled()==(u.CollisionEnabled.QUERY_AND_PHYSICS if tree else u.CollisionEnabled.NO_COLLISION)
    if tree:assert c.get_editor_property('instance_end_cull_distance')==0
    if '/Sample/' not in m.get_path_name():continue
    lods[name]=dict(triangles=[m.get_num_triangles(i) for i in range(e.SM.get_lod_count(m))],screens=list(e.SM.get_lod_screen_sizes(m)),cull_start=c.get_editor_property('instance_start_cull_distance'),cull_end=c.get_editor_property('instance_end_cull_distance'))
    for i in range(c.get_instance_count()):
     t=c.get_instance_transform(i,True);p=t.translation;r=e.radius(m,t.scale3d.x)
     gap=min(e.segment_distance(p.x,p.y,(a.x,a.y),(b.x,b.y))-r-800 for a,b in zip(ctx[0],ctx[0][1:]));clearance=min(clearance,gap)
     if not e.allowed(p.x,p.y,r,ctx):failures.append([name,i,'bounds exclusion'])
     h=e.trace(p.x,p.y);assert h;ground+=1
     if abs(h[0].z-p.z)>2:failures.append([name,i,'root gap'])
     rot=t.rotation.rotator();assert abs(rot.pitch)<3 and abs(rot.roll)<3
  elif a.get_actor_label().startswith('E5 Sample - Boulder'):
   c=a.static_mesh_component;m=c.static_mesh;name=m.get_name();counts[name]=counts.get(name,0)+1;p=a.get_actor_location();r=e.radius(m,a.get_actor_scale3d().x);assert e.allowed(p.x,p.y,r,ctx);assert c.get_collision_response_to_channel(e.GROUND)==u.CollisionResponseType.ECR_IGNORE;assert e.SM.get_simple_collision_count(m)+e.SM.get_convex_collision_count(m)>0
 totals=dict(trees=sum(v for k,v in counts.items() if k.startswith('SM_Tree')),bushes=sum(v for k,v in counts.items() if k.startswith('SM_Bush')),grass=sum(v for k,v in counts.items() if k.startswith('SM_Grass')),rocks=40+counts.get('SM_Rock_Sample',0))
 assert 220<=totals['trees']<=320 and 120<=totals['bushes']<=200 and totals['grass']<=10000 and 30<=totals['rocks']<=50,totals
 result=dict(counts=counts,totals=totals,min_sample_corridor_clearance_cm=clearance,ground_checks=ground,failures=failures,lod_and_cull=lods,visual_review='не проверено',flight='не проверено');(EV/'map-validation.json').write_text(json.dumps(result,indent=2));assert not failures,failures[:10];assert ground==1846,ground
 print('SAMPLE_VALIDATION_PASS',totals,clearance,ground)
