"""Private lane preflight only; performs no key/trust/SDK/device operation."""
import argparse,hashlib,json,re,stat
from pathlib import Path,PurePosixPath
import package_copy as package
PATCH_BINDING = {'reviewed_patch_lock': 'ee2b5f939e2ec7b069453ab8d8a3e4693e75258b256727264e757b5987cd3da9', 'selected_shipping_files_verified': 102, 'bounded_source_lock': 'a18d4051e6572291d49b8c0a8c70f5ed7a18739cf1d024040f60e4b070b5affd', 'bounded_postimages_verified': 136, 'native_manifest_sha256': '41a549e1c7df0351d173142d98ea097c30975398d8178f5140dd2ab35db7831f', 'engine_sha256': '50f33f1edd0fdc5909fe9e60eaa3cb7e907511fe8d9cf3d0f6fb641c252c723b', 'packer_source_sha256': 'd8f4693e29c9266bb14ed5b573d60a858dfb9eaf7523343b2020daa975b738bf', 'source_only_not_native_qualification': True}
def exact_file(root,relative):
 if not isinstance(relative,str) or not relative or '\\' in relative or ':' in relative or relative.startswith('/') or any(x in ('','.','..') for x in relative.split('/')):raise ValueError('canonical relative artifact file required')
 path=root.joinpath(*PurePosixPath(relative).parts)
 for component in (root,*(root.joinpath(*path.relative_to(root).parts[:i]) for i in range(1,len(path.relative_to(root).parts)+1))):
  info=component.lstat()
  if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:raise ValueError('artifact symlink/reparse refused')
 if not path.is_file():raise ValueError('regular artifact input required')
 return path

PLAN_SCHEMA = 'ua50-bounded-20261127-sign-pair-input-v1'
XLLAMA = '4db51522c850e62d0bb24234dfeb48e55f4017d7'
LLAMA = 'd81235049384534c167caea52b85a694f6103d14'
OLD_UNSIGNED = '05c9d3d14cd6ea1b674918b9d5e87489b66aa6d6c06a1ee95aa85daef4115cb4'


def read_receipt(path, expected):
    if path.stat().st_size > 1024 * 1024:
        raise ValueError('receipt exceeds 1 MiB')
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != expected:
        raise ValueError('accepted receipt bytes changed')
    return json.loads(raw)


def check_source(record, track):
    if (record.get('track'), record.get('xllama'), record.get('llama')) != (track, XLLAMA, LLAMA):
        raise ValueError('source receipt track/base')
    if track == 'patch':
        for key, value in PATCH_BINDING.items():
            actual = record.get(key)
            if type(actual) is not type(value) or actual != value:
                raise ValueError('2026-11-27 source receipt mismatch: ' + key)
    elif record['reviewed_patch_lock'] is not None or record['selected_shipping_files_verified'] != 0:
        raise ValueError('stock must be original unpatched baseline')


def check_build(record, pkg, package_sha):
    if (record.get('schema'), record.get('track'), record.get('xllama'), record.get('llama')) != (
        'ua50-windows-unsigned-v1', 'patch', XLLAMA, LLAMA
    ):
        raise ValueError('build receipt schema/track/base')
    if record.get('signed') is not False or record.get('deploy') is not False:
        raise ValueError('unsigned and undeployed build required')
    if type(record.get('model_runs')) is not int or record['model_runs'] != 0:
        raise ValueError('build receipt model_runs must be zero')
    received = record.get('package')
    expected = {'name': pkg.name, 'bytes': pkg.stat().st_size, 'sha256': package_sha}
    if not isinstance(received, dict) or type(received.get('bytes')) is not int or received != expected:
        raise ValueError('build receipt does not bind exact unsigned package')


def validate(plan, root):
    versions = ('InstalledVersion', 'PatchedVersion', 'RecoveryVersion')
    if set(plan) != {'schema', *versions, 'Patched', 'Stock'} or plan['schema'] != PLAN_SCHEMA:
        raise ValueError('exact 2026-11-27 signing plan schema required')
    if any(not isinstance(plan[key], str) for key in versions):
        raise ValueError('actual version inputs required')
    iv, pv, rv = (package.version(plan[key]) for key in versions)
    if not iv < pv < rv:
        raise ValueError('fresh installed < patched < forward-recovery required')
    out = {key: plan[key] for key in versions}
    for arm, track in (('Patched', 'patch'), ('Stock', 'baseline')):
        entry = plan[arm]
        leaves = ['Package', 'SourceReceipt']
        if arm == 'Patched':
            leaves.append('BuildReceipt')
        if set(entry) != {key for leaf in leaves for key in (leaf, leaf + 'SHA256')}:
            raise ValueError('exact arm input schema required')
        for leaf in leaves:
            value = entry[leaf + 'SHA256']
            if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{64}', value):
                raise ValueError('actual accepted lowercase SHA required')
        if arm == 'Patched' and entry['PackageSHA256'] == OLD_UNSIGNED:
            raise ValueError('historical 1526 unsigned package refused')
        files = {leaf: exact_file(root, entry[leaf]) for leaf in leaves}
        check_source(read_receipt(files['SourceReceipt'], entry['SourceReceiptSHA256']), track)
        pkg = files['Package']
        if package.bounded_package_sha(pkg) != entry['PackageSHA256']:
            raise ValueError('accepted unsigned package bytes changed')
        if arm == 'Patched':
            check_build(read_receipt(files['BuildReceipt'], entry['BuildReceiptSHA256']),
                        pkg, entry['PackageSHA256'])
        payload = package.read_package(pkg, False)
        _, identity = package.identity(payload['AppxManifest.xml'])
        proposed = plan['PatchedVersion' if arm == 'Patched' else 'RecoveryVersion']
        if package.version(proposed) <= package.version(identity.getAttribute('Version')):
            raise ValueError('proposed version must also exceed artifact version')
        out[arm] = {**entry, **{key: str(path) for key, path in files.items()}, 'Track': track}
    return out


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--input-root', type=Path, required=True)
    args = parser.parse_args()
    if args.plan.stat().st_size > 65536:
        parser.error('plan exceeds 64 KiB')
    print(json.dumps(validate(json.loads(args.plan.read_bytes()), args.input_root.resolve())))
