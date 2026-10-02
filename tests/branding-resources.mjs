// Read-only PE verification. Never loads or executes the inspected binaries.
import assert from 'node:assert/strict';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve, basename } from 'node:path';
import { pathToFileURL } from 'node:url';

const root = resolve(import.meta.dirname, '..');
const releaseVersion = (await readFile(resolve(root, 'VERSION'), 'utf8')).trim();
const expectedResourceVersion = releaseVersion.includes('-alpha.') ? releaseVersion.replace('-alpha.', '.') : releaseVersion + '.0';
const digest = bytes => createHash('sha256').update(bytes).digest('hex');
const align4 = value => Math.ceil(value / 4) * 4;
const bounds = (bytes, offset, length) => assert(Number.isSafeInteger(offset) && Number.isSafeInteger(length) && offset >= 0 && length >= 0 && offset + length <= bytes.length, 'PE data extends outside its file');

export function parsePe(bytes) {
  assert(bytes.length <= 128 * 1024 * 1024, 'PE exceeds verification size bound');
  bounds(bytes, 0, 64); assert.equal(bytes.readUInt16LE(0), 0x5a4d, 'DOS signature missing');
  const pe = bytes.readUInt32LE(60); bounds(bytes, pe, 24); assert.equal(bytes.readUInt32LE(pe), 0x4550, 'PE signature missing');
  const machine = bytes.readUInt16LE(pe + 4), count = bytes.readUInt16LE(pe + 6), optionalSize = bytes.readUInt16LE(pe + 20), optional = pe + 24;
  assert(count > 0 && count <= 96, 'Invalid PE section count'); bounds(bytes, optional, optionalSize);
  assert.equal(bytes.readUInt16LE(optional), 0x20b, 'Expected a PE32+ executable'); assert(optionalSize >= 112, 'Truncated optional header');
  const directoryCount = bytes.readUInt32LE(optional + 108); assert(directoryCount >= 16 && directoryCount <= 32); assert(optionalSize >= 112 + directoryCount * 8);
  const directories = Array.from({ length: directoryCount }, (_, index) => ({ rva: bytes.readUInt32LE(optional + 112 + index * 8), size: bytes.readUInt32LE(optional + 116 + index * 8) }));
  const sections = [], table = optional + optionalSize; bounds(bytes, table, count * 40);
  for (let index = 0; index < count; ++index) {
    const at = table + index * 40;
    const name = bytes.subarray(at, at + 8).toString('ascii').replace(/\0.*$/, '');
    assert(name && !sections.some(section => section.name === name), 'Duplicate/empty PE section name');
    const section = { name, virtualSize: bytes.readUInt32LE(at + 8), rva: bytes.readUInt32LE(at + 12), rawSize: bytes.readUInt32LE(at + 16), rawOffset: bytes.readUInt32LE(at + 20), flags: bytes.readUInt32LE(at + 36) };
    bounds(bytes, section.rawOffset, section.rawSize); sections.push(section);
  }
  const offsetOf = (rva, length) => {
    const found = sections.filter(section => rva >= section.rva && rva + length <= section.rva + section.rawSize);
    assert.equal(found.length, 1, 'PE RVA does not map to one file-backed section');
    const offset = found[0].rawOffset + rva - found[0].rva; bounds(bytes, offset, length); return offset;
  };
  const resourceDirectory = directories[2]; assert(resourceDirectory.rva && resourceDirectory.size, 'PE resource directory missing');
  const resourceBase = offsetOf(resourceDirectory.rva, resourceDirectory.size);
  const resourceSection = sections.find(section => resourceDirectory.rva >= section.rva && resourceDirectory.rva < section.rva + section.rawSize);
  assert(resourceSection && !(resourceSection.flags & 0x20000000), 'Resource directory occupies executable code');
  const resources = new Map(), seen = new Set();
  const resourceBounds = (offset, length) => { assert(offset >= 0 && offset + length <= resourceDirectory.size, 'Resource tree outside declared directory'); bounds(bytes, resourceBase + offset, length); };
  const resourceName = value => {
    if (!(value & 0x80000000)) return value;
    const offset = value & 0x7fffffff; resourceBounds(offset, 2); const length = bytes.readUInt16LE(resourceBase + offset);
    assert(length <= 2048, 'Resource name too long'); resourceBounds(offset + 2, length * 2);
    return bytes.subarray(resourceBase + offset + 2, resourceBase + offset + 2 + length * 2).toString('utf16le');
  };
  const walk = (offset, path) => {
    assert(path.length < 3 && !seen.has(offset), 'Cyclic or invalid resource tree'); seen.add(offset); resourceBounds(offset, 16);
    const entries = bytes.readUInt16LE(resourceBase + offset + 12) + bytes.readUInt16LE(resourceBase + offset + 14);
    assert(entries <= 4096, 'Resource directory too large'); resourceBounds(offset + 16, entries * 8);
    for (let index = 0; index < entries; ++index) {
      const at = resourceBase + offset + 16 + index * 8, value = bytes.readUInt32LE(at + 4), next = [...path, resourceName(bytes.readUInt32LE(at))];
      if (value & 0x80000000) walk(value & 0x7fffffff, next);
      else {
        assert.equal(next.length, 3, 'Unexpected resource leaf depth'); resourceBounds(value, 16);
        const data = resourceBase + value, rva = bytes.readUInt32LE(data), size = bytes.readUInt32LE(data + 4), key = JSON.stringify(next);
        assert(!resources.has(key), 'Duplicate resource leaf'); assert(size <= 32 * 1024 * 1024, 'Resource leaf too large');
        const start = offsetOf(rva, size); resources.set(key, { path: next, codePage: bytes.readUInt32LE(data + 8), bytes: bytes.subarray(start, start + size) });
      }
    }
  };
  walk(0, []);
  const identity = { machine, characteristics: bytes.readUInt16LE(pe + 22), magic: bytes.readUInt16LE(optional),
    sizeOfCode: bytes.readUInt32LE(optional + 4), sizeOfUninitializedData: bytes.readUInt32LE(optional + 12),
    entryPoint: bytes.readUInt32LE(optional + 16), baseOfCode: bytes.readUInt32LE(optional + 20), imageBase: bytes.readBigUInt64LE(optional + 24).toString(),
    sectionAlignment: bytes.readUInt32LE(optional + 32), fileAlignment: bytes.readUInt32LE(optional + 36),
    subsystem: bytes.readUInt16LE(optional + 68), dllCharacteristics: bytes.readUInt16LE(optional + 70),
    stackReserve: bytes.readBigUInt64LE(optional + 72).toString(), stackCommit: bytes.readBigUInt64LE(optional + 80).toString(),
    heapReserve: bytes.readBigUInt64LE(optional + 88).toString(), heapCommit: bytes.readBigUInt64LE(optional + 96).toString(),
    loaderFlags: bytes.readUInt32LE(optional + 104), directoryCount };
  return { bytes, sections, directories, resourceSection: resourceSection.name, resources, identity };
}

function versionInfo(bytes) {
  const readBlock = (start, end, depth = 0) => {
    assert(depth <= 4, 'Version resource nesting too deep'); bounds(bytes, start, 6);
    const length = bytes.readUInt16LE(start), valueLength = bytes.readUInt16LE(start + 2), type = bytes.readUInt16LE(start + 4);
    assert(length >= 6 && start + length <= end, 'Invalid version block length');
    let cursor = start + 6;
    while (cursor + 2 <= start + length && bytes.readUInt16LE(cursor) !== 0) cursor += 2;
    assert(cursor + 2 <= start + length, 'Unterminated version key');
    const key = bytes.subarray(start + 6, cursor).toString('utf16le'); cursor = align4(cursor + 2);
    const valueBytes = type === 1 ? valueLength * 2 : valueLength; assert(cursor + valueBytes <= start + length, 'Truncated version value');
    const value = bytes.subarray(cursor, cursor + valueBytes); cursor = align4(cursor + valueBytes);
    const children = [];
    while (cursor + 6 <= start + length && bytes.readUInt16LE(cursor)) { const child = readBlock(cursor, start + length, depth + 1); children.push(child); cursor = align4(cursor + child.length); }
    return { key, value, children, length };
  };
  const parsed = readBlock(0, bytes.length); assert.equal(parsed.key, 'VS_VERSION_INFO'); assert.equal(parsed.value.length, 52);
  assert.equal(parsed.value.readUInt32LE(0), 0xfeef04bd, 'Fixed version signature missing');
  const number = at => { const high = parsed.value.readUInt32LE(at), low = parsed.value.readUInt32LE(at + 4); return [high >>> 16, high & 0xffff, low >>> 16, low & 0xffff].join('.'); };
  const tables = parsed.children.find(child => child.key === 'StringFileInfo')?.children ?? []; assert(tables.length, 'Version strings missing');
  return { fileVersion: number(8), productVersion: number(16), fileType: parsed.value.readUInt32LE(36), tables: tables.map(table => Object.fromEntries(table.children.map(child => [child.key, child.value.toString('utf16le').replace(/\0+$/, '')]))) };
}

export function compareBootstrap(original, branded) {
  const originalVersion = [...original.resources.values()].filter(resource => resource.path[0] === 16);
  assert(originalVersion.length && originalVersion.every(resource => versionInfo(resource.bytes).tables.every(table => table.OriginalFilename === 'bootstrap.exe')), 'Original CEF bootstrap no longer has its unmodified identity');
  assert.deepEqual(branded.identity, original.identity, 'CEF executable identity or memory policy changed');
  assert.equal(branded.resourceSection, original.resourceSection, 'Resource section identity changed');
  const oldResource = original.sections.find(section => section.name === original.resourceSection), newResource = branded.sections.find(section => section.name === branded.resourceSection);
  assert.deepEqual({ ...newResource, virtualSize: 0, rawSize: 0 }, { ...oldResource, virtualSize: 0, rawSize: 0 }, 'Resource section location or flags changed');
  const oldReloc = original.sections.find(section => section.name === '.reloc'), newReloc = branded.sections.find(section => section.name === '.reloc');
  assert(oldReloc && newReloc, 'Base relocation section missing');
  const aligned = (value, alignment) => Math.ceil(value / alignment) * alignment;
  for (const [pe, resource, reloc] of [[original, oldResource, oldReloc], [branded, newResource, newReloc]]) {
    assert(!(reloc.flags & 0x20000000), 'Base relocation section became executable');
    assert.equal(pe.sections.at(-1), reloc, 'Expected base relocations to remain the final section');
    assert.equal(pe.sections.at(-2), resource, 'Expected resources immediately before base relocations');
    assert.equal(reloc.rva, aligned(resource.rva + Math.max(resource.virtualSize, resource.rawSize), pe.identity.sectionAlignment), 'Base relocation RVA is not the aligned resource end');
    assert.equal(reloc.rawOffset, resource.rawOffset + resource.rawSize, 'Base relocation file offset is not the resource end');
    assert.equal(pe.directories[5].rva, reloc.rva, 'Base relocation directory does not reference its section start');
    assert.equal(pe.directories[5].size, reloc.virtualSize, 'Base relocation directory does not cover the unchanged relocation payload');
  }
  assert.equal(newReloc.rva - oldReloc.rva, aligned(Math.max(newResource.virtualSize, newResource.rawSize), branded.identity.sectionAlignment) - aligned(Math.max(oldResource.virtualSize, oldResource.rawSize), original.identity.sectionAlignment), 'Base relocations moved beyond resource growth');
  assert.equal(newReloc.rawOffset - oldReloc.rawOffset, newResource.rawSize - oldResource.rawSize, 'Base relocation file movement exceeds resource growth');
  for (let index = 0; index < original.directories.length; ++index) {
    if (index === 5) assert.equal(branded.directories[index].size, original.directories[index].size, 'Base relocation directory size changed');
    else if (index !== 2) assert.deepEqual(branded.directories[index], original.directories[index], 'Non-resource PE data directory changed: ' + index);
  }
  const before = original.sections.filter(section => section.name !== original.resourceSection), after = branded.sections.filter(section => section.name !== branded.resourceSection);
  assert.equal(after.length, before.length, 'Non-resource section count changed');
  let executableBytes = 0;
  for (const section of before) {
    const match = after.find(item => item.name === section.name); assert(match, 'Bootstrap section missing: ' + section.name);
    if (section.name === '.reloc') assert.deepEqual({ ...match, rawOffset: 0, rva: 0 }, { ...section, rawOffset: 0, rva: 0 }, 'Base relocation section size/flags changed');
    else assert.deepEqual(match, section, 'Bootstrap section layout/flags changed: ' + section.name);
    assert(original.bytes.subarray(section.rawOffset, section.rawOffset + section.rawSize).equals(branded.bytes.subarray(match.rawOffset, match.rawOffset + match.rawSize)), 'Bootstrap section bytes changed: ' + section.name);
    if (section.flags & 0x20000000) executableBytes += section.rawSize;
  }
  assert(executableBytes > 0, 'No executable bytes verified');
  const keys = new Set([...original.resources.keys(), ...branded.resources.keys()]); let preservedResources = 0;
  const normalizedResourceCodePages = [];
  const normalizableResources = new Set([[5, 200, 1033], [6, 7, 1033], [6, 14, 1033], [6, 15, 1033], [10, 'CEF_REVOCATION_LIST', 1033], [24, 1, 1033]].map(path => JSON.stringify(path)));
  for (const key of keys) {
    const old = original.resources.get(key), current = branded.resources.get(key), type = (old ?? current).path[0];
    if ([3, 14, 16].includes(type)) continue;
    assert(old && current, 'Non-branding resource added/removed: ' + key);
    assert(current.bytes.equals(old.bytes), 'Non-branding resource bytes changed: ' + key); preservedResources++;
    if (current.codePage !== old.codePage) {
      // EndUpdateResource assigns the build host's Western ACP to unchanged
      // resource-directory entries. These pinned resource formats carry their
      // own encoding (Unicode dialog/strings/XML) or are retrieved as raw data.
      assert(normalizableResources.has(key) && old.codePage === 0 && current.codePage === 1252, 'Unexpected non-branding resource code page change: ' + key);
      normalizedResourceCodePages.push({ resource: old.path, original: old.codePage, branded: current.codePage });
    }
  }
  assert([...original.resources.values()].some(resource => resource.path[0] === 24), 'Original bootstrap manifest not found');
  return { executableBytes, identicalNonResourceSections: before.map(section => section.name), preservedResourceLeaves: preservedResources, manifestPreserved: true, normalizedResourceCodePages,
    resourceGrowthRelocation: { section: '.reloc', originalRva: oldReloc.rva, brandedRva: newReloc.rva, originalRawOffset: oldReloc.rawOffset, brandedRawOffset: newReloc.rawOffset, identicalPayloadBytes: oldReloc.rawSize } };
}

function checkIcon(pe, ico) {
  bounds(ico, 0, 6); assert.equal(ico.readUInt16LE(0), 0); assert.equal(ico.readUInt16LE(2), 1);
  const count = ico.readUInt16LE(4); assert(count > 0 && count <= 64); bounds(ico, 6, count * 16);
  const groups = [...pe.resources.values()].filter(resource => resource.path[0] === 14 && resource.path[1] === 101);
  assert(groups.length, 'Xenon icon group101 missing');
  const dimensions = [];
  for (const group of groups) {
    const bytes = group.bytes; bounds(bytes, 0, 6); assert.equal(bytes.readUInt16LE(0), 0); assert.equal(bytes.readUInt16LE(2), 1); assert.equal(bytes.readUInt16LE(4), count); bounds(bytes, 6, count * 14);
    for (let index = 0; index < count; ++index) {
      const sourceAt = 6 + index * 16, groupAt = 6 + index * 14;
      assert(ico.subarray(sourceAt, sourceAt + 12).equals(bytes.subarray(groupAt, groupAt + 12)), 'Icon group geometry/size differs from approved asset');
      const size = ico.readUInt32LE(sourceAt + 8), offset = ico.readUInt32LE(sourceAt + 12), imageId = bytes.readUInt16LE(groupAt + 12); bounds(ico, offset, size);
      const image = [...pe.resources.values()].find(resource => resource.path[0] === 3 && resource.path[1] === imageId && resource.path[2] === group.path[2]);
      assert(image && image.bytes.equals(ico.subarray(offset, offset + size)), 'Icon image differs from approved asset');
      if (group === groups[0]) dimensions.push({ width: ico[sourceAt] || 256, height: ico[sourceAt + 1] || 256 });
    }
  }
  assert(dimensions.some(size => size.width >= 32 && size.height >= 32), 'App icon lacks a usable window size');
  return { groupId: 101, languages: groups.map(group => group.path[2]), dimensions };
}

function checkVersion(pe, allowedNames, fileType) {
  const resources = [...pe.resources.values()].filter(resource => resource.path[0] === 16); assert(resources.length, 'Version resource missing');
  const names = new Set();
  for (const resource of resources) {
    const version = versionInfo(resource.bytes); assert.equal(version.fileVersion, expectedResourceVersion); assert.equal(version.productVersion, expectedResourceVersion); assert.equal(version.fileType, fileType, 'Fixed version file type is incorrect');
    for (const strings of version.tables) {
      for (const name of ['ProductName', 'FileDescription']) assert.equal(strings[name], 'Xenon Browser', name + ' branding mismatch');
      assert.equal(strings.CompanyName, 'Xenon Browser contributors');
      assert.equal(strings.FileVersion, expectedResourceVersion); assert.equal(strings.ProductVersion, expectedResourceVersion);
      assert(allowedNames.includes(strings.OriginalFilename), 'Unexpected branded OriginalFilename'); names.add(strings.OriginalFilename);
    }
  }
  return { fileVersion: expectedResourceVersion, productVersion: expectedResourceVersion, productName: 'Xenon Browser', originalFilenames: [...names] };
}

export async function verifyBranding({ release = resolve(root, 'build/app/Release') } = {}) {
  const results = [];
  const bytes = await Promise.all([readFile(resolve(root, 'third_party/cef/Release/bootstrap.exe')), readFile(resolve(release, 'Xenon.exe')),
    readFile(resolve(release, 'Xenon.dll')), readFile(resolve(root, 'assets/branding/xenon-icon.ico'))]);
  const [original, exe, dll] = bytes.slice(0, 3).map(parsePe);
  const check = (name, action) => { const details = action(); results.push({ name, passed: true, details }); };
  check('Resource branding preserves CEF executable code, memory policy and all non-branding resources', () => compareBootstrap(original, exe));
  check('Branded executable icon group matches the approved Xenon icon', () => checkIcon(exe, bytes[3]));
  check('Application DLL icon group matches the approved Xenon icon', () => checkIcon(dll, bytes[3]));
  check('Executable version resource identifies Xenon', () => checkVersion(exe, ['Xenon.exe'], 1));
  check('Application DLL version resource identifies Xenon', () => checkVersion(dll, ['Xenon.dll', 'XenonAuthTest.dll', 'AuthTest.dll'], 2));
  return { run: 'branding-' + Date.now(), capturedAt: new Date().toISOString(), binary: 'Xenon.exe', release: basename(release),
    originalBootstrapSha256: digest(bytes[0]), executableSha256: digest(bytes[1]), applicationDllSha256: digest(bytes[2]), iconSha256: digest(bytes[3]),
    passed: true, results, scope: 'Read-only PE/resources; no executable loading, browser launch, screenshot or visual appearance assertion' };
}

if (process.argv[1] && pathToFileURL(resolve(process.argv[1])).href === import.meta.url) {
  let report;
  try {
    const args = process.argv.slice(2); assert(args.length === 0 || (args.length === 2 && args[0] === '--release'), 'Usage: node tests/branding-resources.mjs [--release <extracted-release-directory>]');
    report = await verifyBranding(args.length ? { release: resolve(args[1]) } : {});
    for (const result of report.results) console.log('PASS ' + result.name);
  } catch (error) {
    report = { run: 'branding-' + Date.now(), capturedAt: new Date().toISOString(), passed: false, results: [{ name: 'Branding resource integrity', passed: false, error: error.message }] };
    console.log('FAIL ' + error.message);
  }
  await mkdir(resolve(root, 'out'), { recursive: true }); await writeFile(resolve(root, 'out/branding-results.json'), JSON.stringify(report, null, 2) + '\n');
  process.exitCode = report.passed ? 0 : 1;
}
