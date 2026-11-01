#!/usr/bin/env python3
"""Build one Xedge resource image matching the firmware configuration."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import zipfile


def build(source, output, psa, opcua, ota, minify=False):
    """Stage resources privately; return the ZIP and generate guarded C output."""
    source, output = Path(source).resolve(), Path(output).resolve()
    stage = output.parent / (output.stem + '-stage')
    resources = (source / 'main/BAS-Resources').resolve()
    work = stage / 'BAS-Resources/build'
    tools = stage / 'BAS-Resources/tools/linux'
    work.mkdir(parents=True, exist_ok=True)
    tools.mkdir(parents=True, exist_ok=True)
    for dest, target in [(stage / 'BAS-Resources/src', resources / 'src')] + [
            (stage / name, source / 'main' / name) for name in ('LPeg', 'CBOR', 'lua-protobuf')]:
        if not dest.exists():
            dest.symlink_to(target)
    shutil.copy2(resources / 'build/Xedge.sh', work / 'Xedge.sh')
    shutil.copy2(resources / 'tools/linux/bin2c', tools / 'bin2c')
    env = dict(os.environ, SHARKSSL_PSA_CONSUMER=str(int(psa)),
               BA_ENABLE_LUA_OPCUA=str(int(opcua)), NO_BIN2C='1')
    subprocess.run(['bash', 'Xedge.sh'], cwd=work, env=env, input='n\n', text=True, check=True)
    if minify:
        for name in ('package.json', 'package-lock.json', 'gulpfile.js'):
            shutil.copy2(resources / 'build' / name, work / name)
        shutil.copytree(resources / 'build/plugins', work / 'plugins', dirs_exist_ok=True)
        subprocess.run(['npm', 'ci', '--ignore-scripts', '--no-audit', '--no-fund'], cwd=work, check=True)
        subprocess.run(['npm', 'run', 'minify-xedge'], cwd=work, check=True)
    tree = work / 'XedgeBuild'
    overlay = {p.relative_to(source).as_posix(): p for p in (source / '.lua').rglob('*') if p.is_file()}
    if not ota:
        overlay = {n: p for n, p in overlay.items() if not n.startswith('.lua/XedgePlugins/fw')}
    archive = output.with_suffix('.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(tree.rglob('*')):
            if p.is_file():
                name = p.relative_to(tree).as_posix()
                if name not in overlay and not (ota and name == '.lua/XedgePlugins/appupdate.lua'):
                    z.write(p, name)
        for name, p in sorted(overlay.items()):
            z.write(p, name)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert '.certificate/cacert.shark' not in z.namelist()
        assert any(n.startswith('.lua/opcua/') for n in z.namelist()) == opcua
        assert ('.lua/XedgePlugins/fwupdate.lua' in z.namelist()) == ota
        tpm = resources / ('src/mako/.lua/psatpm.lua' if psa else 'src/xedge/.lua/tpm.lua')
        assert z.read('.lua/tpm.lua') == tpm.read_bytes()
    subprocess.run([str(tools / 'bin2c'), '-z', 'getLspZipReader', str(archive), str(output)], check=True)
    # Reject an externally supplied resource image built for another backend.
    guard = ('#include "sdkconfig.h"\n'
             f'#if defined(CONFIG_XEDGE_PSA) != {int(psa)} || defined(CONFIG_OPCUA_ENABLED) != {int(opcua)}\n'
             '#error Xedge resources do not match menuconfig; rebuild the resource image\n'
             '#endif\n')
    output.write_bytes(guard.encode() + output.read_bytes())
    return archive


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', required=True)
    p.add_argument('--output', required=True)
    p.add_argument('--psa', type=int, choices=(0, 1), default=0)
    p.add_argument('--opcua', type=int, choices=(0, 1), default=0)
    p.add_argument('--ota', type=int, choices=(0, 1), default=1)
    p.add_argument('--minify', action='store_true')
    a = p.parse_args()
    build(a.source, a.output, bool(a.psa), bool(a.opcua), bool(a.ota), a.minify)
