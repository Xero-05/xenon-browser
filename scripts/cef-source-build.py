"""Apply and audit Xenon's pinned CEF source builds.

Two recipes in dependencies.lock.json share this procedure. `media`
(cef.mediaSourceBuild) adds Windows H.264/AAC decoding. `engine`
(cef.engineSourceBuild) keeps proprietary codecs disabled and carries only
Xenon's engine-behavior patches. Patches are tracked in this repository, never
hand-edited in downloaded sources. The audits are technical build provenance,
not a patent licensing determination.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
RECIPES = {
    'media': {'key': 'mediaSourceBuild', 'manifest': 'xenon-codec-build.json',
              'patches': ('patch', 'videoSchedulingPatch', 'backgroundFrameRatePatch'),
              'limitations': 'Technical build audit only; not legal clearance. H.264 requires Windows GPU decoding. AAC uses Media Foundation. No bundled H.264/AAC software fallback.'},
    'engine': {'key': 'engineSourceBuild', 'manifest': 'xenon-engine-build.json',
               'patches': ('videoSchedulingPatch', 'backgroundFrameRatePatch'),
               'limitations': 'Technical build audit only; not legal clearance. Proprietary codecs are disabled, so H.264/AAC media do not play, as with the stock CEF distribution. Only the pinned engine-behavior patches are applied.'},
}
# Effective GN values the engine build must have, whether set or defaulted.
ENGINE_ARGS = {'proprietary_codecs': 'false', 'ffmpeg_branding': '"Chromium"'}


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


def check_recipe(name, recipe):
    """Fail closed on lock edits that change what a recipe may contain."""
    patches = {key for key, value in recipe.items() if isinstance(value, dict)}
    require(patches == set(RECIPES[name]['patches']), f'Unexpected patch set for the {name} recipe')
    if name == 'media':
        require(recipe['codecBackend'] == 'windows', 'Wrong codec backend')
        return
    require(recipe['codecBackend'] == 'none', 'Wrong codec backend')
    pairs = [item.split('=', 1) for item in recipe['gnDefines'].split()]
    require(all(len(pair) == 2 for pair in pairs), 'Malformed GN defines')
    defines = dict(pairs)
    require(len(defines) == len(pairs), 'Duplicate GN define')
    require(defines.get('proprietary_codecs') == 'false', 'The engine recipe must disable proprietary codecs')
    require(defines.get('ffmpeg_branding') == 'Chromium', 'The engine recipe must use Chromium FFmpeg branding')
    require('xenon_windows_codecs' not in defines, 'The engine recipe must not enable Windows codecs')


def validate(source, name):
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text())
    recipe = lock['cef'][RECIPES[name]['key']]
    check_recipe(name, recipe)
    require(run('git', 'rev-parse', 'HEAD', cwd=source) == recipe['chromiumCommit'], 'Chromium revision mismatch')
    require(run('git', 'rev-parse', 'HEAD', cwd=source / 'cef') == recipe['cefCommit'], 'CEF revision mismatch')
    if name == 'engine':
        # The codec patch declares this argument. A checkout that carries it is
        # not an engine source tree; build the engine in its own directory.
        require('xenon_windows_codecs' not in (source / 'media/media_options.gni').read_text(),
                'Codec patch present in the engine source tree')
    patches = []
    for key in RECIPES[name]['patches']:
        patch = ROOT / recipe[key]['path']
        require(sha(patch) == recipe[key]['sha256'], f'Patch hash mismatch: {key}')
        patches.append(patch)
    return recipe, patches


def apply(source, name):
    _, patches = validate(source, name)
    pending = []
    for patch in patches:
        reverse = subprocess.run(['git', 'apply', '--reverse', '--check', str(patch)],
                                 cwd=source, capture_output=True)
        if reverse.returncode != 0:
            pending.append(str(patch))
    if pending:
        # Check the complete pending set before changing any source files.
        run('git', 'apply', '--check', '--whitespace=error-all', *pending, cwd=source)
        run('git', 'apply', '--whitespace=error-all', *pending, cwd=source)
    print(f'All {len(patches)} pinned {RECIPES[name]["key"]} patches applied.')


def audit(source, distribution, name):
    recipe, patches = validate(source, name)
    for patch in patches:
        run('git', 'apply', '--reverse', '--check', str(patch), cwd=source)
    for other in RECIPES.values():
        if other['manifest'] != RECIPES[name]['manifest']:
            require(not (distribution / other['manifest']).exists(), f'Distribution carries {other["manifest"]}')
    output = source / 'out/Release_GN_x64'
    args = (output / 'args.gn').read_text()
    gn = source / 'buildtools/win/gn.exe'
    effective = {}
    if name == 'media':
        required = {
            'xenon_windows_codecs': 'true', 'proprietary_codecs': 'true',
            'ffmpeg_branding': '"Chromium"', 'media_use_openh264': 'false',
            'enable_ffmpeg_video_decoders': 'false',
        }
        for arg, value in required.items():
            require(re.search(rf'(?m)^{arg}\s*=\s*{re.escape(value)}\s*$', args), f'Unexpected GN argument: {arg}')
    else:
        require('xenon_windows_codecs' not in args, 'Unexpected GN argument: xenon_windows_codecs')
        # GN reports set and defaulted values alike, one `name = value` per line.
        listing = run(str(gn), 'args', str(output), '--list', '--short', cwd=source)
        for arg, value in ENGINE_ARGS.items():
            match = re.search(rf'(?m)^{arg} = (.+?)\s*$', listing)
            require(match and match.group(1) == value, f'Unexpected GN argument: {arg}')
            effective[arg] = match.group(1)
    ffmpeg = source / 'third_party/ffmpeg/chromium/config/Chromium/win/x64'
    codecs = (ffmpeg / 'libavcodec/codec_list.c').read_text()
    require(not re.search(r'ff_\w*(aac|h264)\w*_(decoder|encoder)', codecs), 'Bundled FFmpeg H.264/AAC codec')
    components = (ffmpeg / 'config_components.h').read_text()
    for component in ['AAC_DECODER', 'AAC_FIXED_DECODER', 'H264_DECODER', 'AAC_ENCODER', 'LIBOPENH264_ENCODER']:
        require(re.search(rf'(?m)^#define CONFIG_{component} 0$', components), f'Bundled FFmpeg component: {component}')
    dependencies = run(str(gn), 'desc', str(output), '//cef:libcef', 'deps', '--all', cwd=source)
    require('openh264' not in dependencies.lower(), 'OpenH264 linked into libcef')
    require('symphonia_codec_aac' not in dependencies.lower(), 'Symphonia AAC linked into libcef')
    for binary in ['libcef.dll', 'bootstrap.exe']:
        require(sha(distribution / 'Release' / binary) == sha(output / binary),
                f'Distribution does not match audited build output: {binary}')
    artifacts = ['Release/libcef.dll', 'Release/bootstrap.exe', 'LICENSE.txt', 'CREDITS.html']
    report = {
        'codecBackend': recipe['codecBackend'], 'recipe': recipe,
        'argsSha256': sha(output / 'args.gn'),
        'ffmpegCodecListSha256': sha(ffmpeg / 'libavcodec/codec_list.c'),
        'ffmpegComponentsSha256': sha(ffmpeg / 'config_components.h'),
        'bundledH264AacDecoders': False, 'bundledOpenH264': False,
        'files': {artifact: sha(distribution / artifact) for artifact in artifacts},
        'limitations': RECIPES[name]['limitations'],
    }
    if name == 'engine':
        report = {'recipeName': RECIPES[name]['key'], 'proprietaryCodecs': False,
                  'effectiveArgs': effective, **report}
    (distribution / RECIPES[name]['manifest']).write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['apply', 'audit'])
    parser.add_argument('--recipe', choices=sorted(RECIPES), required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--distribution', type=Path)
    args = parser.parse_args()
    if args.command == 'apply':
        apply(args.source, args.recipe)
    else:
        if not args.distribution:
            parser.error('audit requires --distribution')
        audit(args.source, args.distribution, args.recipe)
