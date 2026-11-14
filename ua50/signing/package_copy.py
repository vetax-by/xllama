"""Future private signing-lane package-only helper. No crypto/key/device operations."""
import argparse, hashlib, json, os, re, stat, sys, zipfile
from pathlib import Path, PurePosixPath
from xml.dom import minidom
LIMIT=400*1024*1024
GENERATED={'AppxBlockMap.xml','[Content_Types].xml','AppxSignature.p7x'}
DENY={'.gguf','.safetensors','.pfx','.p12','.pem','.key'}
CODE_INTEGRITY='AppxMetadata/CodeIntegrity.cat'
NEW_CODE_INTEGRITY_LIMIT=64*1024
def sha(data):return hashlib.sha256(data).hexdigest()
def bounded_package_sha(path):
 # Reject oversized input BEFORE open/read/hash/allocation; enforce again while reading.
 before=path.stat()
 if before.st_size>LIMIT:raise ValueError('package400MiB cap before read')
 digest=hashlib.sha256();count=0
 with path.open('rb') as stream:
  held=os.fstat(stream.fileno())
  if held.st_size>LIMIT:raise ValueError('package400MiB held cap before read')
  while True:
   data=stream.read(min(1024*1024,LIMIT-count+1))
   if not data:break
   count+=len(data)
   if count>LIMIT:raise ValueError('package400MiB streaming cap')
   digest.update(data)
  after=os.fstat(stream.fileno())
 final=path.stat()
 signature=lambda s:(s.st_dev,s.st_ino,s.st_size,s.st_mtime_ns,s.st_ctime_ns)
 if signature(held)!=signature(after) or (held.st_dev,held.st_ino,held.st_size)!=(before.st_dev,before.st_ino,before.st_size) or (after.st_dev,after.st_ino,after.st_size)!=(final.st_dev,final.st_ino,final.st_size) or count!=held.st_size:
  raise ValueError('package changed during bounded hash')
 return digest.hexdigest()
def version(value):
 if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)',value):raise ValueError('four decimal version components required')
 parts=tuple(map(int,value.split('.')))
 if not 0<parts[0]<=65535 or any(p>65535 for p in parts):raise ValueError('version component range')
 return parts
def read_package(path,signed):
 if path.stat().st_size>LIMIT:raise ValueError('package400MiB cap')
 result={};folded=set();total=0
 with zipfile.ZipFile(path) as archive:
  if len(archive.infolist())>20000:raise ValueError('entry count cap')
  for entry in archive.infolist():
   raw=entry.filename;name=PurePosixPath(raw)
   if '\\' in raw or ':' in raw or raw.startswith('/') or any(p in ('','.','..') for p in raw.split('/')) or str(name)!=raw:raise ValueError('noncanonical package path')
   if raw.casefold() in folded:raise ValueError('duplicate/case collision')
   folded.add(raw.casefold())
   if entry.is_dir() or stat.S_ISLNK(entry.external_attr>>16) or entry.flag_bits&1:raise ValueError('directory/link/encrypted entry refused')
   if name.suffix.lower() in DENY:raise ValueError('weight/private-key-like package member refused')
   if raw=='AppxSignature.p7x' and not signed:raise ValueError('input must be unsigned')
   total+=entry.file_size
   if total>1024*1024*1024:raise ValueError('unpacked1GiB cap')
   result[raw]=archive.read(entry)
 if 'AppxManifest.xml' not in result:raise ValueError('manifest missing')
 if signed and 'AppxSignature.p7x' not in result:raise ValueError('signed package has no signature member')
 return result
def identity(data):
 if b'<!DOCTYPE' in data.upper() or b'<!ENTITY' in data.upper():raise ValueError('DTD/entity manifest refused')
 doc=minidom.parseString(data)
 nodes=doc.getElementsByTagNameNS('http://schemas.microsoft.com/appx/manifest/foundation/windows10','Identity')
 if len(nodes)!=1 or nodes[0].parentNode!=doc.documentElement:raise ValueError('exact root Identity required')
 node=nodes[0]
 if tuple(node.getAttribute(k) for k in ('Name','Publisher','ProcessorArchitecture'))!=('GianlucaMazza.xllama','CN=xllama-dev','x64'):raise ValueError('identity/publisher/architecture changed')
 version(node.getAttribute('Version'))
 return doc,node
def prepare(package,expected_sha,installed,new,work):
 if bounded_package_sha(package)!=expected_sha:raise ValueError('accepted unsigned SHA mismatch')
 payload=read_package(package,False);doc,node=identity(payload['AppxManifest.xml'])
 original_version=node.getAttribute('Version')
 if version(new)<=version(installed) or version(new)<=version(original_version):raise ValueError('new version must exceed fresh installed AND input version')
 original_xml=doc.toxml(encoding='utf-8');node.setAttribute('Version',new);changed=doc.toxml(encoding='utf-8')
 node.setAttribute('Version',original_version)
 if doc.toxml(encoding='utf-8')!=original_xml:raise ValueError('unexpected manifest mutation')
 payload['AppxManifest.xml']=changed
 work=work.resolve()
 if any((parent/'.git').exists() for parent in (work,*work.parents)):raise ValueError('private scratch outside repository required')
 work.mkdir(exist_ok=False);tree=work/'unpacked';tree.mkdir()
 hashes={}
 for name,data in payload.items():
  if name in GENERATED:continue
  target=tree.joinpath(*PurePosixPath(name).parts);target.parent.mkdir(parents=True,exist_ok=True)
  with target.open('xb') as stream:stream.write(data)
  hashes[name]={'bytes':len(data),'sha256':sha(data)}
 record={'schema':'ua50-signed-copy-source-plan-v1','unsigned_sha256':expected_sha,'installed_version_input':installed,'original_package_version':original_version,'new_version':new,'payload':hashes}
 (work/'COPY-PLAN.json').write_text(json.dumps(record,indent=2)+'\n')
 return record
def payload_mismatch_report(expected,actual):
 # Only bounded archive-relative member identifiers + lengths/digests; never bytes/paths.
 def metadata(value):
  if not isinstance(value,dict):return {'metadata':'INVALID'}
  n=value.get('bytes');digest=value.get('sha256')
  if type(n) is not int or not 0<=n<=1024*1024*1024 or not isinstance(digest,str) or not re.fullmatch('[0-9a-f]{64}',digest):return {'metadata':'INVALID'}
  return {'bytes':n,'sha256':digest}
 def member_label(name):
  canonical=isinstance(name,str) and name and not any(c in name for c in ('\\',':')) and not name.startswith('/') and all(p not in ('','.','..') for p in name.split('/')) and str(PurePosixPath(name))==name
  if not canonical:return {'member':'INVALID_RELATIVE_MEMBER'}
  label={'member':name[:256]}
  if len(name)>256:label.update(member_truncated=True,full_member_name_sha256=sha(name.encode('utf-8')))
  return label
 keys=sorted(set(expected)|set(actual))
 differences=[(name,'added' if name not in expected else 'missing' if name not in actual else 'changed') for name in keys if expected.get(name)!=actual.get(name)]
 report={'schema':'ua50-signed-payload-difference-v1','difference_count':len(differences),'added_count':sum(k=='added' for _,k in differences),'missing_count':sum(k=='missing' for _,k in differences),'changed_count':sum(k=='changed' for _,k in differences),'entries':[],'truncated':False}
 for name,kind in differences:
  if len(report['entries'])>=20:report['truncated']=True;break
  entry=member_label(name)|{'kind':kind,'expected':metadata(expected[name]) if name in expected else None,'actual':metadata(actual[name]) if name in actual else None}
  report['entries'].append(entry)
  if len(json.dumps(report,ensure_ascii=True,separators=(',',':')).encode('ascii'))>16384:
   report['entries'].pop();report['truncated']=True;break
 return report
def verify(package,plan_path):
 plan=json.loads(plan_path.read_text());payload=read_package(package,True);doc,node=identity(payload['AppxManifest.xml'])
 if node.getAttribute('Version')!=plan['new_version']:raise ValueError('signed manifest version mismatch')
 actual={name:{'bytes':len(data),'sha256':sha(data)} for name,data in payload.items() if name not in GENERATED}
 # Adopt ONLY the exact new SDK CI footprint when absent from accepted input plan.
 # Existing planned CI remains in strict comparison, including its exact bytes/hash.
 compared=actual.copy();adopted={}
 if CODE_INTEGRITY not in plan['payload'] and CODE_INTEGRITY in actual:
  ci=actual[CODE_INTEGRITY]
  if 0<ci['bytes']<=NEW_CODE_INTEGRITY_LIMIT:
   adopted[CODE_INTEGRITY]=ci;del compared[CODE_INTEGRITY]
 if compared!=plan['payload']:
  report=payload_mismatch_report(plan['payload'],compared)
  print(json.dumps(report,ensure_ascii=True,separators=(',',':')),file=sys.stderr,flush=True)
  raise ValueError('signed-copy payload changed outside planned manifest-only version edit')
 ci_mode='preexisting_exact' if CODE_INTEGRITY in plan['payload'] else 'generated_new' if adopted else 'absent'
 ci_record={'member':CODE_INTEGRITY,'mode':ci_mode,'actual':actual.get(CODE_INTEGRITY),'new_footprint_max_bytes':NEW_CODE_INTEGRITY_LIMIT}
 return {'signed_package_sha256':bounded_package_sha(package),'payload_files_verified':len(compared),'new_version':plan['new_version'],'crypto_signature_status':'REQUIRES_WINDOWS_SIGTOOL_AND_AUTHENTICODE','footprint_policy':'exact-new-codeintegrity-absent-plan-v1','adopted_footprints':adopted,'code_integrity_footprint':ci_record}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('operation',choices=('prepare','verify'));p.add_argument('--package',type=Path,required=True);p.add_argument('--sha256');p.add_argument('--installed');p.add_argument('--new');p.add_argument('--work',type=Path);p.add_argument('--plan',type=Path);a=p.parse_args()
 if a.operation=='prepare':
  if not all((a.sha256,a.installed,a.new,a.work)):p.error('prepare requires SHA/installed/new/work')
  result=prepare(a.package,a.sha256,a.installed,a.new,a.work)
 else:
  if not a.plan:p.error('verify requires plan')
  result=verify(a.package,a.plan)
 print(json.dumps(result))
