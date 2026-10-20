"""Turn each generated 3-node glTF scene into one Static Mesh with multiple material slots.
No geometry, UVs, materials or binary buffers are modified. Only glTF scene metadata
(nodes and mesh-primitives references) changes.
"""
import json,struct,sys
from pathlib import Path

def unify(path):
    path=Path(path)
    b=path.read_bytes()
    magic, version, total=struct.unpack_from('<4sII',b,0)
    assert magic==b'glTF' and version==2 and total==len(b)
    jl,jtype=struct.unpack_from('<I4s',b,12)
    assert jtype==b'JSON'
    d=json.loads(b[20:20+jl])
    if len(d.get('meshes',[]))<2: return
    assert all(len(m['primitives'])==1 for m in d['meshes'])
    assert d['scenes'][0]['nodes']==[0] and d['nodes'][0].get('children')==list(range(1,len(d['meshes'])+1))
    primitive_list=[m['primitives'][0] for m in d['meshes']]
    d['meshes']=[{'name':path.stem,'primitives':primitive_list}]
    d['nodes']=[{'name':'world','children':[1]},{'name':path.stem,'mesh':0}]
    payload=json.dumps(d,separators=(',',':')).encode('utf-8')
    payload+=b' '*((-len(payload))%4)
    rest=b[20+jl:]
    total=12+8+len(payload)+len(rest)
    out=struct.pack('<4sII',b'glTF',2,total)+struct.pack('<I4s',len(payload),b'JSON')+payload+rest
    path.write_bytes(out)

if __name__=='__main__':
    for fn in sys.argv[1:]:
        unify(fn)
