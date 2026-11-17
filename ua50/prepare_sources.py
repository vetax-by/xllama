"""Actions source preparation only. Never invoked locally against a live checkout."""
import hashlib
import json
import os
import zipfile
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

CURRENT_PATCH_LOCK = 'f035c9cd3b672ac2b441eecd65d171ef2175c33ea9c34045644d61c0d85957e0'
STOCK_GUARD_SHA = '1041743635f5d67e8c64591727b627095532c62d832646ca49468cadd9ff8ccb'
def admitted_runtime_lock(operator, lock_sha):
    if lock_sha != CURRENT_PATCH_LOCK:
        raise ValueError('only current configurable model2 diagnostics lock admitted; older locks refused')
    raw = (operator / 'PATCH-LOCK.json').read_bytes()
    if hashlib.sha256(raw).hexdigest() != CURRENT_PATCH_LOCK:
        raise ValueError('current patch lock bytes changed')
    lock = json.loads(raw)
    if lock['parent_patch_lock_sha256'] != '921fc2a5d9724fc28851146266fc67b7627e54a45c8a10775f30c52eb2a20ebe' or lock['parent17_reused_not_replayed'] is not True or lock['parent12_reused_not_replayed'] is not True:
        raise ValueError('exact accepted generation parent17 required')
    if [(e['file'], e['target']) for e in lock['patches']] != [
            ('patch/runtime-root.patch', 'xllama'), ('patch/runtime-llama.patch', 'llama'),
            ('patch/app-xllama.patch', 'xllama'), ('patch/app-llama.patch', 'llama'),
            ('patch/cpu-pilot-root.patch', 'xllama'), ('patch/cpu-pilot-llama.patch', 'llama'),
            ('patch/first-answer-root.patch', 'xllama'), ('patch/finished-answer-root.patch', 'xllama'),
            ('patch/cache-ab-root.patch', 'xllama'), ('patch/cache-ab-llama.patch', 'llama'),
            ('patch/configurable-runner-root.patch', 'xllama'), ('patch/configurable-runner-llama.patch', 'llama'),
            ('patch/ua68-expert-gpu-root.patch', 'xllama'), ('patch/ua68-expert-gpu-native.patch', 'llama'),
            ('patch/ua68-gpu-identity-root.patch', 'xllama'),
            ('patch/generation-root.patch', 'xllama'), ('patch/generation-native.patch', 'llama'),
            ('patch/followup-root.patch', 'xllama'), ('patch/followup-native.patch', 'llama'),
            ('patch/test-harness-trace-root.patch', 'xllama'),
            ('patch/model2-proof-logits-root.patch', 'xllama'),
            ('patch/model2-layer-witness-root.patch', 'xllama'),
            ('patch/model2-first-vector-witness-root.patch', 'xllama'),
            ('patch/model2-first-vector-witness-native.patch', 'llama'),
            ('patch/model2-configurable-diagnostics-root.patch', 'xllama'),
            ('patch/model2-configurable-diagnostics-native.patch', 'llama')]:
        raise ValueError('exact accepted parent twenty-four plus configurable diagnostics patches required')
    if lock.get('parent20_patch_lock_sha256') != '62437bdcd1a46c3460cef01b7b7d7d63fae5723df6e326d8baad57ada71fd1d7' or lock.get('parent20_reused_not_replayed') is not True:
        raise ValueError('exact accepted parent20 required')
    if lock.get('parent21_patch_lock_sha256') != 'fa3ce18df56361f2b78ce8eda57fdb8c87ff0d21792693b4be928415b33c0027' or lock.get('parent21_reused_not_replayed') is not True:
        raise ValueError('exact accepted parent21 required')
    if lock.get('parent22_patch_lock_sha256') != 'b427a60d9a17df6e1c2a7b60c8c4e1ee5e392a47e7ceb2105eea208b2241ee48' or lock.get('parent22_reused_not_replayed') is not True:
        raise ValueError('exact accepted parent22 required')
    if lock.get('parent24_patch_lock_sha256') != '9b9659dc28c63383aa89c10eafc9a8c625ee6d5d659030941c808f4303d9c372' or lock.get('parent24_reused_not_replayed') is not True:
        raise ValueError('exact accepted parent24 required')
    if lock.get('parent14_reused_not_replayed') is not True:
        raise ValueError('exact accepted parent14 required')
    for entry in lock['patches']:
        patch = checked_patch(operator, entry)
        if patch.stat().st_size != entry['bytes']:
            raise ValueError('patch byte count mismatch')
    return lock

SHIPPING_INVENTORY_SHA = 'aad42fc7684979c88119724f1bfced8cb1eabcdd6c21d4a0d8b8b0f13997645c'
COMPILER_INVENTORY_SHA = 'bf30c8cddcfa4ac0e773499000e03b964299b9332650858147f5a8afebd0e815'

def admitted_shipping_inventory(operator):
    raw = (operator / 'SHIPPING-FILE-INVENTORY.json').read_bytes()
    if hashlib.sha256(raw).hexdigest() != SHIPPING_INVENTORY_SHA:
        raise ValueError('shipping inventory changed')
    imports = (operator / 'COMPILER-IMPORT-INVENTORY.json').read_bytes()
    if hashlib.sha256(imports).hexdigest() != COMPILER_INVENTORY_SHA:
        raise ValueError('compiler/import declarations changed')
    includes = (operator / 'INCLUDE-DEPENDENCY-INVENTORY.json').read_bytes()
    if hashlib.sha256(includes).hexdigest() != '59e6d4793f7b4163dada2279f86144ae5c4411eb5118d38cff3f00fbdda1e14d' or len(json.loads(includes)['direct_includes_only']) != 718:
        raise ValueError('exact current 718 direct source includes required')
    entries = json.loads(raw)['selected_source_files']
    if len(entries) != 84:
        raise ValueError('exact eighty-four selected shipping files required')
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

# The windows-2022 image documents this Git-for-Windows installation.
# Never search PATH for bash on Windows: System32/bash.exe may be the WSL stub.
WINDOWS_GIT_ROOT = Path('C:/Program Files/Git')

def script_bash():
    if sys.platform != 'win32':
        return 'bash'  # Preserve the existing non-Windows invocation.
    root = WINDOWS_GIT_ROOT
    bash = root / 'bin' / 'bash.exe'
    git_exe = root / 'cmd' / 'git.exe'
    for path in (bash, git_exe):
        if not path.is_absolute():
            raise ValueError('absolute Git-for-Windows installation required')
        # Refuse missing files, symlinks and Windows junction/reparse components.
        for component in (*reversed(path.parents), path):
            info = component.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
                raise ValueError('Git-for-Windows symlink/reparse path refused')
        if not stat.S_ISREG(path.lstat().st_mode):
            raise ValueError('regular Git-for-Windows executable required')
    git_version = subprocess.run([str(git_exe), '--version'], check=True,
                                capture_output=True, text=True, timeout=10).stdout.strip()
    bash_version = subprocess.run([str(bash), '--version'], check=True,
                                 capture_output=True, text=True, timeout=10).stdout.strip()
    if not re.fullmatch(r'git version [0-9][^\r\n]*\.windows\.[0-9]+', git_version):
        raise ValueError('Git-for-Windows version required')
    if not bash_version.startswith('GNU bash, version '):
        raise ValueError('GNU Bash version required at explicit Git-for-Windows path')
    print(json.dumps({'script_bash': str(bash), 'git_version': git_version,
                      'bash_version': bash_version.splitlines()[0]}))
    return str(bash)

# Exact source-only fixtures tracked at the pinned llama commit.
# One classified Keynote document is not an arbitrary private-key exemption.
UPSTREAM_SOURCE_FIXTURES = {'docs/development/llama-star/idea-arch.key': {'bytes': 488591,
                                               'git_blob': '3e068e7075c2ebb53a270b57c51e9281618d8f29',
                                               'kind': 'keynote_zip',
                                               'mode': '100755',
                                               'sha256': '32dede0548e6501b4b60d89b4afc3445a9a45c5df6fcee0c2bb9fe75592908c9'},
 'models/ggml-vocab-aquila.gguf': {'bytes': 4825676,
                                   'git_blob': '7a9abb122ddd18706100e155535f29120ca7ebc4',
                                   'kind': 'tensor_free_vocab',
                                   'mode': '100644',
                                   'sha256': '7c53c3c516ac67c7ca12977b9690fdea3d2ef13bbaed6378f98191a13ef5ca00'},
 'models/ggml-vocab-baichuan.gguf': {'bytes': 1340998,
                                     'git_blob': '7caaf8239b052b1fc2f2b74441fe377a73758bd1',
                                     'kind': 'tensor_free_vocab',
                                     'mode': '100644',
                                     'sha256': '4f5b955697f3bd3108070b1d5936c7eb9fc542b81c6932e59abddec75bca1963'},
 'models/ggml-vocab-bert-bge.gguf': {'bytes': 627549,
                                     'git_blob': 'b2cbd5df6882d8e581368d494bb86aa449f212a7',
                                     'kind': 'tensor_free_vocab',
                                     'mode': '100644',
                                     'sha256': 'fbcbe22278fb302694d5f4a41bfe48c5f90e8e3554eab1c0435387dff654a854'},
 'models/ggml-vocab-command-r.gguf': {'bytes': 10874545,
                                      'git_blob': 'b553eab330591687351f1bb717eb56a9ee762c2f',
                                      'kind': 'tensor_free_vocab',
                                      'mode': '100644',
                                      'sha256': 'a2f8cfea952ef7c391a6d92a1c309d0bd32e36384d9b9230569a7425732f27d9'},
 'models/ggml-vocab-deepseek-coder.gguf': {'bytes': 1156067,
                                           'git_blob': '6728cd747249ecaef7a2f63f103f68261d713c3d',
                                           'kind': 'tensor_free_vocab',
                                           'mode': '100644',
                                           'sha256': '91cb1379f2e33af1c4866b194622b7a0e12e8f0c9dba7ba2f10d55978730bec1'},
 'models/ggml-vocab-deepseek-llm.gguf': {'bytes': 3970167,
                                         'git_blob': '5d66091c44b6f581b5d38c05f14f29c3e2b75b5d',
                                         'kind': 'tensor_free_vocab',
                                         'mode': '100644',
                                         'sha256': '867f77537b54565f0d81d508c04edc41aa1d4ffc1a92745f225b4c1b02755f76'},
 'models/ggml-vocab-falcon.gguf': {'bytes': 2287728,
                                   'git_blob': '334d50da51ba55657509709aa51b52baeda8093c',
                                   'kind': 'tensor_free_vocab',
                                   'mode': '100644',
                                   'sha256': '9f0bf8b0733680398b72e652e90f260f43782f326e75545fc0e49611a5ba35ad'},
 'models/ggml-vocab-gemma-4.gguf': {'bytes': 15776467,
                                    'git_blob': '03c4954118ecc37b0503b50e0965f087fdb99e6d',
                                    'kind': 'tensor_free_vocab',
                                    'mode': '100644',
                                    'sha256': '58b1ba0b57f3b4d7c468ba4ffd91ad85190346a3d7ad7e71d1cabaae8a14bb65'},
 'models/ggml-vocab-gpt-2.gguf': {'bytes': 1766807,
                                  'git_blob': '5ea85cf52e7de68fed9e4eebe5996ea3a6753994',
                                  'kind': 'tensor_free_vocab',
                                  'mode': '100644',
                                  'sha256': 'cedc56ca6e2e89f63e781696d1fd76b4b1d49e6720dee86463e915f6e90016ac'},
 'models/ggml-vocab-gpt-neox.gguf': {'bytes': 1771431,
                                     'git_blob': 'b9af16845ccb4d4888c15f1293b1f47291928412',
                                     'kind': 'tensor_free_vocab',
                                     'mode': '100644',
                                     'sha256': 'ae593a7f9b8bb174ed4f5019e41530463e4dac7aa06e42dee8aa650d2bdac53d'},
 'models/ggml-vocab-llama-bpe.gguf': {'bytes': 7818140,
                                      'git_blob': 'e51a99118bc43bc2373adf162f081751013b361a',
                                      'kind': 'tensor_free_vocab',
                                      'mode': '100644',
                                      'sha256': '97272e430d53bc7688f52d5e0ad8ea8f163ede9f1bbd1694feaa504797d5d96e'},
 'models/ggml-vocab-llama-spm.gguf': {'bytes': 723869,
                                      'git_blob': '658295a5df7412d786103d2a5332ee6e7de9a5df',
                                      'kind': 'tensor_free_vocab',
                                      'mode': '100644',
                                      'sha256': '16c3724582d59aa8bf84711894e833f916ee46a31d80e21312759c48bf8d0e69'},
 'models/ggml-vocab-mpt.gguf': {'bytes': 1771393,
                                'git_blob': 'f42f56dec929440941fac087eb4a15c262743b3a',
                                'kind': 'tensor_free_vocab',
                                'mode': '100644',
                                'sha256': '59dc382612866d1fc6c11ea531318d327598f3412d9c8f8600607cdf3030898f'},
 'models/ggml-vocab-nomic-bert-moe.gguf': {'bytes': 6821877,
                                           'git_blob': 'b6f4d9441113bf08fd3168744048800cf232c7f9',
                                           'kind': 'tensor_free_vocab',
                                           'mode': '100644',
                                           'sha256': '90a6746926454784a98389ad36a36d89bc9cfc81db9cb0f33c941bcc959fe5f9'},
 'models/ggml-vocab-phi-3.gguf': {'bytes': 726019,
                                  'git_blob': '745be416a798a1e7a2effaa935bc903edaaf303d',
                                  'kind': 'tensor_free_vocab',
                                  'mode': '100644',
                                  'sha256': '967d7190d11c4842eab697079d98d56c2116e10eb617be355a2733bfc132e326'},
 'models/ggml-vocab-qwen2.gguf': {'bytes': 5928681,
                                  'git_blob': '541e475bc94532defa0bb38dfabf6dfaae1fe8ae',
                                  'kind': 'tensor_free_vocab',
                                  'mode': '100644',
                                  'sha256': '44c2f46b715f585c6ab513970e8a006bfa5badd6108560054921cf598d154d8c'},
 'models/ggml-vocab-qwen35.gguf': {'bytes': 5928682,
                                   'git_blob': 'c06d1c17e674f651e9a8265957de2dfed441f208',
                                   'kind': 'tensor_free_vocab',
                                   'mode': '100644',
                                   'sha256': '63ed952ff338996cf0bdf24a7b10015124273f75c6dc9bb427356aa3f67ec62c'},
 'models/ggml-vocab-refact.gguf': {'bytes': 1720710,
                                   'git_blob': '52afcf01aeb7323e0153b2817a1cbfc5a07d787b',
                                   'kind': 'tensor_free_vocab',
                                   'mode': '100644',
                                   'sha256': 'ac3ceda902fed91ccf74312b305d9b86c37e4f8e35fa9cc6ef3ce34fca7d4678'},
 'models/ggml-vocab-starcoder.gguf': {'bytes': 1719346,
                                      'git_blob': '7a7e7742ab1fcf0a65b9ef5d3115ce735ae0de90',
                                      'kind': 'tensor_free_vocab',
                                      'mode': '100644',
                                      'sha256': 'fedb892b4e1bd3c1f2fcdae356440b14fb458f4264d586e5c987ed93df4e174d'}}

def fixture_handle_metadata(stream):
    """One handle-based source for BOTH path probes and the retained read handle."""
    if sys.platform != 'win32':
        value = os.fstat(stream.fileno())
        if not stat.S_ISREG(value.st_mode):
            raise ValueError('regular held fixture required')
        return {'api': 'os.fstat', 'device': value.st_dev, 'inode': value.st_ino,
                'bytes': value.st_size, 'mtime_ns': value.st_mtime_ns,
                'ctime_ns': value.st_ctime_ns}
    # Do not compare CPython path stat timestamps with descriptor timestamps.
    # Use the SAME native raw fields for every snapshot, without conversion.
    import ctypes
    import msvcrt
    class Basic(ctypes.Structure):
        _fields_ = [('creation', ctypes.c_int64), ('access', ctypes.c_int64),
                    ('write', ctypes.c_int64), ('change', ctypes.c_int64),
                    ('attributes', ctypes.c_uint32)]
    class Standard(ctypes.Structure):
        _fields_ = [('allocation', ctypes.c_int64), ('size', ctypes.c_int64),
                    ('links', ctypes.c_uint32), ('delete_pending', ctypes.c_ubyte),
                    ('directory', ctypes.c_ubyte)]
    class Identity(ctypes.Structure):
        _fields_ = [('volume', ctypes.c_uint64), ('file_id', ctypes.c_ubyte * 16)]
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    query = kernel.GetFileInformationByHandleEx
    query.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32]
    query.restype = ctypes.c_int
    handle = ctypes.c_void_p(msvcrt.get_osfhandle(stream.fileno()))
    basic, standard, identity = Basic(), Standard(), Identity()
    for information_class, value in ((0, basic), (1, standard), (18, identity)):
        if not query(handle, information_class, ctypes.byref(value), ctypes.sizeof(value)):
            raise ctypes.WinError(ctypes.get_last_error())
    if basic.attributes & (0x10 | 0x400) or standard.directory or standard.delete_pending:
        raise ValueError('native fixture directory/reparse/delete-pending refused')
    return {'api': 'GetFileInformationByHandleEx', 'volume': identity.volume,
            'file_id': bytes(identity.file_id).hex(), 'bytes': standard.size,
            'creation_100ns': basic.creation, 'write_100ns': basic.write,
            'change_100ns': basic.change, 'attributes': basic.attributes,
            'links': standard.links}

def fixture_path_metadata(path):
    # lstat is retained ONLY for type/reparse refusal, not timestamp equality.
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
        raise ValueError('regular non-reparse fixture path required')
    with path.open('rb') as probe:
        return fixture_handle_metadata(probe)

def fixture_metadata_equal(relative, phase, snapshots):
    values = list(snapshots.values())
    if not values or any(value != values[0] for value in values[1:]):
        # Preserve ALL exact native values and phase; no rounding or relabeling.
        raise ValueError(json.dumps({'error': 'fixture metadata mismatch',
                                    'path': relative, 'phase': phase,
                                    'snapshots': snapshots}, sort_keys=True))

def record_fixture_metadata(relative, phase, snapshots, query):
    try:
        snapshots[phase] = query()
    except Exception as error:
        raise ValueError(json.dumps({'error': 'fixture metadata query failed',
                                    'path': relative, 'phase': phase,
                                    'snapshots': snapshots, 'exception': type(error).__name__,
                                    'detail': str(error)}, sort_keys=True)) from error

def admitted_source_fixture(source, path):
    relative = path.relative_to(source).as_posix()
    prefix = 'llama.cpp/'
    key = relative[len(prefix):] if relative.startswith(prefix) else ''
    expected = UPSTREAM_SOURCE_FIXTURES.get(key)
    if expected is None:
        raise ValueError('unapproved weight/key-like source path: ' + relative)
    llama = source / 'llama.cpp'
    # Each admission rebinds HEAD, commit tree AND tracked index, not a mutable name.
    if git(llama, 'rev-parse', 'HEAD') != LLAMA:
        raise ValueError('fixture source commit changed')
    blob = expected['git_blob']
    if git(llama, 'ls-tree', '-z', LLAMA, '--', key) != expected['mode'] + ' blob ' + blob + '\t' + key + '\0':
        raise ValueError('fixture pinned tree identity changed')
    if git(llama, 'ls-files', '--stage', '-z', '--', key) != expected['mode'] + ' ' + blob + ' 0\t' + key + '\0':
        raise ValueError('fixture tracked index identity changed')
    for component in (source, *(source.joinpath(*path.relative_to(source).parts[:i])
                      for i in range(1, len(path.relative_to(source).parts) + 1))):
        info = component.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('fixture symlink/reparse path refused')
    snapshots = {}
    record_fixture_metadata(relative, 'path_before', snapshots, lambda: fixture_path_metadata(path))
    if snapshots['path_before']['bytes'] != expected['bytes']:
        raise ValueError('fixture type/size changed')
    sha = hashlib.sha256()
    git_sha = hashlib.sha1(b'blob ' + str(expected['bytes']).encode() + b'\0')
    with path.open('rb') as stream:
        record_fixture_metadata(relative, 'held_before', snapshots, lambda: fixture_handle_metadata(stream))
        fixture_metadata_equal(relative, 'before_read', snapshots)
        header = stream.read(24)
        if expected['kind'] == 'tensor_free_vocab':
            if len(header) != 24 or header[:4] != b'GGUF' or int.from_bytes(header[4:8], 'little') not in (2, 3) or int.from_bytes(header[8:16], 'little') != 0:
                raise ValueError('upstream vocab must contain zero model tensors')
        elif expected['kind'] == 'keynote_zip':
            if not header.startswith(b'PK\x03\x04'):
                raise ValueError('exact upstream presentation ZIP header required')
        else:
            raise ValueError('unknown source fixture classification')
        sha.update(header); git_sha.update(header)
        count = len(header)
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk: break
            count += len(chunk)
            if count > expected['bytes']: raise ValueError('fixture byte count changed')
            sha.update(chunk); git_sha.update(chunk)
        if expected['kind'] == 'keynote_zip':
            stream.seek(0)
            with zipfile.ZipFile(stream) as archive:
                names = archive.namelist()
                if len(names) != 57 or not {'Index/Document.iwa', 'Metadata/BuildVersionHistory.plist', 'Metadata/Properties.plist'} <= set(names):
                    raise ValueError('exact upstream presentation structure required')
        record_fixture_metadata(relative, 'held_after', snapshots, lambda: fixture_handle_metadata(stream))
        fixture_metadata_equal(relative, 'after_read', snapshots)
        record_fixture_metadata(relative, 'path_after', snapshots, lambda: fixture_path_metadata(path))
        fixture_metadata_equal(relative, 'path_after_read', snapshots)
    if count != expected['bytes'] or git_sha.hexdigest() != blob or sha.hexdigest() != expected['sha256']:
        raise ValueError('fixture content identity changed')
    return {'path': relative, 'source_commit': LLAMA, 'git_blob': blob,
            'bytes': count, 'sha256': sha.hexdigest(), 'kind': expected['kind'],
            'tensor_count': 0 if expected['kind'] == 'tensor_free_vocab' else None,
            'metadata_api': snapshots['held_before']['api'], 'metadata_snapshots': snapshots}

def scan_source_payloads(source):
    admitted = []
    for path in source.rglob('*'):
        suffix = path.suffix.lower()
        if suffix not in ('.gguf', '.safetensors', '.pfx', '.p12', '.pem', '.key'):
            continue
        # Unknown files of EVERY forbidden suffix refuse. Only exact pinned
        # source objects can pass; final package forbids all these extensions.
        admitted.append(admitted_source_fixture(source, path))
    return admitted

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
    # Validate the tool before submodule/network or any source mutation.
    bash = script_bash()
    if git(source, 'rev-parse', 'HEAD') != XLLAMA:
        raise ValueError('xllama identity')
    if git(source, 'status', '--porcelain'):
        raise ValueError('dirty source')
    git(source, 'submodule', 'update', '--init', '--depth', '1', '--', 'llama.cpp')
    if git(source / 'llama.cpp', 'rev-parse', 'HEAD') != LLAMA:
        raise ValueError('llama identity')
    subprocess.run([bash, 'scripts/apply-uwp-patches.sh'], cwd=source, check=True, timeout=60)
    if track == 'patch':
        for entry in lock['patches']:
            patch = checked_patch(operator, entry)
            target = source if entry['target'] == 'xllama' else source / 'llama.cpp'
            # Native git path checks remain enabled; no --unsafe-paths or shell interpolation.
            git(target, 'apply', '--check', str(patch))
            git(target, 'apply', str(patch))
    if inventory is not None:
        verify_shipping_postimages(source, inventory)
    subprocess.run([bash, 'scripts/check-uwp-sources.sh'], cwd=source, check=True, timeout=60)
    # Admit only exact tracked tensor-free vocab and classified presentation fixtures.
    source_fixtures = scan_source_payloads(source)
    receipt = {'track': track, 'xllama': XLLAMA, 'llama': LLAMA,
               'upstream_source_fixtures': source_fixtures,
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
