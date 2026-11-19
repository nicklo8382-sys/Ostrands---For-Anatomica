import os
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
# The fork changes only the plugin, so releases ship data/SKSE and players get the
# meshes and textures from the original OStrands of the same upstream version.
SHIPPED_DATA = ('SKSE',)
REQUIRED_DATA = (
    'SKSE/Plugins/OStrands.ini',
)


def source_revision(root):
    try:
        commit = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=root, capture_output=True, text=True, check=True).stdout.strip()
        dirty = subprocess.run(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=root, capture_output=True, text=True,
                               check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None
    return f'{commit} (plus uncommitted changes)' if dirty else commit


def notice_text(root):
    text = (root / 'NOTICE.txt').read_text(encoding='utf-8-sig').rstrip('\n') + '\n'
    revision = source_revision(root)
    if revision:
        text += f'This build was made from commit {revision}.\n'
    return text.replace('\n', '\r\n').encode('utf-8')


def build_package(root=ROOT):
    root = Path(root)
    cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8-sig')
    match = re.search(r'project\(\s*OStrands\s+VERSION\s+(\d+(?:\.\d+){1,3})(?=\s|\))', cmake)
    if not match:
        raise ValueError('Cannot find the OStrands project version in CMakeLists.txt')

    dll = root / 'build/release/0_OStrands.dll'
    data = root / 'data'
    for path in (dll, root / 'LICENSE', root / 'NOTICE.txt', *(data / name for name in REQUIRED_DATA)):
        if not path.is_file():
            raise FileNotFoundError(f'Required package input is missing: {path}')

    files = [(dll, 'SKSE/Plugins/0_OStrands.dll'), (root / 'LICENSE', 'SKSE/Plugins/OStrands-LICENSE.txt')]
    generated = [('SKSE/Plugins/OStrands-NOTICE.txt', notice_text(root))]
    names = {name.casefold() for _, name in files} | {name.casefold() for name, _ in generated}
    for path in sorted(path for folder in SHIPPED_DATA for path in (data / folder).rglob('*')):
        if not path.is_file():
            continue
        name = path.relative_to(data).as_posix()
        if name.casefold() in names:
            raise ValueError(f'Duplicate package destination: {name}')
        names.add(name.casefold())
        files.append((path, name))

    dist = root / 'dist'
    dist.mkdir(exist_ok=True)
    out = dist / f'OStrands {match.group(1)}.zip'
    # Build beside the destination so replacement is atomic. Closing the temporary
    # file first also allows ZipFile to reopen it on Windows.
    with tempfile.NamedTemporaryFile(prefix='.ostrands-', suffix='.tmp', dir=dist, delete=False) as temp:
        temporary = Path(temp.name)
    try:
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as archive:
            for path, name in files:
                archive.write(path, name)
            for name, contents in generated:
                archive.writestr(name, contents)
        with zipfile.ZipFile(temporary) as archive:
            bad_file = archive.testzip()
            if bad_file is not None:
                raise ValueError(f'Archive verification failed: {bad_file}')
        os.replace(temporary, out)
    finally:
        temporary.unlink(missing_ok=True)
    return out


def main():
    out = build_package()
    with zipfile.ZipFile(out) as archive:
        for info in archive.infolist():
            print(f'  {info.filename:45s} {info.file_size:8d}')
    print(out.relative_to(ROOT))


if __name__ == '__main__':
    main()
