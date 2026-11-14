"""Actions source preparation only. Never invoked locally against a live checkout."""
import hashlib
import json
import re
import stat
from pathlib import Path, PurePosixPath
import subprocess
import sys

XLLAMA = '4db51522c850e62d0bb24234dfeb48e55f4017d7'
LLAMA = 'd81235049384534c167caea52b85a694f6103d14'

def git(cwd, *args):
    return subprocess.run(['git', '-C', str(cwd), *args], check=True,
                          capture_output=True, text=True, timeout=180).stdout.strip()

def checked_patch(operator, entry):
    raw = entry['file']
    if not isinstance(raw, str) or not raw.startswith('patch/'):
        raise ValueError('patch path must be patch/FILENAME')
    parts = raw.split('/')
    if len(parts) != 2 or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*\.patch', parts[1]):
        raise ValueError('simple patch filename required; no Windows separators/drive syntax')
    # lstat each owned namespace component: Windows junctions/reparse points
    # can be directories without Path.is_symlink() being true.
    parent = operator / 'patch'
    path = parent / parts[1]
    for component in (operator, parent, path):
        info = component.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('symlink/reparse namespace refused')
    root = operator.resolve(strict=True)
    resolved_parent = parent.resolve(strict=True)
    resolved_path = path.resolve(strict=True)
    if resolved_parent != root / 'patch' or resolved_path.parent != resolved_parent or not resolved_path.is_relative_to(root):
        raise ValueError('patch input escaped owned namespace')
    if not stat.S_ISREG(path.lstat().st_mode):
        raise ValueError('missing regular patch')
    data = path.read_bytes()
    if not 0 < len(data) <= 4 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != entry['sha256']:
        raise ValueError('patch hash/size mismatch')
    if entry['target'] not in ('xllama', 'llama'):
        raise ValueError('patch target')
    return path.resolve()

CURRENT_PATCH_LOCK = '760ed6df52cffcc92de714fd35e1b796d6697ad82e354159f7de73f92c8d2846'
STOCK_GUARD_SHA = '1041743635f5d67e8c64591727b627095532c62d832646ca49468cadd9ff8ccb'
def admitted_runtime_lock(operator, lock_sha):
    if lock_sha != CURRENT_PATCH_LOCK:
        raise ValueError('only current shipping-v5/760e lock admitted; standalone a199/e781 refused')
    raw = (operator / 'PATCH-LOCK.json').read_bytes()
    if hashlib.sha256(raw).hexdigest() != CURRENT_PATCH_LOCK:
        raise ValueError('current patch lock bytes changed')
    lock = json.loads(raw)
    if lock['xllama'] != XLLAMA or lock['llama'] != LLAMA:
        raise ValueError('current runtime base mismatch')
    if [(e['file'], e['target']) for e in lock['patches']] != [
            ('patch/runtime-root.patch', 'xllama'), ('patch/runtime-llama.patch', 'llama'),
            ('patch/app-xllama.patch', 'xllama'), ('patch/app-llama.patch', 'llama')]:
        raise ValueError('exact runtime-root/runtime-llama/app-xllama/app-llama four-patch order required')
    for entry in lock['patches']:
        patch = checked_patch(operator, entry)
        if patch.stat().st_size != entry['bytes']:
            raise ValueError('patch byte count mismatch')
    return lock

SHIPPING_INVENTORY_SHA = '250e0bd298204856ac644de739ea9f19ffe681a676f62a2d33fa8be5b7f4a368'
COMPILER_INVENTORY_SHA = 'a0f96caad25c29b45ce42bddf711750264db2ea797a77b196b5e3fde76145954'

def admitted_shipping_inventory(operator):
    raw = (operator / 'SHIPPING-FILE-INVENTORY.json').read_bytes()
    if hashlib.sha256(raw).hexdigest() != SHIPPING_INVENTORY_SHA:
        raise ValueError('shipping inventory changed')
    imports = (operator / 'COMPILER-IMPORT-INVENTORY.json').read_bytes()
    if hashlib.sha256(imports).hexdigest() != COMPILER_INVENTORY_SHA:
        raise ValueError('compiler/import declarations changed')
    entries = json.loads(raw)['selected_materialized_files']
    if len(entries) != 40:
        raise ValueError('exact forty selected shipping files required')
    seen = set()
    for entry in entries:
        target, path = entry['target'], entry['target_path']
        if target not in ('xllama', 'llama') or not isinstance(path, str):
            raise ValueError('shipping target/path')
        parts = path.split('/')
        if not parts or any(not re.fullmatch(r'[A-Za-z0-9_.-]+', p) or p in ('.', '..') for p in parts):
            raise ValueError('canonical relative shipping path required')
        if (target, path) in seen:
            raise ValueError('duplicate shipping target/path')
        seen.add((target, path))
    return entries

def verify_shipping_postimages(source, entries):
    # These are selected current source bytes, not SDK imports or a build proof.
    for entry in entries:
        root = source if entry['target'] == 'xllama' else source / 'llama.cpp'
        path = root.joinpath(*entry['target_path'].split('/'))
        relative = path.relative_to(root)
        current = root
        for part in relative.parts:
            current = current / part
            info = current.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
                raise ValueError('shipping symlink/reparse refused')
        if not stat.S_ISREG(path.lstat().st_mode) or not path.resolve(strict=True).is_relative_to(root.resolve(strict=True)):
            raise ValueError('shipping file containment/type')
        data = path.read_bytes()
        if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError('shipping postimage missing/changed: ' + entry['target_path'])
    return len(entries)

def main(source, operator, track, lock_sha=''):
    source, operator = Path(source).resolve(), Path(operator).resolve()
    if track not in ('baseline', 'patch'):
        raise ValueError('track')
    lock = admitted_runtime_lock(operator, lock_sha) if track == 'patch' else None
    inventory = admitted_shipping_inventory(operator) if track == 'patch' else None
    if track == 'baseline' and lock_sha:
        raise ValueError('baseline must not carry a runtime lock')
    guards = list((source / 'patches').glob('0*-*.patch'))
    if [p.name for p in guards] != ['0001-uwp-appcontainer-guards.patch'] or hashlib.sha256(guards[0].read_bytes()).hexdigest() != STOCK_GUARD_SHA:
        raise ValueError('exact shipping stock guard inventory required')
    if git(source, 'rev-parse', 'HEAD') != XLLAMA:
        raise ValueError('xllama identity')
    if git(source, 'status', '--porcelain'):
        raise ValueError('dirty source')
    git(source, 'submodule', 'update', '--init', '--depth', '1', '--', 'llama.cpp')
    if git(source / 'llama.cpp', 'rev-parse', 'HEAD') != LLAMA:
        raise ValueError('llama identity')
    subprocess.run(['bash', 'scripts/apply-uwp-patches.sh'], cwd=source, check=True, timeout=60)
    if track == 'patch':
        for entry in lock['patches']:
            patch = checked_patch(operator, entry)
            target = source if entry['target'] == 'xllama' else source / 'llama.cpp'
            # Native git path checks remain enabled; no --unsafe-paths or shell interpolation.
            git(target, 'apply', '--check', str(patch))
            git(target, 'apply', str(patch))
    if inventory is not None:
        verify_shipping_postimages(source, inventory)
    subprocess.run(['bash', 'scripts/check-uwp-sources.sh'], cwd=source, check=True, timeout=60)
    # Do not permit the candidate to silently add model weights/signing keys.
    for path in source.rglob('*'):
        if path.is_file() and path.suffix.lower() in ('.gguf', '.safetensors', '.pfx', '.p12', '.pem', '.key'):
            raise ValueError('weights/private-key-like file in build tree: ' + str(path.relative_to(source)))
    receipt = {'track': track, 'xllama': XLLAMA, 'llama': LLAMA,
               'reviewed_patch_lock': lock_sha if track == 'patch' else None,
               'shipping_inventory_sha256': SHIPPING_INVENTORY_SHA if inventory is not None else None,
               'compiler_inventory_sha256': COMPILER_INVENTORY_SHA if inventory is not None else None,
               'selected_shipping_files_verified': len(inventory) if inventory is not None else 0,
               'root_diff_sha256': hashlib.sha256(git(source, 'diff', '--binary').encode()).hexdigest(),
               'llama_diff_sha256': hashlib.sha256(git(source / 'llama.cpp', 'diff', '--binary').encode()).hexdigest()}
    (source / 'UA50-SOURCE-RECEIPT.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(receipt))

if __name__ == '__main__':
    main(*sys.argv[1:])
