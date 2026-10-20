"""E5A one-shot authoring via Unreal MCP. Geometry lives in placed actors."""
import math, json
from pathlib import Path
import unreal as u
ROOT=Path(u.Paths.project_dir()).resolve().parents[1]
EV=ROOT/'docs/prototype-level/evidence/stage05a'
ACT=u.get_editor_subsystem(u.EditorActorSubsystem)
LEVEL=u.get_editor_subsystem(u.LevelEditorSubsystem)
GROUND=u.CollisionChannel.ECC_GROUND_SURFACE

def position(actor, x, y, z=0, yaw=0):
    actor.set_actor_location(u.Vector(x*100,(y-60)*100,z*100),False,False)
    actor.set_actor_rotation(u.Rotator(pitch=0,yaw=yaw,roll=0),False)

def cube(label,x,y,z,sx,sy,sz,yaw=0,solid=False):
    a=ACT.spawn_actor_from_class(u.StaticMeshActor,u.Vector())
    assert a
    a.set_actor_label('E5A - '+label);a.set_folder_path('E5A/Preparation' if solid else 'E5A/Markings')
    a.set_editor_property('tags',['E5APrep' if solid else 'E5AMarking'])
    c=a.static_mesh_component
    c.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
    c.set_material(0,u.load_asset('/Game/TrainingField/Terrain/M_Stage01_Obstacle' if solid else '/Engine/BasicShapes/BasicShapeMaterial'))
    c.set_collision_profile_name("BlockAll" if solid else "NoCollision")
    if not solid: a.set_actor_enable_collision(False)
    c.set_collision_response_to_channel(GROUND,u.CollisionResponseType.ECR_IGNORE)
    a.set_actor_scale3d(u.Vector(sx,sy,sz));position(a,x,y,z,yaw)
    return a

def line(label,a,b,width=.22,z=.025):
    dx,dy=b[0]-a[0],b[1]-a[1]
    return cube(label,(a[0]+b[0])/2,(a[1]+b[1])/2,z,math.hypot(dx,dy),width,.02,math.degrees(math.atan2(dy,dx)))

def text(label,value,x,y,z,size=140,flat=True):
    a=ACT.spawn_actor_from_class(u.TextRenderActor,u.Vector())
    a.set_actor_label('E5A - '+label);a.set_folder_path('E5A/Labels')
    a.set_editor_property('tags',['E5AMarking'])
    position(a,x,y,z)
    a.set_actor_rotation(u.Rotator(pitch=90 if flat else 0,yaw=-90,roll=0),False)
    c=a.get_component_by_class(u.TextRenderComponent)
    c.set_text(value);c.set_world_size(size)
    c.set_horizontal_alignment(u.HorizTextAligment.EHTA_CENTER)
    c.set_collision_profile_name("NoCollision")
    a.set_actor_enable_collision(False)
    return a

def bezier(p,t):
    return tuple((1-t)**3*p[0][i]+3*(1-t)**2*t*p[1][i]+3*(1-t)*t*t*p[2][i]+t**3*p[3][i] for i in range(2))

def apply():
    actors=ACT.get_all_level_actors()
    assert not any(a.actor_has_tag('E5AComplete') for a in actors), 'Already authored: inspect before changing'
    land=next(a for a in actors if isinstance(a,u.Landscape))
    if not land.actor_has_tag("E5AFlattened"):
        assert u.Stage01TerrainBuilder.flatten_training_block(land)
        land.set_editor_property("tags",list(land.tags)+["E5AFlattened"])
    # The rejected E5 placement is superseded; source assets stay available for E5B.
    for a in actors:
        if a.get_actor_label().startswith(('E5 -','E5 Sample -','E5A -')) or (isinstance(a,u.TriggerBox) and str(a.get_folder_path()) in ('Route/Exclusions','Gameplay/Exclusions','Vegetation/Zones')):
            assert ACT.destroy_actor(a),a.get_actor_label()
    actors=ACT.get_all_level_actors()
    start=next(a for a in actors if isinstance(a,u.PlayerStart))
    pad_a=next(a for a in actors if a.get_actor_label()=='StartPad')
    pad_b=next(a for a in actors if a.get_actor_label()=='LandingPad')
    position(start,-32,-18,.5,90);start.set_actor_label('Start A - toward G1')
    position(pad_a,-32,-18);position(pad_b,32,-18)
    gates=sorted((a for a in actors if isinstance(a,u.DroneTrainerGate)),key=lambda a:a.gate_index)
    assert len(gates)==5
    for a,(x,y,yaw) in zip(gates,[(-32,2,90),(-32,22,90),(0,36,0),(32,22,-90),(32,2,-90)]):
        position(a,x,y,0,yaw)
    layout=next(a for a in actors if isinstance(a,u.DroneTrainerLevelLayout))
    assert layout.start==start and layout.landing_pad==pad_b and list(layout.gates)==gates
    # Straight approach and return; offset the curve by its measured tangent.
    curves=[[(-32,22),(-32,36),(-18,36),(0,36)],[(0,36),(18,36),(32,36),(32,22)]]
    path=[(-32,-18),(-32,2),(-32,22)]
    path += [bezier(c,i/24) for c in curves for i in range(1,25)]
    path += [(32,2),(32,-18)]
    for side in (-1,1):
        edge=[]
        for i,p in enumerate(path):
            prev=path[max(0,i-1)];nxt=path[min(len(path)-1,i+1)]
            dx,dy=nxt[0]-prev[0],nxt[1]-prev[1];length=math.hypot(dx,dy)
            edge.append((p[0]-side*8*dy/length,p[1]+side*8*dx/length))
        for i,(a,b) in enumerate(zip(edge,edge[1:])):line('Corridor %d %02d'%(side,i),a,b)
    for x in (-32,32):
        for y in (-8,12):line('Distance tick',(x-7.7,y),(x+7.7,y),.12)
    for x,sign in [(-32,1),(32,-1)]:
        line('Direction',(x,-15),(x,-11 if sign==1 else -19),.3)
        tip=(x,-11 if sign==1 else -19)
        for s in (-1,1):line('Arrow',tip,(x+s,tip[1]-sign*1.5),.3)
    text('Pad A','A',-32,-19.5,.05,230)
    text('Pad B','B',32,-19.5,.05,230)
    # Distinct landing double border; collision stays solely on the existing pad.
    for r in (3.3,3.65):
        pts=[(32-r,-18-r),(32+r,-18-r),(32+r,-18+r),(32-r,-18+r)]
        for a,b in zip(pts,pts[1:]+pts[:1]):line('B border',a,b,.12)
    cube('Training module',-8,-41,1.5,6,4,3,solid=True)
    cube('Table',0,-39,.4,2,1,.8,solid=True)
    for x in (3,4):cube('Case place',x,-39,.25,.7,.5,.5,solid=True)
    text('Preparation','PREPARATION',0,-35,.05,100)
    text('Lesson','A  >  G1-G5  >  B',-8,-38.9,2.4,38,False)
    layout.set_editor_property('tags',list(layout.tags)+['E5AComplete'])
    assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    assert LEVEL.save_current_level()
    (EV/'authored-route.json').write_text(json.dumps({'origin_cm':[0,-6000,0],'curve_cm':[[x*100,(y-60)*100,0] for x,y in path],'status':'authored_not_user_accepted'},indent=2))
    print('E5A_AUTHORING_PASS')


def validate():
    world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    rows=[]
    for x in range(-60,61,2):
        for y in range(-50,51,2):
            hit=u.SystemLibrary.line_trace_single(world,u.Vector(x*100,(y-60)*100,2000),u.Vector(x*100,(y-60)*100,-2000),u.TraceTypeQuery.ECC_GROUND_SURFACE,False,[],u.DrawDebugTrace.NONE)
            assert hit, (x,y)
            data=hit.to_tuple()
            assert data[0] and abs(data[5].z)<.2 and data[7].z>.999,(x,y,str(data))
            rows.append(data[5].z)
    # At the transition sample the actual collision surface, including the outer border.
    transition=[]
    for y in range(-90,91):
        hit=u.SystemLibrary.line_trace_single(world,u.Vector(-6000,(y-60)*100,12000),u.Vector(-6000,(y-60)*100,-12000),u.TraceTypeQuery.ECC_GROUND_SURFACE,False,[],u.DrawDebugTrace.NONE)
        assert hit
        data=hit.to_tuple();assert data[0]
        transition.append(data[5].z)
    out={'flat_samples':len(rows),'min_z_cm':min(rows),'max_z_cm':max(rows),'transition_samples':len(transition),'transition_max_step_cm_per_m':max(abs(b-a) for a,b in zip(transition,transition[1:])),'result':'PASS'}
    (EV/'editor-ground-validation.json').write_text(json.dumps(out,indent=2))
    print('E5A_GROUND_PASS',out)
