"""E5B one-shot editor authoring; reuses the existing baked foliage workflow."""
import json, math, random, re
import unreal as u
import stage05a_layout as a
import stage05_environment as e
import stage05_sample as sample
EV=a.ROOT/'docs/prototype-level/evidence/stage05b'
BASE='/Game/TrainingField/Environment/School'
MATS={}

def route_state():
    actors=a.ACT.get_all_level_actors()
    return {x.get_path_name():re.sub(r"0x[0-9a-fA-F]+", "address", str(x.get_actor_transform())) for x in actors if isinstance(x,(u.DroneTrainerGate,u.PlayerStart,u.DroneTrainerLevelLayout)) or x.get_actor_label() in ('StartPad','LandingPad') or (x.actor_has_tag('E5AMarking') and x.get_actor_label() not in ('E5A - Preparation','E5A - Lesson'))}

def tag(actor,label,solid):
    actor.set_actor_label('E5B - '+label)
    actor.set_folder_path('E5B/School' if solid else 'E5B/Details')
    actor.set_editor_property('tags',['E5BSolid' if solid else 'E5BDecor'])
    c=actor.static_mesh_component
    c.set_collision_profile_name('BlockAll' if solid else 'NoCollision')
    if not solid: actor.set_actor_enable_collision(False)
    c.set_collision_response_to_channel(a.GROUND,u.CollisionResponseType.ECR_IGNORE)
    c.set_cull_distance(0)
    return actor

def part(label,x,y,z,sx,sy,sz,color='Graphite',solid=True,shape='Cube'):
    actor=a.ACT.spawn_actor_from_class(u.StaticMeshActor,u.Vector())
    actor.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/'+shape))
    actor.static_mesh_component.set_material(0,MATS[color])
    actor.set_actor_scale3d(u.Vector(sx,sy,sz));a.position(actor,x,y,z)
    return tag(actor,label,solid)

def prop(name,label,x,y,size,yaw=0,z=0):
    mesh=u.load_asset('/Game/DroneTrainer/Meshes/SM_'+name);assert mesh
    b=mesh.get_bounding_box();extent=b.max-b.min
    scale=u.Vector(*(size[i]*100/extent.to_tuple()[i] for i in range(3)))
    actor=a.ACT.spawn_actor_from_object(mesh,u.Vector())
    actor.set_actor_scale3d(scale);a.position(actor,x,y,z-b.min.z*scale.z/100,yaw)
    return tag(actor,label,True)

def label(value,x,y,z,size):
    actor=a.text('School label',value,x,y,z,size,False)
    actor.set_actor_label('E5B - '+value);actor.set_folder_path('E5B/Signs')
    actor.set_editor_property('tags',['E5BDecor'])
    actor.set_actor_rotation(u.Rotator(pitch=0,yaw=90,roll=0),False)
    return actor

def apply():
    assert not any(x.get_actor_label().startswith('E5B -') for x in a.ACT.get_all_level_actors()), 'Already authored; inspect before changing'
    before=route_state();(EV/'route-before.json').write_text(json.dumps(before,indent=2))
    # Reuse the accepted opaque rough-surface shader; never alter its original asset.
    parent=u.load_asset('/Game/TrainingField/Materials/M_TrainingPad');assert parent
    for name,color in {'Graphite':(.075,.095,.10),'Cream':(.72,.73,.64),'Orange':(.8,.19,.045),'Green':(.12,.21,.16)}.items():
        mi=u.load_asset(BASE+'/MI_'+name) if u.EditorAssetLibrary.does_asset_exist(BASE+'/MI_'+name) else u.AssetToolsHelpers.get_asset_tools().create_asset('MI_'+name,BASE,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
        u.MaterialEditingLibrary.set_material_instance_parent(mi,parent)
        u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi,'PadColor',u.LinearColor(*color,1))
        assert u.EditorAssetLibrary.save_loaded_asset(mi);MATS[name]=mi
    for actor in a.ACT.get_all_level_actors():
        if actor.actor_has_tag('E5APrep') or actor.get_actor_label() in ('E5A - Preparation','E5A - Lesson'):
            assert a.ACT.destroy_actor(actor)
    prop('FieldCabin','Training module',-8,-41,(6,3.5,2.8),90)
    # The table has a real top and four legs, rather than an invisible enclosing box.
    part('Table top',0,-39,.78,2,.9,.08,'Cream')
    for x in (-.85,.85):
        for y in (-39.32,-38.68):part('Table leg',x,y,.37,.07,.07,.74)
    for i in range(3):prop('EquipmentCase','Equipment case %d'%i,2.4+i*.85,-39,(.65,.42,.3),yaw=i*8)
    prop('EquipmentCase','Table case',-.4,-39,(.55,.35,.22),z=.82)
    # A readable physical map faces the field (+Y); it is separate from the HUD.
    part('Lesson board',8,-39,1.85,3,.10,1.9,'Green')
    for x in (6.7,9.3):part('Board support',x,-39,.8,.08,.10,1.6)
    label('A  >  G1-G5  >  B',8,-38.92,2.52,17)
    points=[(-.95,.0),(-.95,.4),(-.95,.8),(0,1.08),(.95,.8),(.95,.4),(.95,0)]
    for j,(p,q) in enumerate(zip(points,points[1:])):
        dx=q[0]-p[0];dz=q[1]-p[1]
        obj=part('Lesson map line',8+(p[0]+q[0])/2,-38.92,1.18+(p[1]+q[1])/2,math.hypot(dx,dz),.025,.022,'Cream',False)
        obj.set_actor_rotation(u.Rotator(pitch=-math.degrees(math.atan2(dz,dx)),yaw=0,roll=0),False)
    for i,(x,z) in enumerate(points):
        part('Lesson map node',8+x,-38.89,1.18+z,.095,.025,.095,'Orange' if i in (0,6) else 'Cream',False)
    label('A',7.05,-38.88,.98,17);label('B',8.95,-38.88,.98,17)
    label('DRONE TRAINING',-8,-39.22,2.2,24)
    # No wind force exists in the flight model: the fabric hangs vertically.
    part('Windsock pole',13,-41,2,.055,.055,4,'Cream',True)
    part('Windsock arm',13.45,-41,3.95,.9,.045,.045,'Graphite')
    for i in range(5):
        diameter=.34-i*.045
        part('Calm windsock',13.85,-41,3.78-i*.2,diameter,diameter,.205,'Orange' if i%2==0 else 'Cream',False,'Cylinder')
    label('WIND 0 m/s',12,-40.92,1.0,17)
    # Low, visible boundary markers; no perimeter fence across recovery space.
    for x,y in [(-58,y) for y in (-42,-12,18,46)]+[(58,y) for y in (-42,-12,18,46)]+[(x,48) for x in (-28,0,28)]:
        part('Field boundary',x,y,.2,.18,.18,.4,'Cream')
        part('Boundary cap',x,y,.405,.19,.19,.03,'Orange',False)
    for x in (-2.7,2.7):
        for y in (-49,-46):part('Access edge',x,y,.12,.14,.7,.24,'Cream')
    rng=random.Random(51019);trees={n:[] for n in sample.TREES}
    # One connected belt behind the field, two staggered rows with overlapping crowns.
    for row in range(2):
        for i in range(27):
            name=sample.TREES[(i+row)%3];x=-85+i*6.5+row*2.4;y=77+row*5.5+math.sin(i*.55)*2
            hit=e.trace(x*100,(y-60)*100);assert hit,(x,y)
            p,_=hit;p.z-=.5;s=rng.uniform(.92,1.08)
            trees[name].append(u.Transform(location=p,rotation=u.Rotator(pitch=0,yaw=rng.uniform(0,360),roll=0),scale=u.Vector(s,s,s)))
    for name,trans in trees.items():
        ft=u.load_asset(sample.BASE+'/Foliage/FT_'+name[3:]);assert ft
        u.InstancedFoliageActor.add_instances(e.world(),ft,trans)
    # Short clumps add a restrained directional mowing pattern; ground materials stay intact.
    grass=[]
    guide=json.loads((a.EV/'authored-route.json').read_text())['curve_cm']
    for xi in range(118):
        for yi in range(82):
            x=-55+xi*.94+rng.uniform(-.15,.15);y=-29+yi*.94+rng.uniform(-.15,.15)
            if any(abs(x-px)<10.5 and abs(y+18)<10.5 for px in (-32,32)):continue
            if abs(abs(x)-40)<.6 or abs(abs(x)-24)<.6:continue
            if any(abs(y-t)<.4 and abs(abs(x)-32)<8.2 for t in (-8,12)):continue
            # Keep the curve's white edge unobscured too.
            distance=min(e.segment_distance(x*100,(y-60)*100,p,q) for p,q in zip(guide,guide[1:]))
            if abs(distance-800)<65:continue
            stripe=int((x+56)//8)%2
            grass.append(u.Transform(location=u.Vector(x*100,(y-60)*100,-.3),rotation=u.Rotator(pitch=0,yaw=90+stripe*180+rng.uniform(-15,15),roll=0),scale=u.Vector(.75,.75,.32+stripe*.06)))
    ft=u.load_asset(sample.BASE+'/Foliage/FT_Grass_SampleMeadow');assert ft
    u.InstancedFoliageActor.add_instances(e.world(),ft,grass)
    for actor in a.ACT.get_all_level_actors():
        for c in actor.get_components_by_class(u.FoliageInstancedStaticMeshComponent):
            c.set_collision_response_to_channel(a.GROUND,u.CollisionResponseType.ECR_IGNORE)
    assert route_state()==before,'Accepted route or markings changed'
    assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    assert a.LEVEL.save_current_level()
    (EV/'authoring.json').write_text(json.dumps(dict(trees=sum(map(len,trees.values())),grass=len(grass),route_unchanged=True,wind_m_s=0),indent=2))
    print('E5B_AUTHORING_PASS',len(grass))


def validate():
    route=json.loads((a.EV/'authored-route.json').read_text())['curve_cm']
    records=[];clearances=[];plants={}
    for actor in a.ACT.get_all_level_actors():
        if actor.actor_has_tag('E5BSolid') or actor.actor_has_tag('E5BDecor'):
            center,extent=actor.get_actor_bounds(False)
            gap=min(e.segment_distance(center.x,center.y,p,q) for p,q in zip(route,route[1:]))-math.hypot(extent.x,extent.y)-800
            assert gap>=0,(actor.get_actor_label(),gap)
            clearances.append(gap)
            for c in actor.get_components_by_class(u.PrimitiveComponent):
                assert c.get_collision_response_to_channel(a.GROUND)==u.CollisionResponseType.ECR_IGNORE or c.get_collision_enabled()==u.CollisionEnabled.NO_COLLISION
                if actor.actor_has_tag('E5BDecor'):assert c.get_collision_enabled()==u.CollisionEnabled.NO_COLLISION
                else:
                    assert c.get_collision_enabled()==u.CollisionEnabled.QUERY_AND_PHYSICS
                    assert e.SM.get_simple_collision_count(c.static_mesh)>0,(actor.get_actor_label(),c.static_mesh.get_path_name())
            records.append(dict(label=actor.get_actor_label(),center=list(center.to_tuple()),extent=list(extent.to_tuple()),corridor_gap_cm=gap))
        for c in actor.get_components_by_class(u.FoliageInstancedStaticMeshComponent):
            mesh=c.static_mesh;tree=mesh.get_name() in sample.TREES
            assert c.get_collision_response_to_channel(a.GROUND)==u.CollisionResponseType.ECR_IGNORE
            assert c.get_collision_enabled()==(u.CollisionEnabled.QUERY_AND_PHYSICS if tree else u.CollisionEnabled.NO_COLLISION)
            assert mesh.get_num_lods()>=(4 if tree else 2)
            if tree:
                assert c.get_editor_property('instance_end_cull_distance')==0
                geom=mesh.get_editor_property('body_setup').get_editor_property('agg_geom')
                assert len(geom.get_editor_property('sphyl_elems'))==1 and not geom.get_editor_property('box_elems') and not geom.get_editor_property('convex_elems')
                leaf=mesh.get_material(1)
                assert leaf.get_editor_property("blend_mode")==u.BlendMode.BLEND_MASKED and leaf.get_editor_property("two_sided")
            else:
                assert e.SM.get_simple_collision_count(mesh)==0
                assert c.get_editor_property('instance_end_cull_distance')<=9000
            for i in range(c.get_instance_count()):
                t=c.get_instance_transform(i,True);p=t.translation;scale=t.scale3d
                hit=e.trace(p.x,p.y);assert hit
                assert abs(p.z-hit[0].z)<1,(mesh.get_name(),p,hit[0])
                if tree:
                    radius=e.radius(mesh,max(scale.x,scale.y))
                    gap=min(e.segment_distance(p.x,p.y,a0,a1) for a0,a1 in zip(route,route[1:]))-radius-800
                    assert gap>=0;clearances.append(gap)
                else:assert mesh.get_bounding_box().max.z*scale.z<10
            plants[mesh.get_name()]=dict(count=c.get_instance_count(),lods=mesh.get_num_lods(),end_cull_cm=c.get_editor_property('instance_end_cull_distance'))
    assert len(plants)==4 and sum(p['count'] for n,p in plants.items() if n in sample.TREES)==54
    previous_evidence=a.EV
    try:
        a.EV=EV;a.validate()
    finally:a.EV=previous_evidence
    (EV/'editor-validation.json').write_text(json.dumps(dict(result='PASS',min_full_bounds_corridor_gap_cm=min(clearances),props=records,foliage=plants,grass_max_height_cm=7.65),indent=2))
    print('E5B_VALIDATION_PASS',len(records),min(clearances))
