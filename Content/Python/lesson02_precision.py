"""One-shot U2A authoring through Unreal MCP; do not rerun over an authored field."""
import math,json
from pathlib import Path
import unreal as u
ROOT=Path(u.Paths.project_dir()).resolve().parents[1]
EV=ROOT/'docs/prototype-level/evidence/lesson02'
ACT=u.get_editor_subsystem(u.EditorActorSubsystem)
LEVEL=u.get_editor_subsystem(u.LevelEditorSubsystem)
GROUND=u.CollisionChannel.ECC_GROUND_SURFACE

def xyz(v): return [v.x,v.y,v.z]
def snapshot(a):
    r=a.get_actor_rotation()
    return dict(path=a.get_path_name(),location=xyz(a.get_actor_location()),rotation=[r.pitch,r.yaw,r.roll],scale=xyz(a.get_actor_scale3d()),
        tags=[str(t) for t in a.tags],components=[dict(path=c.get_path_name(),mesh=c.static_mesh.get_path_name() if c.static_mesh else '',
            collision=str(c.get_collision_enabled()),ground=str(c.get_collision_response_to_channel(GROUND)),materials=[m.get_path_name() if m else '' for m in c.get_materials()]) for c in a.get_components_by_class(u.StaticMeshComponent)])

def mesh(label,location,scale,solid=False,asset='/Engine/BasicShapes/Cube',material=None,yaw=0,tag=None):
    a=ACT.spawn_actor_from_class(u.StaticMeshActor,u.Vector(*location)); assert a
    a.set_actor_label('U2A - '+label); a.set_folder_path('Lesson02/Geometry' if solid else 'Lesson02/Markings')
    a.tags=['Lesson02']+([tag] if tag else [])
    c=a.static_mesh_component; c.set_static_mesh(u.load_asset(asset))
    c.set_material(0,u.load_asset(material or ('/Game/TrainingField/Terrain/M_Stage01_Obstacle' if solid else '/Engine/BasicShapes/BasicShapeMaterial')))
    c.set_collision_profile_name('BlockAll' if solid else 'NoCollision')
    c.set_collision_response_to_channel(GROUND,u.CollisionResponseType.ECR_IGNORE)
    a.set_actor_enable_collision(solid)
    a.set_actor_scale3d(u.Vector(*scale)); a.set_actor_rotation(u.Rotator(yaw=yaw),False)
    return a

def line(label,a,b,width=12,z=3):
    dx,dy=b[0]-a[0],b[1]-a[1]
    return mesh(label,((a[0]+b[0])/2,(a[1]+b[1])/2,z),(math.hypot(dx,dy)/100,width/100,.02),yaw=math.degrees(math.atan2(dy,dx)))

def text(label,value,x,y,z=6,size=110,yaw=-90,flat=True):
    a=ACT.spawn_actor_from_class(u.TextRenderActor,u.Vector(x,y,z)); assert a
    a.set_actor_label('U2A - '+label); a.set_folder_path('Lesson02/Labels'); a.tags=['Lesson02']
    a.set_actor_rotation(u.Rotator(pitch=90 if flat else 0,yaw=yaw),False)
    c=a.get_component_by_class(u.TextRenderComponent); c.set_text(value); c.set_world_size(size)
    c.set_horizontal_alignment(u.HorizTextAligment.EHTA_CENTER); c.set_collision_profile_name('NoCollision'); a.set_actor_enable_collision(False)
    return a

def trace(x,y):
    w=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    h=u.SystemLibrary.line_trace_single(w,u.Vector(x,y,12000),u.Vector(x,y,-2000),u.TraceTypeQuery.ECC_GROUND_SURFACE,False,[],u.DrawDebugTrace.NONE)
    assert h and h.to_tuple()[0],(x,y)
    return h.to_tuple()

def apply():
    assert not u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world()
    actors=ACT.get_all_level_actors(); assert not any(a.actor_has_tag('Lesson02') for a in actors),'Already authored; inspect instead'
    assert LEVEL.save_current_level()
    before={a.get_path_name():snapshot(a) for a in actors}
    land=next(a for a in actors if isinstance(a,u.Landscape))
    assert u.Stage01TerrainBuilder.flatten_precision_field(land)
    # Local +X means right of a +Y-facing nose: UE world -X. All runtime geometry comes from actors.
    points=[('A2',13200,-8300),('P1',13200,-6300),('P2',11400,-6300),('P3',9600,-6300),('B2',9600,-8300)]
    for name,x,y in points:
        if name in ('A2','B2'):
            a=mesh(name,(x,y,0),(1,1,1),True,'/Game/TrainingField/Terrain/SM_TrainingPad','/Game/TrainingField/Materials/M_TrainingPad_NoZFight',90,tag='L2_'+name)
            a.static_mesh_component.set_collision_response_to_channel(GROUND,u.CollisionResponseType.ECR_BLOCK)
            for r in (330,365):
                p=[(x-r,y-r),(x+r,y-r),(x+r,y+r),(x-r,y+r)]
                for start,end in zip(p,p[1:]+p[:1]): line(name+' border',start,end)
        else:
            mesh(name+' center',(x,y,2),(.16,.16,.02),False,'/Engine/BasicShapes/Cylinder',tag='L2_'+name)
            for i in range(32):
                p=[(x+200*math.cos(j*2*math.pi/32),y+200*math.sin(j*2*math.pi/32)) for j in (i,i+1)]
                line(name+' radius 2m',*p,width=8)
            line(name+' cross X',(x-50,y),(x+50,y),width=10,z=4)
            line(name+' cross Y',(x,y-50),(x,y+50),width=10,z=4)
        text(name,name,x,y-150,size=160 if name in ('A2','B2') else 100)
    for (an,ax,ay),(bn,bx,by) in zip(points,points[1:]):
        # Dashed center guidance, not a physical corridor or an acceptance condition.
        length=math.hypot(bx-ax,by-ay); dx,dy=(bx-ax)/length,(by-ay)/length
        for d in range(500,int(length)-300,300):
            line(an+' to '+bn,(ax+dx*d,ay+dy*d),(ax+dx*(d+100),ay+dy*(d+100)),width=10,z=3)
    for label,x,y,yaw,dims,value in [('Initial direction',13200,-3100,-90,(3,.4,5),'I'),('After turn',7500,-6300,0,(.4,3,5),'II')]:
        z=trace(x,y)[5].z
        mesh(label,(x,y,z+250),dims,True)
        nx,ny=math.cos(math.radians(yaw)),math.sin(math.radians(yaw))
        text(label,value,x+nx*25,y+ny*25,z+300,180,yaw,False)
    assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    assert LEVEL.save_current_level()
    assert all(snapshot(a)==before[a.get_path_name()] for a in actors),'Existing actor changed'
    (EV/'preservation.json').write_text(json.dumps(dict(result='PASS',existing_actors_unchanged=len(actors),terrain_edit_world_cm=[6500,15500,-10500,-2500],first_sector_terrain_edit=False),indent=2))
    (EV/'geometry.json').write_text(json.dumps(dict(field_center_cm=[11000,-6500,0],flat_size_m=[70,60],shoulder_m=10,
        points=[dict(name=n,world_cm=[x,y,0]) for n,x,y in points],initial_yaw=90,turned_yaw=180,
        local_to_world='world_X_cm=11000-local_X_m*100; world_Y_cm=-6500+local_Y_m*100',status='authored_not_user_accepted'),indent=2))
    print('PRECISION_AUTHORING_PASS')

def validate():
    zs=[]
    # Stay one grid cell inside the shoulder seam: its edge normal averages the adjacent slope.
    for x in range(7600,14401,200):
        for y in range(-9400,-3599,200):
            d=trace(x,y); assert abs(d[5].z)<.2 and d[7].z>.999,(x,y,str(d)); zs.append(d[5].z)
    # Actual collision overlap across each 16m wide, 10m high swept corridor and 20m pad zones.
    actors=ACT.get_all_level_actors()
    boxes=[(12400,14000,-8300,-5500),(8800,14000,-7100,-5500),(8800,10400,-8300,-5500),(12200,14200,-9300,-7300),(8600,10600,-9300,-7300)]
    blocked=[]
    world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    ignore=[a for a in actors if isinstance(a,u.Landscape) or a.get_actor_label() in ('U2A - A2','U2A - B2')]
    for xmin,xmax,ymin,ymax in boxes:
        hits=u.SystemLibrary.box_overlap_actors(world,u.Vector((xmin+xmax)/2,(ymin+ymax)/2,501),u.Vector((xmax-xmin)/2,(ymax-ymin)/2,499),[u.ObjectTypeQuery.OBJECT_TYPE_QUERY1],None,ignore)
        if hits: blocked.extend(a.get_actor_label() for a in hits)
    assert not blocked,blocked
    (EV/'ground-and-corridor.json').write_text(json.dumps(dict(result='PASS',flat_collision_samples=len(zs),min_z_cm=min(zs),max_z_cm=max(zs),clear_width_m=16,clear_height_m=10,pad_clearance_m=[20,20],obstructions=blocked),indent=2))
    print('PRECISION_GEOMETRY_PASS',len(zs))

if __name__=='__main__':
    apply()
    validate()
