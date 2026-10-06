from pathlib import Path
import struct,json,sys
b=Path(sys.argv[1]).read_bytes();p=0

def I():
 global p
 v=struct.unpack_from('<I',b,p)[0];p+=4;return v

def S():
 global p
 n=I();assert n<100000,(p,n)
 v=b[p:p+n].decode();p+=n;return v
sources={I():S() for _ in range(I())}
globals_=[(S(),S()) for _ in range(I())]
unknown=I();names=[S() for _ in range(I())]
count=I();records=[]
for idx in range(count):
 start=p
 try:
  sig=S();src=I();line=I();deps=[(I(),S()) for _ in range(I())]
  code_size=I();blob_size=I();body=p;p+=blob_size
  reloc=I()
  assert reloc==0,('reloc',reloc)
  unwind=I();p+=unwind;stack=I();name=S();ret=S()
  args=[(S(),S(),I()) for _ in range(I())]
  locals_=[(S(),S(),I()) for _ in range(I())]
  maps=[]
  for _ in range(I()):
   off=struct.unpack_from('<Q',b,p)[0];p+=8;ln=I();ds=[I() for _ in range(I())];maps.append((off,ln,ds))
  records.append(dict(index=idx,signature=sig,source=sources.get(src),line=line,dependencies=deps,code_size=code_size,blob_size=blob_size,body_offset=body,name=name,return_type=ret,args=args,locals=locals_,maps=maps))
 except Exception as e:
  print('ERROR',idx,start,p,e);raise
Path(sys.argv[2]).write_text(json.dumps(records))
print('Parsed',len(records),'records; end',p,'of',len(b))
