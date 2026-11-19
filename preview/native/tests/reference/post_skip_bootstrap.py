"""Fixture-only class bootstrap seam;all method bodies except <clinit> match.

The Source getStorageTypes method is exercised only with complete explicit
stored kinds. Its Core-dependent scheduler bootstrap is outside that domain.
Original class files and the Root/RD/constructor classes are never modified.
"""
from pathlib import Path
import hashlib,json,struct,sys
def prepare(original,classes):
 path=Path('com/biglybt/core/disk/impl/DiskManagerImpl.class');raw=(original/path).read_bytes();at=8
 def u2():
  nonlocal at
  v=struct.unpack_from('>H',raw,at)[0];at+=2;return v
 def u4():
  nonlocal at
  v=struct.unpack_from('>I',raw,at)[0];at+=4;return v
 count=u2();utf={};slot=1
 while slot<count:
  tag=raw[at];at+=1
  if tag==1:
   length=u2();utf[slot]=raw[at:at+length].decode('utf-8',errors='replace');at+=length
  elif tag in (3,4,9,10,11,12,17,18):at+=4
  elif tag in (5,6):at+=8;slot+=1
  elif tag in (7,8,16,19,20):at+=2
  elif tag==15:at+=3
  else:raise AssertionError('Unknown class constant tag '+str(tag))
  slot+=1
 at+=6;interfaces=u2();at+=2*interfaces
 def member(rewrite=False):
  nonlocal at
  start=at;access=u2();name=u2();descriptor=u2();attributes=u2();prefix=raw[start:at];parts=[];changed=False
  for _ in range(attributes):
   begin=at;attribute=u2();size=u4();data=raw[at:at+size];at+=size
   if rewrite and utf[name]=='<clinit>' and utf[attribute]=='Code':
    # No stack/locals/handlers/nested attributes;return. No method algorithm
    # or constant pool is changed,including getStorageTypes and its branches.
    data=struct.pack('>HHI',0,0,1)+b'\xb1'+struct.pack('>HH',0,0)
    parts.append(struct.pack('>HI',attribute,len(data))+data);changed=True
   else:parts.append(raw[begin:at])
  return prefix+b''.join(parts),changed,(utf[name],utf[descriptor]),raw[start:at]
 fields=u2()
 for _ in range(fields):member()
 methods_at=at;methods=u2();output=[raw[:at]];checks={};changed=0
 for _ in range(methods):
  value,different,key,before=member(True);output.append(value);changed+=different
  if not different:assert value==before
  checks[key[0]+key[1]]=dict(before=hashlib.sha256(before).hexdigest(),after=hashlib.sha256(value).hexdigest(),changed=different)
 output.append(raw[at:]);derived=b''.join(output);assert changed==1
 assert checks['getStorageTypes(Lcom/biglybt/core/download/DownloadManager;)[Ljava/lang/String;']['changed'] is False
 target=classes/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(derived)
 receipt=dict(original_sha256=hashlib.sha256(raw).hexdigest(),derived_sha256=hashlib.sha256(derived).hexdigest(),only_class_initializer_changed=True,constant_pool_unchanged=True,methods=checks,explicit_nonempty_kind_domain_only=True)
 (classes.parent/'disk-bootstrap-seam.json').write_text(json.dumps(receipt,indent=2)+'\n');return receipt
if __name__=='__main__':prepare(Path(sys.argv[1]),Path(sys.argv[2]))
