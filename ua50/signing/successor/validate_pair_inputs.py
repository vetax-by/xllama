"""Private lane preflight only; performs no key/trust/SDK/device operation."""
import argparse,hashlib,json,re,stat
from pathlib import Path,PurePosixPath
import package_copy as package
CURRENT_PATCH_LOCK='9b9659dc28c63383aa89c10eafc9a8c625ee6d5d659030941c808f4303d9c372'
def exact_file(root,relative):
 if not isinstance(relative,str) or not relative or '\\' in relative or ':' in relative or relative.startswith('/') or any(x in ('','.','..') for x in relative.split('/')):raise ValueError('canonical relative artifact file required')
 path=root.joinpath(*PurePosixPath(relative).parts)
 for component in (root,*(root.joinpath(*path.relative_to(root).parts[:i]) for i in range(1,len(path.relative_to(root).parts)+1))):
  info=component.lstat()
  if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:raise ValueError('artifact symlink/reparse refused')
 if not path.is_file():raise ValueError('regular artifact input required')
 return path

def validate(plan,root):
 if set(plan)!= {'schema','InstalledVersion','PatchedVersion','RecoveryVersion','Patched','Stock'} or plan['schema']!='ua50-sign-pair-input-v1':raise ValueError('exact pair input schema required')
 iv,pv,rv=(package.version(plan[k]) for k in ('InstalledVersion','PatchedVersion','RecoveryVersion'))
 if not iv<pv<rv:raise ValueError('fresh installed < patched < forward-recovery required')
 out={k:plan[k]for k in ('InstalledVersion','PatchedVersion','RecoveryVersion')}
 for arm,track in (('Patched','patch'),('Stock','baseline')):
  entry=plan[arm]
  if set(entry)!= {'Package','PackageSHA256','SourceReceipt','SourceReceiptSHA256'}:raise ValueError('exact arm input schema')
  for k in ('PackageSHA256','SourceReceiptSHA256'):
   if not re.fullmatch('[0-9a-f]{64}',entry[k]):raise ValueError('actual accepted lowercase SHA required; placeholders refuse')
  pkg=exact_file(root,entry['Package']);receipt=exact_file(root,entry['SourceReceipt'])
  if receipt.stat().st_size>1024*1024:raise ValueError('source receipt1MiB cap before read')
  raw=receipt.read_bytes()
  if hashlib.sha256(raw).hexdigest()!=entry['SourceReceiptSHA256']:raise ValueError('accepted source receipt bytes changed')
  record=json.loads(raw)
  if record['track']!=track or record['xllama']!='4db51522c850e62d0bb24234dfeb48e55f4017d7' or record['llama']!='d81235049384534c167caea52b85a694f6103d14':raise ValueError('source receipt track/base')
  if track=='patch':
   if record['reviewed_patch_lock']!=CURRENT_PATCH_LOCK or record['selected_shipping_files_verified']!=81:raise ValueError('UA50 successor 9b96/81 receipt required')
  elif record['reviewed_patch_lock'] or record['selected_shipping_files_verified']!=0:raise ValueError('stock must be original unpatched baseline')
  if package.bounded_package_sha(pkg)!=entry['PackageSHA256']:raise ValueError('accepted unsigned package bytes changed')
  payload=package.read_package(pkg,False);_,identity=package.identity(payload['AppxManifest.xml'])
  proposed=plan['PatchedVersion'if track=='patch'else'RecoveryVersion']
  if package.version(proposed)<=package.version(identity.getAttribute('Version')):raise ValueError('proposed version must also exceed artifact version')
  out[arm]={'Package':str(pkg),'PackageSHA256':entry['PackageSHA256'],'SourceReceipt':str(receipt),'SourceReceiptSHA256':entry['SourceReceiptSHA256'],'Track':track}
 return out
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--plan',type=Path,required=True);p.add_argument('--input-root',type=Path,required=True);a=p.parse_args()
 if a.plan.stat().st_size>65536:p.error('plan64KiB cap before read')
 print(json.dumps(validate(json.loads(a.plan.read_text()),a.input_root.resolve())))
