"""Fail-closed checks for Windows codec build provenance, without a CEF build."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('codec_build', ROOT / 'scripts/cef-windows-codecs.py')
codec_build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(codec_build)


class CodecAuditTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.source = Path(self.directory.name) / 'src'
        self.distribution = Path(self.directory.name) / 'dist'
        self.ffmpeg = self.source / 'third_party/ffmpeg/chromium/config/Chromium/win/x64'
        self.args = self.source / 'out/Release_GN_x64/args.gn'
        self.write(self.args, '''xenon_windows_codecs=true
proprietary_codecs=true
ffmpeg_branding="Chromium"
media_use_openh264=false
enable_ffmpeg_video_decoders=false
''')
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
        self.recipe = json.loads((ROOT / 'dependencies.lock.json').read_text())['cef']['mediaSourceBuild']
        self.recipe_patch = ROOT / self.recipe['patch']['path']
        self.scheduling_patch = ROOT / self.recipe['videoSchedulingPatch']['path']
        self.patches = [self.recipe_patch, self.scheduling_patch]

    @staticmethod
    def write(path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8')

    def audit(self):
        with patch.object(codec_build, 'validate', return_value=(self.recipe, self.patches)), \
             patch.object(codec_build, 'run', side_effect=lambda *args, **kwargs: self.deps if 'desc' in args else ''), \
             patch('builtins.print'):
            codec_build.audit(self.source, self.distribution)

    def test_manifest_binds_actual_artifact_bytes(self):
        self.audit()
        report = json.loads((self.distribution / 'xenon-codec-build.json').read_text())
        self.assertEqual(report['files']['Release/libcef.dll'], codec_build.sha(self.distribution / 'Release/libcef.dll'))
        self.assertFalse(report['bundledH264AacDecoders'])

    def test_rejects_software_fallback_configuration(self):
        for name, old, new in [('ffmpeg_branding', '"Chromium"', '"Chrome"'),
                               ('media_use_openh264', 'false', 'true'),
                               ('enable_ffmpeg_video_decoders', 'false', 'true')]:
            with self.subTest(name=name):
                original = self.args.read_text()
                self.write(self.args, original.replace(f'{name}={old}', f'{name}={new}'))
                with self.assertRaisesRegex(RuntimeError, 'Unexpected GN argument'):
                    self.audit()
                self.write(self.args, original)

    def test_rejects_registered_ffmpeg_aac_decoder(self):
        self.write(self.ffmpeg / 'libavcodec/codec_list.c', '&ff_aac_decoder, NULL\n')
        with self.assertRaisesRegex(RuntimeError, 'Bundled FFmpeg H.264/AAC'):
            self.audit()

    def test_rejects_enabled_h264_component_even_if_unregistered(self):
        path = self.ffmpeg / 'config_components.h'
        self.write(path, path.read_text().replace('CONFIG_H264_DECODER 0', 'CONFIG_H264_DECODER 1'))
        with self.assertRaisesRegex(RuntimeError, 'Bundled FFmpeg component'):
            self.audit()

    def test_rejects_alternate_software_codec_in_dependency_graph(self):
        for dependency in ['//third_party/openh264:encoder', '//third_party/rust/symphonia_codec_aac/v0_6:lib']:
            with self.subTest(dependency=dependency):
                self.deps = dependency
                with self.assertRaisesRegex(RuntimeError, 'linked into libcef'):
                    self.audit()

    def test_checked_in_patch_matches_pin(self):
        self.assertEqual(codec_build.sha(self.recipe_patch), self.recipe['patch']['sha256'])
        self.assertEqual(codec_build.sha(self.scheduling_patch), self.recipe['videoSchedulingPatch']['sha256'])

    def test_audit_rejects_missing_scheduling_patch_before_writing_manifest(self):
        def check(*args, **kwargs):
            if str(self.scheduling_patch) in args:
                raise RuntimeError('Scheduling patch not applied')
            return ''
        with patch.object(codec_build, 'validate', return_value=(self.recipe, self.patches)), \
             patch.object(codec_build, 'run', side_effect=check):
            with self.assertRaisesRegex(RuntimeError, 'Scheduling patch not applied'):
                codec_build.audit(self.source, self.distribution)
        self.assertFalse((self.distribution / 'xenon-codec-build.json').exists())

    def test_apply_preflights_all_pending_patches_before_mutating(self):
        with patch.object(codec_build, 'validate', return_value=(self.recipe, self.patches)), \
             patch.object(codec_build.subprocess, 'run') as reverse, \
             patch.object(codec_build, 'run', side_effect=RuntimeError('Patch conflict')) as run:
            reverse.return_value.returncode = 1
            with self.assertRaisesRegex(RuntimeError, 'Patch conflict'):
                codec_build.apply(self.source)
        run.assert_called_once_with('git', 'apply', '--check', '--whitespace=error-all',
                                    *map(str, self.patches), cwd=self.source)

    def test_rejects_stale_distribution(self):
        self.write(self.distribution / 'Release/libcef.dll', 'older build with different codecs')
        with self.assertRaisesRegex(RuntimeError, 'Distribution does not match'):
            self.audit()


if __name__ == '__main__':
    unittest.main()
