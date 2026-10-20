"""Fail-closed checks for the codec-free engine recipe and its provenance, without a CEF build."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('source_build', ROOT / 'scripts/cef-source-build.py')
source_build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(source_build)
LOCK = json.loads((ROOT / 'dependencies.lock.json').read_text())


class EngineRecipeTests(unittest.TestCase):
    def setUp(self):
        self.recipe = copy.deepcopy(LOCK['cef']['engineSourceBuild'])

    def test_pinned_engine_recipe_is_codec_free(self):
        source_build.check_recipe('engine', self.recipe)
        self.assertNotIn('patch', self.recipe)
        media_patch = LOCK['cef']['mediaSourceBuild']['patch']['path']
        self.assertNotIn(media_patch, json.dumps(self.recipe))

    def test_engine_recipe_matches_the_local_engine_without_codecs(self):
        # Releases carry the same engine-behavior patches and source pins as
        # the local codec build, so they behave alike apart from H.264/AAC.
        media = LOCK['cef']['mediaSourceBuild']
        for key in ['cefCommit', 'chromiumCommit', 'branch', 'depotToolsCommit', 'automateSha256', 'windowsSdk',
                    'videoSchedulingPatch', 'backgroundFrameRatePatch', 'extensionActionPatch']:
            self.assertEqual(self.recipe[key], media[key], key)
        build_options = 'is_official_build=true use_thin_lto=false chrome_pgo_phase=0 symbol_level=0'
        self.assertTrue(self.recipe['gnDefines'].startswith(build_options + ' '))
        self.assertTrue(media['gnDefines'].startswith(build_options + ' '))

    def test_checked_in_engine_patches_match_pins(self):
        for key in source_build.RECIPES['engine']['patches']:
            self.assertEqual(source_build.sha(ROOT / self.recipe[key]['path']), self.recipe[key]['sha256'], key)

    def test_rejects_codec_lock_edits(self):
        media = LOCK['cef']['mediaSourceBuild']
        edits = {
            'codec patch': lambda recipe: recipe.update(patch=copy.deepcopy(media['patch'])),
            'proprietary codecs': lambda recipe: recipe.update(gnDefines=recipe['gnDefines'].replace('proprietary_codecs=false', 'proprietary_codecs=true')),
            'implicit codecs': lambda recipe: recipe.update(gnDefines=recipe['gnDefines'].replace(' proprietary_codecs=false', '')),
            'windows codecs': lambda recipe: recipe.update(gnDefines=recipe['gnDefines'] + ' xenon_windows_codecs=true'),
            'chrome branding': lambda recipe: recipe.update(gnDefines=recipe['gnDefines'].replace('ffmpeg_branding=Chromium', 'ffmpeg_branding=Chrome')),
            'duplicate define': lambda recipe: recipe.update(gnDefines=recipe['gnDefines'] + ' proprietary_codecs=true'),
            'codec backend': lambda recipe: recipe.update(codecBackend='windows'),
            'missing engine patch': lambda recipe: recipe.pop('backgroundFrameRatePatch'),
        }
        for name, edit in edits.items():
            with self.subTest(edit=name):
                recipe = copy.deepcopy(self.recipe)
                edit(recipe)
                with self.assertRaises(RuntimeError):
                    source_build.check_recipe('engine', recipe)

    def test_media_recipe_keeps_its_own_patch_set(self):
        media = copy.deepcopy(LOCK['cef']['mediaSourceBuild'])
        source_build.check_recipe('media', media)
        media.pop('patch')
        with self.assertRaisesRegex(RuntimeError, 'Unexpected patch set'):
            source_build.check_recipe('media', media)


class EngineSourceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.source = Path(self.directory.name) / 'src'
        self.options = self.source / 'media/media_options.gni'
        self.options.parent.mkdir(parents=True)
        self.options.write_text('declare_args() {\n  link_pulseaudio = false\n}\n')
        recipe = LOCK['cef']['engineSourceBuild']
        self.revisions = {str(self.source): recipe['chromiumCommit'], str(self.source / 'cef'): recipe['cefCommit']}

    def revision(self, *args, cwd=None):
        return self.revisions[str(cwd)]

    def test_validate_selects_only_engine_patches(self):
        with patch.object(source_build, 'run', side_effect=self.revision):
            _, patches = source_build.validate(self.source, 'engine')
        recipe = LOCK['cef']['engineSourceBuild']
        self.assertEqual(patches, [ROOT / recipe[key]['path'] for key in source_build.RECIPES['engine']['patches']])

    def test_validate_rejects_a_checkout_carrying_the_codec_patch(self):
        self.options.write_text('declare_args() {\n  xenon_windows_codecs = false\n}\n')
        with patch.object(source_build, 'run', side_effect=self.revision):
            with self.assertRaisesRegex(RuntimeError, 'Codec patch present'):
                source_build.validate(self.source, 'engine')

    def test_validate_rejects_a_different_chromium_revision(self):
        self.revisions[str(self.source)] = '0' * 40
        with patch.object(source_build, 'run', side_effect=self.revision):
            with self.assertRaisesRegex(RuntimeError, 'Chromium revision mismatch'):
                source_build.validate(self.source, 'engine')


class EngineAuditTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.source = Path(self.directory.name) / 'src'
        self.distribution = Path(self.directory.name) / 'dist'
        self.ffmpeg = self.source / 'third_party/ffmpeg/chromium/config/Chromium/win/x64'
        self.args = self.source / 'out/Release_GN_x64/args.gn'
        self.write(self.args, 'is_official_build=true\nproprietary_codecs=false\nffmpeg_branding="Chromium"\n')
        self.write(self.ffmpeg / 'libavcodec/codec_list.c', 'static const FFCodec *const codec_list[] = { &ff_flac_decoder, NULL };\n')
        self.write(self.ffmpeg / 'config_components.h', ''.join(
            f'#define CONFIG_{name} 0\n' for name in [
                'AAC_DECODER', 'AAC_FIXED_DECODER', 'H264_DECODER',
                'AAC_ENCODER', 'LIBOPENH264_ENCODER']))
        for name in ['Release/libcef.dll', 'Release/bootstrap.exe', 'LICENSE.txt', 'CREDITS.html']:
            self.write(self.distribution / name, 'synthetic fixture: ' + name)
        for name in ['libcef.dll', 'bootstrap.exe']:
            self.write(self.source / 'out/Release_GN_x64' / name, 'synthetic fixture: Release/' + name)
        self.deps = '//media/filters:filters\n//third_party/ffmpeg:ffmpeg_internal'
        # Defaulted and overridden values both appear in GN's short listing.
        self.listing = 'enable_widevine = false\nffmpeg_branding = "Chromium"\nproprietary_codecs = false\n'
        self.recipe = LOCK['cef']['engineSourceBuild']
        self.patches = [ROOT / self.recipe[key]['path'] for key in source_build.RECIPES['engine']['patches']]

    @staticmethod
    def write(path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8')

    def fake_run(self, *args, **kwargs):
        if 'desc' in args:
            return self.deps
        if 'args' in args:
            return self.listing
        return ''

    def audit(self):
        with patch.object(source_build, 'validate', return_value=(self.recipe, self.patches)), \
             patch.object(source_build, 'run', side_effect=self.fake_run), \
             patch('builtins.print'):
            source_build.audit(self.source, self.distribution, 'engine')

    def manifest(self):
        return self.distribution / 'xenon-engine-build.json'

    def test_manifest_binds_recipe_and_artifact_bytes(self):
        self.audit()
        report = json.loads(self.manifest().read_text())
        self.assertEqual(report['recipeName'], 'engineSourceBuild')
        self.assertEqual(report['recipe'], self.recipe)
        self.assertEqual(report['codecBackend'], 'none')
        self.assertIs(report['proprietaryCodecs'], False)
        self.assertIs(report['bundledH264AacDecoders'], False)
        self.assertEqual(report['effectiveArgs'], {'proprietary_codecs': 'false', 'ffmpeg_branding': '"Chromium"'})
        self.assertEqual(report['files']['Release/libcef.dll'], source_build.sha(self.distribution / 'Release/libcef.dll'))
        self.assertFalse((self.distribution / 'xenon-codec-build.json').exists())

    def test_rejects_effective_codec_configuration(self):
        for old, new in [('proprietary_codecs = false', 'proprietary_codecs = true'),
                         ('ffmpeg_branding = "Chromium"', 'ffmpeg_branding = "Chrome"'),
                         ('proprietary_codecs = false\n', '')]:
            with self.subTest(change=new or 'missing'):
                original = self.listing
                self.listing = original.replace(old, new)
                with self.assertRaisesRegex(RuntimeError, 'Unexpected GN argument'):
                    self.audit()
                self.listing = original
        self.assertFalse(self.manifest().exists())

    def test_rejects_windows_codec_argument(self):
        self.write(self.args, self.args.read_text() + 'xenon_windows_codecs=false\n')
        with self.assertRaisesRegex(RuntimeError, 'xenon_windows_codecs'):
            self.audit()

    def test_rejects_bundled_h264_or_aac(self):
        self.write(self.ffmpeg / 'libavcodec/codec_list.c', '&ff_h264_decoder, NULL\n')
        with self.assertRaisesRegex(RuntimeError, 'Bundled FFmpeg H.264/AAC'):
            self.audit()

    def test_rejects_linked_openh264(self):
        self.deps = '//third_party/openh264:encoder'
        with self.assertRaisesRegex(RuntimeError, 'linked into libcef'):
            self.audit()

    def test_rejects_missing_engine_patch_before_writing_manifest(self):
        def check(*args, **kwargs):
            if str(self.patches[1]) in args:
                raise RuntimeError('Frame rate patch not applied')
            return self.fake_run(*args, **kwargs)
        with patch.object(source_build, 'validate', return_value=(self.recipe, self.patches)), \
             patch.object(source_build, 'run', side_effect=check):
            with self.assertRaisesRegex(RuntimeError, 'Frame rate patch not applied'):
                source_build.audit(self.source, self.distribution, 'engine')
        self.assertFalse(self.manifest().exists())

    def test_rejects_distribution_carrying_codec_provenance(self):
        self.write(self.distribution / 'xenon-codec-build.json', '{}')
        with self.assertRaisesRegex(RuntimeError, 'xenon-codec-build.json'):
            self.audit()
        self.assertFalse(self.manifest().exists())

    def test_rejects_stale_distribution(self):
        self.write(self.distribution / 'Release/bootstrap.exe', 'older bootstrap')
        with self.assertRaisesRegex(RuntimeError, 'Distribution does not match'):
            self.audit()

    def test_apply_preflights_only_engine_patches(self):
        with patch.object(source_build, 'validate', return_value=(self.recipe, self.patches)), \
             patch.object(source_build.subprocess, 'run') as reverse, \
             patch.object(source_build, 'run', side_effect=RuntimeError('Patch conflict')) as run:
            reverse.return_value.returncode = 1
            with self.assertRaisesRegex(RuntimeError, 'Patch conflict'):
                source_build.apply(self.source, 'engine')
        run.assert_called_once_with('git', 'apply', '--check', '--whitespace=error-all',
                                    *map(str, self.patches), cwd=self.source)


if __name__ == '__main__':
    unittest.main()
