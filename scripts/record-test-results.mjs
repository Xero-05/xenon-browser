import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
const root=resolve(import.meta.dirname,'..');
const sha256=async p=>createHash('sha256').update(await readFile(resolve(root,p))).digest('hex');
const destination=resolve(root,'docs/test-results');await mkdir(destination,{recursive:true});
const manual=JSON.parse(await readFile(resolve(destination,'native-ui-manual.json'),'utf8'));
if(!manual.passed||manual.applicationDllSha256!==await sha256('build/app/Release/Xenon.dll'))
  throw new Error('Native UI observations are missing or were not recorded against the current binary');
const reports=[];
const branding=JSON.parse(await readFile(resolve(root,'out/branding-results.json'),'utf8'));
if(!branding.passed||branding.applicationDllSha256!==await sha256('build/app/Release/Xenon.dll')||
   branding.executableSha256!==await sha256('build/app/Release/Xenon.exe'))
  throw new Error('Branding verification does not match the current browser');
await writeFile(resolve(destination,'branding.json'),JSON.stringify(branding,null,2)+'\n');
for(const name of ['integration','auth-integration','persistence-integration','native-input-smoke','visibility-integration','worker-lifecycle-integration','startup-integration','workspace-removal-integration','upload-entry-integration','human-autofill-integration']) {
  const report=JSON.parse(await readFile(resolve(root,`out/${name}-results.json`),'utf8'));
  delete report.profile;
  if(report.benchmark)delete report.benchmark.syntheticRoot;
  const fixture=name==='auth-integration'||name==='human-autofill-integration'||(name==='workspace-removal-integration'&&!report.nativeApprovalRequired);
  report.binary=fixture?'build/auth-fixture/Release/XenonAuthTest.exe':'build/app/Release/Xenon.exe';
  report.applicationDll=fixture?'build/auth-fixture/Release/XenonAuthTest.dll':'build/app/Release/Xenon.dll';
  const currentHash=await sha256(report.applicationDll);
  if(report.applicationDllSha256!==currentHash)throw new Error(`${name} was not run against the current binary`);
  await writeFile(resolve(destination,`${name}.json`),JSON.stringify(report,null,2)+'\n');
  reports.push({name,passed:report.passed,checks:report.results.length});
}
for(const name of ['native-tests','adapter-tests','login-capture-tests','human-autofill-tests']) {
  const xml=(await readFile(resolve(root,`out/${name}.xml`),'utf8'))
    .replace(/\s+hostname="[^"]*"/g,'')
    .replaceAll(root.replaceAll('\\','/'),'[workspace]')
    .replaceAll(root,'[workspace]');
  if(/<failure\b|<error\b/.test(xml))throw new Error(`${name} has failing checks`);
  await writeFile(resolve(destination,`${name}.xml`),xml);
}
await writeFile(resolve(destination,'build.json'),JSON.stringify({
  capturedAt:new Date().toISOString(),platform:'Windows x64',stage:'unsigned local alpha',
  dependencies:JSON.parse(await readFile(resolve(root,'dependencies.lock.json'),'utf8')),
  applicationDllSha256:await sha256('build/app/Release/Xenon.dll'),
  bootstrapSha256:await sha256('build/app/Release/Xenon.exe'),
  reports,
},null,2)+'\n');
if(reports.some(r=>!r.passed))throw new Error('Live acceptance contains failures');
console.log('Sanitized acceptance reports saved in docs/test-results/.');
