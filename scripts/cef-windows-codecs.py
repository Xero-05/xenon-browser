"""Apply and audit Xenon's pinned Windows-decoder CEF build.

Patches are tracked in this repository, never hand-edited in downloaded sources.
The audit is technical build provenance, not a patent licensing determination.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def run(*args, cwd=None):
    result = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f'{args[0]} failed ({result.returncode}): {result.stderr}')
    return result.stdout.strip()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def validate(source):
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text())
    recipe = lock['cef']['mediaSourceBuild']
    require(recipe['codecBackend'] == 'windows', 'Wrong codec backend')
    require(run('git', 'rev-parse', 'HEAD', cwd=source) == recipe['chromiumCommit'], 'Chromium revision mismatch')
    require(run('git', 'rev-parse', 'HEAD', cwd=source / 'cef') == recipe['cefCommit'], 'CEF revision mismatch')
    patch = ROOT / recipe['patch']['path']
    require(sha(patch) == recipe['patch']['sha256'], 'Codec patch hash mismatch')
    return recipe, patch


def apply(source):
    _, patch = validate(source)
    reverse = subprocess.run(['git', 'apply', '--reverse', '--check', str(patch)],
                             cwd=source, capture_output=True)
    if reverse.returncode == 0:
        print('Exact Windows codec patch already applied.')
        return
    run('git', 'apply', '--check', '--whitespace=error-all', str(patch), cwd=source)
    run('git', 'apply', '--whitespace=error-all', str(patch), cwd=source)
    print('Applied pinned Windows codec patch.')


def audit(source, distribution):
    recipe, patch = validate(source)
    run('git', 'apply', '--reverse', '--check', str(patch), cwd=source)
    output = source / 'out/Release_GN_x64'
    args = (output / 'args.gn').read_text()
    required = {
        'xenon_windows_codecs': 'true', 'proprietary_codecs': 'true',
        'ffmpeg_branding': '"Chromium"', 'media_use_openh264': 'false',
        'enable_ffmpeg_video_decoders': 'false',
    }
    for name, value in required.items():
        require(re.search(rf'(?m)^{name}\s*=\s*{re.escape(value)}\s*$', args), f'Unexpected GN argument: {name}')
    ffmpeg = source / 'third_party/ffmpeg/chromium/config/Chromium/win/x64'
    codecs = (ffmpeg / 'libavcodec/codec_list.c').read_text()
    require(not re.search(r'ff_\w*(aac|h264)\w*_(decoder|encoder)', codecs), 'Bundled FFmpeg H.264/AAC codec')
    components = (ffmpeg / 'config_components.h').read_text()
    for name in ['AAC_DECODER', 'AAC_FIXED_DECODER', 'H264_DECODER', 'AAC_ENCODER', 'LIBOPENH264_ENCODER']:
        require(re.search(rf'(?m)^#define CONFIG_{name} 0$', components), f'Bundled FFmpeg component: {name}')
    gn = source / 'buildtools/win/gn.exe'
    dependencies = run(str(gn), 'desc', str(output), '//cef:libcef', 'deps', '--all', cwd=source)
    require('openh264' not in dependencies.lower(), 'OpenH264 linked into libcef')
    require('symphonia_codec_aac' not in dependencies.lower(), 'Symphonia AAC linked into libcef')
    for name in ['libcef.dll', 'bootstrap.exe']:
        require(sha(distribution / 'Release' / name) == sha(output / name),
                f'Distribution does not match audited build output: {name}')
    artifacts = ['Release/libcef.dll', 'Release/bootstrap.exe', 'LICENSE.txt', 'CREDITS.html']
    report = {
        'codecBackend': 'windows', 'recipe': recipe,
        'argsSha256': sha(output / 'args.gn'),
        'ffmpegCodecListSha256': sha(ffmpeg / 'libavcodec/codec_list.c'),
        'ffmpegComponentsSha256': sha(ffmpeg / 'config_components.h'),
        'bundledH264AacDecoders': False, 'bundledOpenH264': False,
        'files': {name: sha(distribution / name) for name in artifacts},
        'limitations': 'Technical build audit only; not legal clearance. H.264 requires Windows GPU decoding. AAC uses Media Foundation. No bundled H.264/AAC software fallback.',
    }
    (distribution / 'xenon-codec-build.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['apply', 'audit'])
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--distribution', type=Path)
    args = parser.parse_args()
    if args.command == 'apply':
        apply(args.source)
    else:
        if not args.distribution:
            parser.error('audit requires --distribution')
        audit(args.source, args.distribution)
