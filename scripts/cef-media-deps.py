"""Generate a separate CEF dependency manifest without branded updater tests."""
import ast
import pathlib
import subprocess
import sys


def media_manifest(source):
    tree = ast.parse(source)
    deps = [node.value for node in tree.body if isinstance(node, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == 'deps'
                    for target in node.targets)]
    if len(deps) != 1 or not isinstance(deps[0], ast.Dict):
        raise ValueError('Expected exactly one upstream deps dictionary')
    omitted = {f'src/third_party/updater/chrome_win_{arch}{suffix}/cipd'
               for arch in ('arm64', 'x86', 'x86_64')
               for suffix in ('', '_sans_iid')}
    # AST column offsets are UTF-8 byte offsets. Replace only the six values;
    # leave every other dependency, condition, hook and pin byte-for-byte intact.
    lines = source.encode('utf-8').splitlines(keepends=True)
    offsets = [0]
    for line in lines:
        offsets.append(offsets[-1] + len(line))
    replacements = []
    found = set()
    for key, value in zip(deps[0].keys, deps[0].values):
        name = key.value if isinstance(key, ast.Constant) else None
        if name not in omitted:
            continue
        if name in found or not isinstance(value, ast.Dict):
            raise ValueError('Unexpected updater test dependency structure')
        entry = ast.literal_eval(value)
        if entry.get('dep_type') != 'cipd' or entry.get('condition') != 'checkout_win':
            raise ValueError('Unexpected updater test dependency type/condition')
        found.add(name)
        replacements.append((offsets[value.lineno - 1] + value.col_offset,
                             offsets[value.end_lineno - 1] + value.end_col_offset))
    if found != omitted:
        raise ValueError('The pinned updater test dependency set changed')
    result = source.encode('utf-8')
    for start, end in reversed(replacements):
        result = result[:start] + b'None' + result[end:]
    return result.decode('utf-8')


if __name__ == '__main__':
    source = pathlib.Path(sys.argv[1])
    destination = source.with_name('DEPS.xenon-media')
    # CEF applies its own patches to DEPS during project generation. Always
    # derive the optional checkout manifest from the verified Git revision on
    # initial and resumed builds, without reverting those official patches.
    content = (subprocess.check_output(['git', '-C', str(source.parent), 'show', 'HEAD:DEPS'])
               if '--pristine' in sys.argv[2:] else source.read_bytes())
    destination.write_bytes(media_manifest(content.decode('utf-8')).encode('utf-8'))
    print(f'Generated {destination}; omitted six branded updater test archives')
