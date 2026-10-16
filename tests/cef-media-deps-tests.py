import importlib.util
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    'cef_media_deps', Path(__file__).parents[1] / 'scripts/cef-media-deps.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MediaDepsTests(unittest.TestCase):
    def fixture(self):
        entries = []
        for arch in ('arm64', 'x86', 'x86_64'):
            for suffix in ('', '_sans_iid'):
                entries.append(f"'src/third_party/updater/chrome_win_{arch}{suffix}/cipd': "
                               "{'dep_type': 'cipd', 'condition': 'checkout_win', "
                               "'packages': [{'package': 'test-updater', 'version': 'pinned'}]}")
        return "# Unicode comment: \u00e9\ndeps = {\n" + ',\n'.join(entries) + ",\n'keep': {'url': 'pinned'}}\nhooks = ['preserve']\n"

    def test_preserves_other_dependencies_and_hooks(self):
        source = self.fixture()
        result = module.media_manifest(source)
        removed = "{'dep_type': 'cipd', 'condition': 'checkout_win', 'packages': [{'package': 'test-updater', 'version': 'pinned'}]}"
        self.assertEqual(result, source.replace(removed, 'None'))

    def test_missing_package_fails_closed(self):
        with self.assertRaises(ValueError):
            module.media_manifest(self.fixture().replace('chrome_win_x86/cipd', 'other/cipd'))

    def test_changed_condition_fails_closed(self):
        with self.assertRaises(ValueError):
            module.media_manifest(self.fixture().replace("'checkout_win'", "'checkout_linux'", 1))


if __name__ == '__main__':
    unittest.main()
