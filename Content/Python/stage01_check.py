"""Read-only acceptance of the loaded stage 01 map in live PIE/SIE.

Run via the UE Python console after starting PIE. Does not move or save actors.
"""
import json
import math
from pathlib import Path
import struct
import unreal

world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, "Start PIE/SIE: commandlet maps have no initialized physical bodies"
all_actors=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor)
landscapes=[a for a in all_actors if isinstance(a,unreal.Landscape)]
assert len(landscapes)==1
land=landscapes[0]
structure=unreal.Stage01TerrainBuilder.get_terrain_structure(land)
assert (structure.x,structure.y,structure.z)==(16,2,63)
origin,extent=land.get_actor_bounds(False)
for actual,wanted in [(origin.x-extent.x,-25200),(origin.x+extent.x,25200),
                      (origin.y-extent.y,-25200),(origin.y+extent.y,25200)]:
    assert abs(actual-wanted)<.1,(actual,wanted)
scale=land.get_actor_scale3d()
assert abs(scale.x-100)<.001 and abs(scale.y-100)<.001 and abs(scale.z-12.5)<.001

root=Path(unreal.Paths.project_dir()).resolve().parents[1]
source=root/"AssetsSource/Terrain/Stage01"
data=json.loads((source/"layout.json").read_text())
heights=struct.unpack("<"+"H"*(505*505),(source/"HF_TrainingField.r16").read_bytes())
results={"map":world.get_path_name(),"landscape":land.get_path_name(),"components":16,
         "bounds_cm":[origin.x-extent.x,origin.y-extent.y,origin.x+extent.x,origin.y+extent.y],
         "grid":[],"route":[],"gates":[],"pads":[]}


def ground(x,y):
    hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,6000),unreal.Vector(x,y,-1000),
        unreal.TraceTypeQuery.ECC_GROUND_SURFACE,False,[],unreal.DrawDebugTrace.NONE,True)
    assert hit is not None,("missing ground",x,y)
    parts=hit.to_tuple()
    position,normal=parts[5],parts[7]
    slope=math.degrees(math.acos(max(-1,min(1,normal.z))))
    return position.z,slope


def source_z(x,y):
    # Bilinear source sampling; physical Landscape triangulation differs slightly off-grid.
    fx=max(0,min(503.999999,x/100+252));fy=max(0,min(503.999999,y/100+252))
    ix,iy=int(fx),int(fy);u,v=fx-ix,fy-iy
    z=sum((heights[(iy+dy)*505+ix+dx]-32768)/10.24*wx*wy
          for dx,wx in [(0,1-u),(1,u)] for dy,wy in [(0,1-v),(1,v)])
    for a,b,g in (data['nodes_m'][0],data['nodes_m'][-1]):
        if abs(x-a*100)<=400 and abs(y-b*100)<=400:return g*100
    return z


def sweep(gate,dy,dz,blocked,half=(40,40,15)):
    center=gate.get_opening_center()+gate.get_actor_right_vector()*dy+gate.get_actor_up_vector()*dz
    normal=gate.get_actor_forward_vector()
    hit=unreal.SystemLibrary.box_trace_single(world,center-normal*200,center+normal*200,
        unreal.Vector(*half),gate.get_actor_rotation(),unreal.TraceTypeQuery.ECC_VISIBILITY,
        False,[],unreal.DrawDebugTrace.NONE,True)
    assert (hit is not None)==blocked,(gate.get_gate_id(),dy,dz,blocked,hit)
    return {"offset_cm":[dy,dz],"blocked":blocked}


for y in range(-25000,25001,2500):
    for x in range(-25000,25001,2500):
        z,slope=ground(x,y)
        assert abs(z-source_z(x,y))<.5,(x,y,z,source_z(x,y))
        results['grid'].append([x,y,z,slope])
for a,b in zip(data['nodes_m'],data['nodes_m'][1:]):
    length=math.dist(a[:2],b[:2]); steps=math.ceil(length/2)
    for i in range(steps+1):
        t=i/steps;x=(a[0]+(b[0]-a[0])*t)*100;y=(a[1]+(b[1]-a[1])*t)*100
        z,slope=ground(x,y)
        assert abs(z-source_z(x,y))<3,(x,y,z,source_z(x,y))
        assert slope<=8.01,("route slope",x,y,slope)
        results['route'].append([x,y,z,slope])
for x,y,z in [(-50,40,8),(125,115,12),(-125,145,18),(15,-135,-4)]:
    actual,_=ground(x*100,y*100)
    assert abs(actual-z*100)<.5,("terrain feature",x,y,actual,z)

gates=sorted((a for a in all_actors if isinstance(a,unreal.DroneTrainerGate)),key=lambda a:a.gate_index)
assert [a.gate_index for a in gates]==[1,2,3,4,5]
for gate,node,yaw in zip(gates,data['nodes_m'][1:-1],data['gate_yaws']):
    position=gate.get_actor_location();rotation=gate.get_actor_rotation();size=gate.get_actor_scale3d()
    assert max(abs(position.x-node[0]*100),abs(position.y-node[1]*100),abs(position.z-node[2]*100))<.01
    assert abs(rotation.yaw-yaw)<.01 and abs(rotation.pitch)<.01 and abs(rotation.roll)<.01
    assert max(abs(size.x-1),abs(size.y-1),abs(size.z-1))<.001
    center=gate.get_opening_center();assert abs(center.z-position.z-300)<.01
    assert gate.opening_half_size==unreal.Vector2D(200,200)
    # Mesh ground is -100 and visible opening center +15.384615 in accepted normalized FBX.
    transform=gate.frame.get_world_transform()
    visible=unreal.MathLibrary.transform_location(transform,unreal.Vector(0,0,15.3846153846154))
    assert (visible-center).length()<.01,(visible,center)
    mesh=gate.frame.static_mesh;geometry=mesh.get_editor_property("body_setup").get_editor_property("agg_geom")
    assert len(geometry.get_editor_property("box_elems"))==8
    assert len(geometry.get_editor_property("convex_elems"))==0
    terrace=[]
    for dx in (-400,0,400):
        for dy in (-400,0,400):
            z,slope=ground(position.x+dx,position.y+dy)
            assert abs(z-position.z)<.5 and slope<=2.01,(gate.get_gate_id(),z,slope)
            terrace.append([dx,dy,z,slope])
    probes=[sweep(gate,dy,dz,blocked) for dy,dz,blocked in
            [(0,0,False),(-155,0,False),(155,0,False),(0,-180,False),(0,180,False),
             (-210,0,True),(210,0,True),(0,-210,True),(0,210,True)]]
    hit=unreal.SystemLibrary.line_trace_single(world,center-gate.get_actor_forward_vector()*4000,
        center,unreal.TraceTypeQuery.ECC_VISIBILITY,False,[],unreal.DrawDebugTrace.NONE,True)
    assert hit is None,("40m approach occluded",gate.get_gate_id(),hit)
    results['gates'].append(dict(id=str(gate.get_gate_id()),path=gate.get_path_name(),frame=gate.frame.get_path_name(),
        position_cm=[position.x,position.y,position.z],yaw=rotation.yaw,
        center_cm=[center.x,center.y,center.z],terrace=terrace,probes=probes))

pads=[a for a in all_actors if isinstance(a,unreal.StaticMeshActor) and a.get_actor_label() in ['StartPad','LandingPad']]
assert len(pads)==2
for pad in pads:
    origin,extent=pad.get_actor_bounds(False);position=pad.get_actor_location()
    assert abs(extent.x*2-800)<.01 and abs(extent.y*2-800)<.01
    assert abs(origin.z+extent.z-position.z)<.01
    for dx in (-350,0,350):
        for dy in (-350,0,350):
            z,slope=ground(position.x+dx,position.y+dy)
            assert abs(z-position.z)<.5 and slope<=1.01
    results['pads'].append(dict(path=pad.get_path_name(),top_cm=position.z))
layouts=[a for a in all_actors if isinstance(a,unreal.DroneTrainerLevelLayout)];assert len(layouts)==1
layout=layouts[0];assert layout.flight_min==unreal.Vector2D(-20000,-20000) and layout.flight_max==unreal.Vector2D(20000,20000)
assert layout.warning_xy==18000 and layout.max_agl==5000 and layout.warning_agl==4000
assert layout.corridor_width==1600 and layout.corridor_height==1000
assert [a.gate_index for a in layout.gates]==[1,2,3,4,5]
# Actual spawn reference and the current Pawn body are above ground with clearance.
spawn=layout.start.get_actor_location()
assert abs(spawn.x+12000)<.01 and abs(spawn.y+10000)<.01 and abs(spawn.z-50)<.01
spawn_hit=unreal.SystemLibrary.box_trace_single(world,spawn,spawn+unreal.Vector(0,0,1),
    unreal.Vector(40,40,15),unreal.Rotator(),unreal.TraceTypeQuery.ECC_VISIBILITY,
    False,[],unreal.DrawDebugTrace.NONE,True)
assert spawn_hit is None,('spawn overlaps world',spawn_hit)
assert spawn.z-15-ground(spawn.x,spawn.y)[0]>30
results['spawn_cm']=[spawn.x,spawn.y,spawn.z]
# The full 16 m x 10 m route volume is clear except for the intended gate frames.
results['corridor_clearance_probes']=0
for a,b in zip(data['nodes_m'],data['nodes_m'][1:]):
    count=math.ceil(math.dist(a[:2],b[:2])/4)
    yaw=math.degrees(math.atan2(b[1]-a[1],b[0]-a[0]))
    for i in range(count+1):
        t=i/count;x=(a[0]+t*(b[0]-a[0]))*100;y=(a[1]+t*(b[1]-a[1]))*100
        z,_=ground(x,y)
        center=unreal.Vector(x,y,z+510)
        hit=unreal.SystemLibrary.box_trace_single(world,center,center+unreal.Vector(0,0,1),
            unreal.Vector(50,800,500),unreal.Rotator(pitch=0,yaw=yaw,roll=0),
            unreal.TraceTypeQuery.ECC_VISIBILITY,False,gates,unreal.DrawDebugTrace.NONE,True)
        assert hit is None,('blocked corridor',x,y,hit)
        results['corridor_clearance_probes']+=1

# Horizontal rays at 50 m intersect the actual authored ring before its outer edge.
# Slice its source triangles, rather than treating a disabled collision trace as visibility proof.
bg=json.loads((source/'background.json').read_text());vertices=bg['vertices_cm'];indices=bg['triangles']
assert len(indices)//3<=20000
backgrounds=[a for a in all_actors if isinstance(a,unreal.StaticMeshActor) and a.get_actor_label()=='T03 - Background terrain']
assert len(backgrounds)==1
assert unreal.Stage01TerrainBuilder.get_geometry_triangle_count(backgrounds[0].static_mesh_component.static_mesh)==len(indices)//3
bound=backgrounds[0].static_mesh_component.static_mesh.get_bounding_box()
assert abs(bound.min.x+75000)<.1 and abs(bound.max.x-75000)<.1
segments=[]
for i in range(0,len(indices),3):
    tri=[vertices[indices[i+j]] for j in range(3)];points=[]
    for p,q in zip(tri,tri[1:]+tri[:1]):
        if (p[2]-5000)*(q[2]-5000)<0:
            t=(5000-p[2])/(q[2]-p[2]);points.append((p[0]+t*(q[0]-p[0]),p[1]+t*(q[1]-p[1])))
    if len(points)==2:segments.append(points)
def cross(a,b):return a[0]*b[1]-a[1]*b[0]
results['horizon_rays']=[]
for ox,oy in [(0,0),(-18000,-18000),(-18000,18000),(18000,-18000),(18000,18000)]:
    for angle in range(0,360,15):
        direction=(math.cos(math.radians(angle)),math.sin(math.radians(angle)));nearest=float('inf')
        for p,q in segments:
            edge=(q[0]-p[0],q[1]-p[1]);offset=(p[0]-ox,p[1]-oy);den=cross(direction,edge)
            if abs(den)<1e-8:continue
            t=cross(offset,edge)/den;u=cross(offset,direction)/den
            if t>0 and 0<=u<=1:nearest=min(nearest,t)
        assert nearest<110000,('open horizon',ox,oy,angle)
        results['horizon_rays'].append([ox,oy,angle,nearest])
results['background_triangles']=len(indices)//3

results['layout']=layout.get_path_name()
results['status']='passed'
out=Path(unreal.Paths.project_saved_dir())/'Stage01-check.json'
out.write_text(json.dumps(results,indent=2))
print('STAGE01_CHECK_PASS',len(results['grid']),len(results['route']),sum(len(g['probes']) for g in results['gates']),out)
