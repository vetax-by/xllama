"""Compose bounded source ONLY after the accepted 33-patch clean-tree recipe."""
import hashlib
import json
import pathlib
import re
import stat
import subprocess
import sys

LOCK_SHA = 'e05a3f520f2948cad62feb363652144071c18ec0cf94bfe90b406a9153639d20'
ROOT = pathlib.Path(__file__).resolve().parent


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def checked_file(root, relative):
    parts = relative.split('/')
    if not parts or any(not re.fullmatch(r'[A-Za-z0-9_.-]+', x) or x in ('.', '..') for x in parts):
        raise ValueError('noncanonical source path')
    current = root
    for part in parts:
        current = current / part
        info = current.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('symlink/reparse source')
    if not stat.S_ISREG(current.stat().st_mode):
        raise ValueError('regular source required')
    return current


def inventory():
    raw = (ROOT / 'SOURCE-LOCK.json').read_bytes()
    if sha(raw) != LOCK_SHA:
        raise ValueError('bounded source lock')
    lock = json.loads(raw)
    seen = set()
    for row in lock['postimages']:
        if row['path'] in seen:
            raise ValueError('duplicate source path')
        seen.add(row['path'])
        b = checked_file(ROOT / 'postimages', row['path']).read_bytes()
        if len(b) != row['bytes'] or sha(b) != row['sha256']:
            raise ValueError('changed postimage: ' + row['path'])
    actual = {str(p.relative_to(ROOT / 'postimages')).replace('\\', '/') for p in (ROOT / 'postimages').rglob('*') if p.is_file()}
    if actual != seen or len(seen) != 136:
        raise ValueError('postimage inventory')
    if sha(checked_file(ROOT, 'ENGINE-CONTRACT.json').read_bytes()) != lock['engine_sha256']:
        raise ValueError('engine association')
    packer = checked_file(ROOT, 'PACKER-SOURCES.json').read_bytes()
    if sha(packer) != lock['packer_source_sha256']:
        raise ValueError('packer association')
    return lock


def verify_composed(source, lock):
    packer = checked_file(ROOT, 'PACKER-SOURCES.json').read_bytes()
    if sha(packer) != lock['packer_source_sha256']:
        raise ValueError('packer association')
    for row in lock['postimages'] + json.loads(packer)['files']:
        b = checked_file(source, row['path']).read_bytes()
        if len(b) != row['bytes'] or sha(b) != row['sha256']:
            raise ValueError('composed source mismatch: ' + row['path'])


def main(source, selected_lock):
    if selected_lock != LOCK_SHA:
        raise ValueError('unselected source lock')
    lock = inventory()
    source = pathlib.Path(source).resolve()
    # The original recipe refuses a dirty/wrong-base product, applies the UWP
    # guard and historical patches once, and checks all 102 baseline postimages.
    subprocess.run([sys.executable, str(ROOT.parent / 'prepare_sources.py'), str(source), str(ROOT.parent), 'patch', lock['base_patch_lock_sha256']], check=True)
    receipt_path = source / 'UA50-SOURCE-RECEIPT.json'
    receipt = json.loads(receipt_path.read_bytes())
    if receipt['reviewed_patch_lock'] != lock['base_patch_lock_sha256'] or receipt['selected_shipping_files_verified'] != 102:
        raise ValueError('baseline composition receipt')
    # Explicit exact postimage replacement; never reapply root patches to the
    # already-patched application checkout on the public fork.
    for row in lock['postimages']:
        relative = row['path']
        target = source.joinpath(*relative.split('/'))
        # Check every existing destination ancestor before writing new leaves.
        current = source
        for part in relative.split('/')[:-1]:
            current /= part
            current.mkdir(exist_ok=True)
            info = current.lstat()
            if not stat.S_ISDIR(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
                raise ValueError('destination reparse')
        try:
            info = target.lstat()
        except FileNotFoundError:
            pass
        else:
            if not stat.S_ISREG(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
                raise ValueError('destination type/reparse')
        target.write_bytes(checked_file(ROOT / 'postimages', relative).read_bytes())
    verify_composed(source, lock)
    receipt.update(bounded_source_lock=LOCK_SHA,
                   native_manifest_sha256=lock['native_manifest_sha256'],
                   engine_sha256=lock['engine_sha256'],
                   packer_source_sha256=lock['packer_source_sha256'],
                   bounded_postimages_verified=len(lock['postimages']),
                   source_only_not_native_qualification=True)
    receipt_path.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'bounded_source_lock': LOCK_SHA, 'verified_postimages': len(lock['postimages'])}))


if __name__ == '__main__':
    main(*sys.argv[1:])
