"""E5 authoring through Unreal MCP; no runtime generation or lesson changes."""
import json, math, random
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve().parents[1]
SRC = ROOT / 'AssetsSource/Environment'
EV = ROOT / 'docs/prototype-level/evidence/stage05'
BASE = '/Game/TrainingField/Environment/'
LIB = u.EditorAssetLibrary
TOOLS = u.AssetToolsHelpers.get_asset_tools()
SM = u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
ACT = u.get_editor_subsystem(u.EditorActorSubsystem)
M = u.MaterialEditingLibrary
PLANTS = ['SM_Tree_Oak','SM_Tree_Birch','SM_Tree_Spruce','SM_Bush_Low','SM_Bush_Round','SM_Bush_Tall','SM_Grass_Short','SM_Grass_Dry']
ROCKS = ['SM_Rock_Small','SM_Rock_Medium','SM_Rock_Large']
GROUND = u.CollisionChannel.ECC_GROUND_SURFACE


def save(asset):
    assert LIB.save_loaded_asset(asset), asset


def struct(cls,**properties):
    value=cls()
    for k,v in properties.items():value.set_editor_property(k,v)
    return value


def load(name):
    path=BASE+name
    if name.startswith('Meshes/'):
        path += '/StaticMeshes/'+name.split('/')[-1]
    a = u.load_asset(path)
    assert a, name
    return a


def import_meshes(names,replace=False):
    assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    for name in names:
        if not replace and LIB.does_asset_exist(BASE+'Meshes/'+name+'/StaticMeshes/'+name):continue
        task = u.AssetImportTask()
        task.filename = str(SRC / (name+'.glb')); task.destination_path=BASE+'Meshes'
        task.destination_name=name; task.automated=True; task.replace_existing=True; task.save=True
        TOOLS.import_asset_tasks([task])
        mesh=load('Meshes/'+name)
        assert isinstance(mesh,u.StaticMesh),task.imported_object_paths
        print('STAGE05_IMPORTED',name,mesh.get_num_triangles(0))


def import_texture(name, filename, kind='color'):
    task=u.AssetImportTask();task.filename=str(filename);task.destination_path=BASE+'Textures'
    task.destination_name=name;task.automated=True;task.replace_existing=True;task.save=False
    TOOLS.import_asset_tasks([task]);t=load('Textures/'+name)
    t.set_editor_property('srgb',kind=='color')
    t.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP if kind=='normal' else u.TextureCompressionSettings.TC_DEFAULT if kind=='color' else u.TextureCompressionSettings.TC_MASKS)
    t.set_editor_property('flip_green_channel',False)
    t.set_editor_property('never_stream',False)
    if kind=='color' and 'Bark' not in name and 'Rock' not in name:
        t.set_editor_property('do_scale_mips_for_alpha_coverage',True)
        t.set_editor_property('alpha_coverage_thresholds',u.Vector4(0,0,0,.45))
    save(t);return t


def expression(mat,cls,**props):
    n=M.create_material_expression(mat,cls)
    for k,v in props.items():n.set_editor_property(k,v)
    return n


def material(name,masked):
    assert not LIB.does_asset_exist(BASE+'Materials/'+name), 'Do not silently rebuild authored materials'
    mat=TOOLS.create_asset(name,BASE+'Materials',u.Material,u.MaterialFactoryNew())
    mat.set_editor_property('blend_mode',u.BlendMode.BLEND_MASKED if masked else u.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property('two_sided',masked)
    if masked:mat.set_editor_property('opacity_mask_clip_value',.45)
    tex=expression(mat,u.MaterialExpressionTextureSampleParameter2D,parameter_name='BaseColor',texture=load('Textures/T_GrassAtlas_BC' if name=='M_Grass' else 'Textures/T_Leaf_Oak' if masked else 'Textures/T_Bark_Oak'))
    assert M.connect_material_property(tex,'RGB',u.MaterialProperty.MP_BASE_COLOR)
    rough=expression(mat,u.MaterialExpressionScalarParameter,parameter_name='Roughness',default_value=.9 if masked else .95)
    assert M.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
    metal=expression(mat,u.MaterialExpressionConstant,r=0)
    assert M.connect_material_property(metal,'',u.MaterialProperty.MP_METALLIC)
    if masked:
        fade=expression(mat,u.MaterialExpressionPerInstanceFadeAmount)
        mul=expression(mat,u.MaterialExpressionMultiply)
        assert M.connect_material_expressions(tex,'A',mul,'A')
        assert M.connect_material_expressions(fade,'',mul,'B')
        assert M.connect_material_property(mul,'',u.MaterialProperty.MP_OPACITY_MASK)
    M.recompile_material(mat);save(mat);return mat


def instance(name,parent,texture):
    mi=TOOLS.create_asset(name,BASE+'Materials',u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
    M.set_material_instance_parent(mi,parent)
    M.set_material_instance_texture_parameter_value(mi,'BaseColor',texture)
    save(mi);return mi


def materials():
    fol=ROOT/'Unreal/DroneTrainer/Content/TrainingField/Textures/Foliage'
    for name,file in [('Leaf_Oak','leaf_wide.png'),('Leaf_Birch','leaf_light.png'),('Needles','needle.png'),('Leaf_Shrub','leaf_dark.png'),('Bark_Oak','bark_oak.png'),('Bark_Birch','bark_birch.png'),('Bark_Spruce','bark_pine.png')]:
        import_texture('T_'+name,fol/file)
    for name,kind in [('GrassAtlas_BC','color'),('GrassAtlas_N','normal'),('GrassAtlas_R','data'),('Rock_BC','color')]:
        import_texture('T_'+name,SRC/('T_'+name+'.png'),kind)
    leaf=material('M_Foliage',True);bark=material('M_Bark',False);material('M_Grass',True)
    for name in ['Leaf_Oak','Leaf_Birch','Needles','Leaf_Shrub','Bark_Oak','Bark_Birch','Bark_Spruce']:
        instance('MI_'+name,bark if name.startswith('Bark') else leaf,load('Textures/T_'+name))
    instance('MI_Rock',bark,load('Textures/T_Rock_BC'))
    print('STAGE05_MATERIALS_COMPLETE')


def collision(mesh,shapes):
    SM.remove_collisions(mesh)
    body=mesh.get_editor_property('body_setup')
    body.set_editor_property('agg_geom',struct(u.KAggregateGeom,box_elems=shapes[0],sphyl_elems=shapes[1]))
    body.set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)


def lods(mesh,ratios):
    n=mesh.get_editor_property('nanite_settings');n.set_editor_property('enabled',False);mesh.set_editor_property('nanite_settings',n)
    build=SM.get_lod_build_settings(mesh,0);build.set_editor_property('recompute_normals',True);build.set_editor_property('recompute_tangents',True)
    SM.set_lod_build_settings(mesh,0,build)
    opts=u.StaticMeshReductionOptions(auto_compute_lod_screen_size=False,reduction_settings=[u.StaticMeshReductionSettings(percent_triangles=p,screen_size=s) for p,s in ratios])
    assert SM.set_lods(mesh,opts)==len(ratios)


def configure_meshes(names):
    leaves=['Leaf_Oak','Leaf_Birch','Needles','Leaf_Shrub','Leaf_Oak','Leaf_Birch']
    bark=['Bark_Oak','Bark_Birch','Bark_Spruce','Bark_Oak','Bark_Oak','Bark_Oak']
    for name in names:
        mesh=load('Meshes/'+name);i=PLANTS.index(name) if name in PLANTS else -1
        if 0<=i<6:
            mesh.set_material(0,load('Materials/MI_'+bark[i]));mesh.set_material(1,load('Materials/MI_'+leaves[i]))
            lods(mesh,[(1,1),(.38,.15),(.08,.055),(.025,.018)] if i<3 else [(1,1),(.4,.12),(.08,.035)])
        else:
            mesh.set_material(0,load('Materials/M_Grass' if i>=6 else 'Materials/MI_Rock'))
            lods(mesh,[(1,1),(.5,.025)] if i>=6 else [(1,1),(.4,.08),(.1,.025)])
        if 0<=i<3:
            height=[430,650,1220][i];radius=[24,14,18][i]
            collision(mesh,([], [struct(u.KSphylElem,center=u.Vector(0,0,height/2),radius=radius,length=height-2*radius)]))
        elif name in ROCKS[1:]:
            SM.remove_collisions(mesh)
            assert SM.add_simple_collisions(mesh,u.ScriptCollisionShapeType.NDOP26)>=0
        else:collision(mesh,([],[]))
        save(mesh)
    print('STAGE05_MESHES_CONFIGURED',names)


def world():
    return u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()


def trace(x,y):
    hit=u.SystemLibrary.line_trace_single(world(),u.Vector(x,y,12000),u.Vector(x,y,-12000),u.TraceTypeQuery.ECC_GROUND_SURFACE,False,[],u.DrawDebugTrace.NONE)
    if hit is None:return None
    values=hit.to_tuple()
    # break_hit_result: blocking/startpenetrating/time/distance/location/impact_point/normal/impact_normal...
    if not values[0] or values[7].z<math.cos(math.radians(25)):return None
    return values[5],values[7]


def foliage_type(name):
    tree=name in PLANTS[:3];grass=name in PLANTS[6:]
    ft=TOOLS.create_asset('FT_'+name[3:],BASE+'Foliage',u.FoliageType_InstancedStaticMesh,u.FoliageType_InstancedStaticMeshFactory())
    assert ft
    ft.set_editor_property('mesh',load('Meshes/'+name))
    for k,v in dict(scaling=u.FoliageScaling.UNIFORM,scale_x=u.FloatInterval(.85,1.15),scale_y=u.FloatInterval(.85,1.15),scale_z=u.FloatInterval(.85,1.15),align_to_normal=False,align_max_angle=0,random_pitch_angle=0,random_yaw=True,ground_slope_angle=u.FloatInterval(0,25),enable_density_scaling=grass,enable_cull_distance_scaling=not tree,cast_shadow=not grass,evaluate_world_position_offset=False).items():ft.set_editor_property(k,v)
    ft.set_editor_property('cull_distance',u.Int32Interval(0,0) if tree else u.Int32Interval(6500,9000) if grass else u.Int32Interval(16000,22000))
    body=struct(u.BodyInstance,collision_profile_name='BlockAll' if tree else 'NoCollision',collision_enabled=u.CollisionEnabled.QUERY_AND_PHYSICS if tree else u.CollisionEnabled.NO_COLLISION)
    body.set_editor_property('collision_responses',struct(u.CollisionResponse,response_array=[struct(u.ResponseChannel,channel='GroundSurface',response=u.CollisionResponseType.ECR_IGNORE)]))
    ft.set_editor_property('body_instance',body)
    save(ft);return ft


def test_map():
    assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    assert u.EditorLevelLibrary.new_level('/Game/TrainingField/Tests/E5_AssetCheck')
    for i,name in enumerate(PLANTS+ROCKS):
        a=ACT.spawn_actor_from_object(load('Meshes/'+name),u.Vector((i%4)*1800,(i//4)*1800,0));a.set_actor_label(name)
    cube=ACT.spawn_actor_from_object(u.load_asset('/Engine/BasicShapes/Cube.Cube'),u.Vector(-600,0,50));cube.set_actor_label('Calibration 1m')
    assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    assert u.EditorLoadingAndSavingUtils.load_map('/Game/TrainingField/Maps/TrainingField')
    print('STAGE05_TEST_MAP_SAVED')


def context():
    actors=ACT.get_all_level_actors();layout=next(a for a in actors if isinstance(a,u.DroneTrainerLevelLayout))
    gates=sorted(layout.get_editor_property('gates'),key=lambda a:a.get_editor_property('gate_index'))
    assert len(gates)==5
    route=[layout.get_editor_property('start').get_actor_location()]+[a.get_opening_center() for a in gates]+[layout.get_editor_property('landing_pad').get_actor_location()]
    pads=[a for a in actors if a.get_actor_label() in ['Start clearing','Landing clearing','Technical area']]
    road=json.loads((ROOT/'AssetsSource/Terrain/Stage01/layout.json').read_text())['road_m']
    return route,pads,[(p[0]*100,p[1]*100) for p in road]


def segment_distance(x,y,a,b):
    dx=b[0]-a[0];dy=b[1]-a[1];d=dx*dx+dy*dy
    t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/d)) if d else 0
    return math.hypot(x-a[0]-t*dx,y-a[1]-t*dy)


def allowed(x,y,r,ctx,technical=False):
    route,pads,road=ctx
    if any(segment_distance(x,y,(a.x,a.y),(b.x,b.y))<800+r for a,b in zip(route,route[1:])):return False
    if any(segment_distance(x,y,a,b)<350+r for a,b in zip(road,road[1:])):return False
    for a in pads:
        if technical and a.get_actor_label()=='Technical area':continue
        center,extent=a.get_actor_bounds(False)
        if abs(x-center.x)<extent.x+r and abs(y-center.y)<extent.y+r:return False
    return abs(x)+r<20000 and abs(y)+r<20000


def radius(mesh,scale=1):
    b=mesh.get_bounding_box()
    return max(math.hypot(x,y) for x in [b.min.x,b.max.x] for y in [b.min.y,b.max.y])*scale


def place():
    assert not any(a.get_actor_label().startswith('E5') for a in ACT.get_all_level_actors()),'Already placed; preserve edits'
    ctx=context();rng=random.Random(51018);counts={};rejected=0;all_trees=[]
    # Four staggered rows with gaps, jitter and changing species. No runtime PCG dependency.
    candidates=[]
    for row in range(4):
        for j in range(49):candidates.append((-17100+j*700+rng.uniform(-140,140),14600+row*650+rng.uniform(-130,130)))
        for j in range(36):candidates.append((14800+row*650+rng.uniform(-130,130),-13500+j*750+rng.uniform(-150,150)))
        for j in range(16):candidates.append((-17300+row*540+rng.uniform(-100,100),-5500+j*720+rng.uniform(-130,130)))
    rng.shuffle(candidates)
    for name,count in zip(PLANTS[:3],[110,90,60]):
        mesh=load('Meshes/'+name);ft=foliage_type(name);trans=[]
        while candidates and len(trans)<count:
            x,y=candidates.pop();s=rng.uniform(.9,1.1);r=radius(mesh,s)
            if not allowed(x,y,r,ctx) or any(math.hypot(x-a,y-b)<440 for a,b in all_trees):rejected+=1;continue
            hit=trace(x,y)
            if not hit:rejected+=1;continue
            p,n=hit;p.z-=.5
            trans.append(u.Transform(location=p,rotation=u.Rotator(pitch=0,yaw=rng.uniform(0,360),roll=0),scale=u.Vector(s,s,s)));all_trees.append((x,y))
        assert len(trans)==count,(name,len(trans))
        u.InstancedFoliageActor.add_instances(world(),ft,trans);counts[name]=len(trans)
    groups=[(rng.uniform(-17000,17000),rng.choice([-1,1])*rng.uniform(13500,17700)) for i in range(50)]
    groups +=[(rng.choice([-1,1])*rng.uniform(14500,17500),rng.uniform(-6500,13000)) for i in range(40)]
    for name,count in zip(PLANTS[3:],[55,55,50,6000,1800]):
        mesh=load('Meshes/'+name);ft=foliage_type(name);trans=[]
        for attempt in range(count*30):
            if len(trans)==count:break
            gx,gy=rng.choice(groups);x=gx+rng.gauss(0,270);y=gy+rng.gauss(0,240);s=rng.uniform(.88,1.02)
            if not allowed(x,y,radius(mesh,s),ctx):rejected+=1;continue
            hit=trace(x,y)
            if not hit:rejected+=1;continue
            p,n=hit;p.z-=.5
            trans.append(u.Transform(location=p,rotation=u.Rotator(pitch=0,yaw=rng.uniform(0,360),roll=0),scale=u.Vector(s,s,s)))
        assert len(trans)==count,(name,len(trans))
        u.InstancedFoliageActor.add_instances(world(),ft,trans);counts[name]=len(trans)
    for a in ACT.get_all_level_actors():
        if isinstance(a,u.InstancedFoliageActor):
            a.set_actor_label('E5 - Static Mesh Foliage')
            for c in a.get_components_by_class(u.FoliageInstancedStaticMeshComponent):
                c.set_collision_response_to_channel(GROUND,u.CollisionResponseType.ECR_IGNORE)
    (EV/'placement.json').write_text(json.dumps(dict(seed=51018,counts=counts,rejected=rejected),indent=2))
    assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('STAGE05_FOLIAGE_PLACED',counts)


def check_sources():
    rows=[]
    for name in PLANTS+ROCKS:
        mesh=load('Meshes/'+name);b=mesh.get_bounding_box();tris=[mesh.get_num_triangles(i) for i in range(SM.get_lod_count(mesh))]
        assert all(0<t for t in tris)
        assert len(mesh.static_materials)<=2
        assert not mesh.get_editor_property('nanite_settings').enabled
        rows.append(dict(name=name,path=mesh.get_path_name(),min=list(b.min.to_tuple()),max=list(b.max.to_tuple()),triangles=tris,materials=[s.material_interface.get_path_name() for s in mesh.static_materials],simple_collision=SM.get_simple_collision_count(mesh),lod_screens=list(SM.get_lod_screen_sizes(mesh))))
    (EV/'asset-check.json').write_text(json.dumps(rows,indent=2))
    print('STAGE05_ASSET_CHECK_PASS')


def props():
    ctx=context();rng=random.Random(51019);placed=[]
    def spawn(mesh,label,x,y,target=None,yaw=0,technical=False):
        existing=next((a for a in ACT.get_all_level_actors() if a.get_actor_label()==label),None)
        if existing:return existing
        b=mesh.get_bounding_box();size=b.max-b.min
        scale=u.Vector(*(target[i]/[size.x,size.y,size.z][i] for i in range(3))) if target else u.Vector(1,1,1)
        r=max(math.hypot(x*scale.x,y*scale.y) for x in [b.min.x,b.max.x] for y in [b.min.y,b.max.y])
        if not allowed(x,y,r,ctx,technical):return None
        ground=trace(x,y)
        if not ground:return None
        p,n=ground;p.z-=b.min.z*scale.z+.5
        actor=ACT.spawn_actor_from_object(mesh,p,u.Rotator(pitch=0,yaw=yaw,roll=0));actor.set_actor_label(label);actor.set_actor_scale3d(scale)
        c=actor.static_mesh_component;c.set_collision_profile_name('BlockAll' if SM.get_simple_collision_count(mesh)>0 else 'NoCollision')
        c.set_collision_response_to_channel(GROUND,u.CollisionResponseType.ECR_IGNORE)
        c.set_cull_distance(0)
        actor.set_folder_path('E5 Environment')
        placed.append(dict(label=label,path=actor.get_path_name(),location=list(p.to_tuple()),scale=list(scale.to_tuple())))
        return actor
    for i in range(40):
        mesh=load('Meshes/'+ROCKS[i%3])
        for attempt in range(100):
            x=rng.choice([-1,1])*rng.uniform(11800,14200);y=rng.uniform(-6000,13000)
            if spawn(mesh,'E5 - Rock %02d'%i,x,y,yaw=rng.uniform(0,360)):break
        else:raise AssertionError('No rock position')
    for name,target,ratio in [('FieldCabin',[600,300,280],.45),('FieldBarrier',[200,45,90],.3),('EquipmentCase',[60,40,25],.28)]:
        mesh=u.load_asset('/Game/DroneTrainer/Meshes/SM_'+name);assert mesh
        lods(mesh,[(1,1),(ratio,.08)])
        b=mesh.get_bounding_box();size=b.max-b.min;center=(b.max+b.min)*.5
        collision(mesh,([struct(u.KBoxElem,center=center,x=size.x,y=size.y,z=size.z)],[]));save(mesh)
    cabin=u.load_asset('/Game/DroneTrainer/Meshes/SM_FieldCabin')
    assert spawn(cabin,'E5 - FieldCabin',-14900,-10300,[600,300,280],yaw=0,technical=True)
    barrier=u.load_asset('/Game/DroneTrainer/Meshes/SM_FieldBarrier')
    for i in range(10):
        # Two short borders to the service area, with an open access gap.
        assert spawn(barrier,'E5 - Barrier %02d'%i,-15180+i%5*100,-12300 if i<5 else -8200,[200,45,90],yaw=90,technical=True)
    case=u.load_asset('/Game/DroneTrainer/Meshes/SM_EquipmentCase')
    for i in range(3):assert spawn(case,'E5 - EquipmentCase %02d'%i,-14900+i*80,-10700,[60,40,25],technical=True)
    tower=u.load_asset('/Game/TrainingField/Meshes/Tower_01_Observation/StaticMeshes/Tower_01_Observation');assert tower
    boxes=[struct(u.KBoxElem,center=u.Vector(0,0,1220),x=342,y=342,z=15),struct(u.KBoxElem,center=u.Vector(0,0,1400),x=365,y=365,z=15)]
    legs=[]
    for i in range(4):
        a=math.pi/4+i*math.pi/2
        bottom=u.Vector(math.cos(a)*200,math.sin(a)*200,10);top=u.Vector(math.cos(a)*135,math.sin(a)*135,1220)
        direction=top-bottom
        legs.append(struct(u.KSphylElem,center=(bottom+top)*.5,rotation=u.MathLibrary.make_rot_from_z(direction),radius=8,length=direction.length()-16))
        boxes.append(struct(u.KBoxElem,center=u.Vector(bottom.x,bottom.y,0),x=85,y=85,z=28))
    collision(tower,(boxes,legs));lods(tower,[(1,1),(.45,.05)]);save(tower)
    assert spawn(tower,'E5 - Observation Tower',12000,11000)
    (EV/'props.json').write_text(json.dumps(placed,indent=2))
    assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('STAGE05_PROPS_COMPLETE',len(placed))


def finish():
    ctx=context();rng=random.Random(51020)
    entries=[]
    for a in ACT.get_all_level_actors():
        for c in a.get_components_by_class(u.FoliageInstancedStaticMeshComponent):
            if c.static_mesh.get_name() in PLANTS[3:6]:
                entries.extend((c,i) for i in range(c.get_instance_count()))
    assert len(entries)==160
    rng.shuffle(entries)
    candidates=[(-16000+i*1000,y) for y in [13900,14900] for i in range(33)]
    candidates += [(x,-6000+i*1000) for x in [13900,14900] for i in range(19)]
    rng.shuffle(candidates)
    centers=[(x,y) for x,y in candidates if allowed(x,y,400,ctx) and trace(x,y)][:32]
    assert len(centers)==32
    for j,(c,i) in enumerate(entries):
        gx,gy=centers[j//5];mesh=c.static_mesh
        for attempt in range(100):
            x=gx+rng.uniform(-160,160);y=gy+rng.uniform(-160,160)
            s=rng.uniform(.86,.96) if mesh.get_name()==PLANTS[3] else rng.uniform(.88,1.02)
            if not allowed(x,y,radius(mesh,s),ctx):continue
            hit=trace(x,y)
            if not hit:continue
            p,n=hit;p.z-=.5
            assert c.update_instance_transform(i,u.Transform(location=p,rotation=u.Rotator(pitch=0,yaw=rng.uniform(0,360),roll=0),scale=u.Vector(s,s,s)),True,True,True)
            break
        else:raise AssertionError('Bush group surface')
    grass=load('Materials/M_Grass')
    normal=expression(grass,u.MaterialExpressionTextureSampleParameter2D,parameter_name='Normal',texture=load('Textures/T_GrassAtlas_N'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    rough=expression(grass,u.MaterialExpressionTextureSampleParameter2D,parameter_name='RoughnessMap',texture=load('Textures/T_GrassAtlas_R'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_MASKS)
    assert M.connect_material_property(normal,'RGB',u.MaterialProperty.MP_NORMAL)
    assert M.connect_material_property(rough,'R',u.MaterialProperty.MP_ROUGHNESS)
    M.recompile_material(grass);save(grass)
    actors=ACT.get_all_level_actors()
    for a in actors:
        if a.get_actor_label().startswith('E5 - ') and isinstance(a,u.StaticMeshActor):
            a.set_editor_property('tags',[u.Name('E5Prop')])
    tower=next(a for a in actors if a.get_actor_label()=='E5 - Observation Tower')
    p=tower.get_actor_location();heights=[]
    for i in range(4):
        angle=math.pi/4+i*math.pi/2
        hit=trace(p.x+math.cos(angle)*200,p.y+math.sin(angle)*200);assert hit
        heights.append(hit[0].z)
    # The existing concrete feet extend 14cm below root: bury them enough for no air gap.
    p.z=min(heights)+13.5;tower.set_actor_location(p,False,False)
    (EV/'tower-supports.json').write_text(json.dumps(dict(ground_z_cm=heights,pivot_z_cm=p.z,bottom_gap_cm=[p.z-14-z for z in heights]),indent=2))
    assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    print('STAGE05_FINISH_PASS')


def validate():
    ctx=context();route,pads,road=ctx;counts={};failures=[];records=[];clearance=1e30;ground_checks=0
    def check(mesh,t,label,technical=False):
        nonlocal clearance,ground_checks
        p=t.translation;s=t.scale3d;b=mesh.get_bounding_box()
        r=max(math.hypot(x*s.x,y*s.y) for x in [b.min.x,b.max.x] for y in [b.min.y,b.max.y])
        gap=min(segment_distance(p.x,p.y,(a.x,a.y),(z.x,z.y))-r-800 for a,z in zip(route,route[1:]))
        clearance=min(clearance,gap)
        if not allowed(p.x,p.y,r,ctx,technical):failures.append([label,'exclusion',list(p.to_tuple()),r])
        hit=trace(p.x,p.y)
        if not hit:failures.append([label,'ground'])
        else:
            ground_checks+=1
            if mesh.get_name() in PLANTS and abs(p.z-hit[0].z)>2:failures.append([label,'root gap'])
        rot=t.rotation.rotator()
        if abs(rot.pitch)>3 or abs(rot.roll)>3:failures.append([label,'tilt'])
        counts[mesh.get_name()]=counts.get(mesh.get_name(),0)+1
    for a in ACT.get_all_level_actors():
        if isinstance(a,u.InstancedFoliageActor):
            for c in a.get_components_by_class(u.FoliageInstancedStaticMeshComponent):
                mesh=c.static_mesh
                if not mesh.get_path_name().startswith(BASE):continue
                assert c.get_collision_response_to_channel(GROUND)==u.CollisionResponseType.ECR_IGNORE
                tree=mesh.get_name() in PLANTS[:3]
                assert c.get_collision_enabled()==(u.CollisionEnabled.QUERY_AND_PHYSICS if tree else u.CollisionEnabled.NO_COLLISION)
                if tree:assert c.get_editor_property('instance_end_cull_distance')==0
                for i in range(c.get_instance_count()):check(mesh,c.get_instance_transform(i,True),mesh.get_name())
        elif a.get_actor_label().startswith('E5 - ') and isinstance(a,u.StaticMeshActor):
            mesh=a.static_mesh_component.static_mesh
            assert a.static_mesh_component.get_collision_response_to_channel(GROUND)==u.CollisionResponseType.ECR_IGNORE
            check(mesh,a.get_actor_transform(),a.get_actor_label(),not a.get_actor_label().startswith(('E5 - Rock','E5 - Observation')))
            o,e=a.get_actor_bounds(False);records.append(dict(label=a.get_actor_label(),mesh=mesh.get_path_name(),location=list(a.get_actor_location().to_tuple()),rotation=list(a.get_actor_rotation().to_tuple()),scale=list(a.get_actor_scale3d().to_tuple()),bounds_center=list(o.to_tuple()),bounds_extent=list(e.to_tuple()),collision=SM.get_simple_collision_count(mesh)))
    expected=dict(zip(PLANTS,[110,90,60,55,55,50,6000,1800]));expected.update(dict(zip(ROCKS,[14,13,13])));expected.update(SM_FieldCabin=1,SM_FieldBarrier=10,SM_EquipmentCase=3,Tower_01_Observation=1)
    result=dict(counts=counts,expected=expected,min_corridor_clearance_cm=clearance,ground_checks=ground_checks,failures=failures,props=records)
    (EV/'map-validation.json').write_text(json.dumps(result,indent=2))
    assert counts==expected,(counts,expected)
    assert not failures,failures[:10]
    assert clearance>=0
    print('STAGE05_MAP_VALIDATION_PASS',counts,clearance,ground_checks)


def ground_patches():
    assert not any(a.actor_has_tag('E5GroundPatch') for a in ACT.get_all_level_actors())
    ctx=context();rng=random.Random(51022);mats=[]
    for i in range(2):
        mask=import_texture('T_GroundPatch_%d'%i,SRC/('T_GroundPatch_%d.png'%i),'data')
        mat=TOOLS.create_asset('M_GroundPatch_%d'%i,BASE+'Materials',u.Material,u.MaterialFactoryNew())
        mat.set_editor_property('material_domain',u.MaterialDomain.MD_DEFERRED_DECAL)
        mat.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
        bc=expression(mat,u.MaterialExpressionTextureSample,texture=u.load_asset('/Game/TrainingField/Textures/Stage04/T_Dirt_BC'))
        opacity=expression(mat,u.MaterialExpressionTextureSample,texture=mask,sampler_type=u.MaterialSamplerType.SAMPLERTYPE_MASKS)
        assert M.connect_material_property(bc,'RGB',u.MaterialProperty.MP_BASE_COLOR)
        assert M.connect_material_property(opacity,'R',u.MaterialProperty.MP_OPACITY)
        M.recompile_material(mat);save(mat);mats.append(mat)
    positions=[(-13600,-4500+i*2600) for i in range(6)]+[(13300,-4500+i*2600) for i in range(6)]
    records=[]
    for i,(x,y) in enumerate(positions):
        assert allowed(x,y,260,ctx)
        hit=trace(x,y);assert hit
        p,n=hit;p.z+=15
        a=ACT.spawn_actor_from_class(u.DecalActor,p,u.Rotator(pitch=-90,yaw=rng.uniform(0,360),roll=0))
        a.set_actor_label('E5 - Ground patch %02d'%i);a.set_editor_property('tags',[u.Name('E5GroundPatch')]);a.set_folder_path('E5 Environment')
        c=a.get_component_by_class(u.DecalComponent);c.set_decal_material(mats[i%2]);c.set_editor_property('decal_size',u.Vector(50,180,180))
        records.append(dict(actor=a.get_path_name(),location=list(p.to_tuple()),material=mats[i%2].get_path_name()))
    assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    (EV/'ground-patches.json').write_text(json.dumps(records,indent=2))
    print('STAGE05_GROUND_PATCHES_PASS',len(records))


def final_editor_check():
    tower=next(a for a in ACT.get_all_level_actors() if a.get_actor_label()=='E5 - Observation Tower')
    p=tower.get_actor_location();p.x=12500;p.y=11500
    hits=[trace(p.x+math.cos(math.pi/4+i*math.pi/2)*200,p.y+math.sin(math.pi/4+i*math.pi/2)*200) for i in range(4)]
    assert all(hits)
    heights=[h[0].z for h in hits];assert max(heights)-min(heights)<2
    p.z=min(heights)+13.5;tower.set_actor_location(p,False,False)
    (EV/'tower-supports.json').write_text(json.dumps(dict(ground_z_cm=heights,pivot_z_cm=p.z,bottom_gap_cm=[p.z-14-z for z in heights]),indent=2))
    rows={}
    for name in PLANTS:
        ft=load('Foliage/FT_'+name[3:]);row={}
        for key in ['cull_distance','scale_x','scale_y','scale_z','scaling','align_to_normal','align_max_angle','random_pitch_angle','ground_slope_angle','enable_density_scaling','enable_cull_distance_scaling','cast_shadow','evaluate_world_position_offset','body_instance']:
            row[key]=str(ft.get_editor_property(key))
        rows[ft.get_path_name()]=row
    (EV/'foliage-types-measured.json').write_text(json.dumps(rows,indent=2))
    assert sum(a.actor_has_tag('E5GroundPatch') for a in ACT.get_all_level_actors())==12
    assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    validate()
    print('STAGE05_FINAL_EDITOR_PASS')
