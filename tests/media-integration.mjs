// Production browser, disposable profile, synthetic loopback video only.
// The fixture reports actual decoded frames; codec declarations alone are not
// evidence of playback. No raw CDP, existing profile or external site is used.
import assert from 'node:assert/strict';
import http from 'node:http';
import net from 'node:net';
import { spawn, execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { createHash, randomBytes } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { writePrivateConfig } from '../adapter/dist/src/private-config.js';

const root = resolve(import.meta.dirname, '..');
const run = `media-integration-${Date.now()}-${randomBytes(4).toString('hex')}`;
const profile = resolve(root, '.cache', run), pipeName = `xenon-test-${run}`;
const pipe = `\\\\.\\pipe\\${pipeName}`, configPath = resolve(profile, 'client-config.json');
const binary = resolve(root, 'build/app/Release/Xenon.exe');
const requireH264 = process.argv.includes('--require-h264');
const sample = await readFile(resolve(root, 'tests/fixtures/media/synthetic-h264-aac.mp4'));
const fragmentedSample = await readFile(resolve(root, 'tests/fixtures/media/synthetic-h264-aac-fragmented.mp4'));
const runFile = promisify(execFile), sleep = ms => new Promise(r => setTimeout(r, ms));
const hashes = async () => Object.fromEntries(await Promise.all(['Xenon.dll', 'libcef.dll'].map(async name =>
  [name, createHash('sha256').update(await readFile(resolve(binary, '..', name))).digest('hex')])));
let browser, client, server, report, startHashes, endHashes, failure;
const alivePipe = () => new Promise(resolve => {
  const socket = net.connect(pipe);
  socket.once('connect', () => { socket.destroy(); resolve(true); });
  socket.once('error', () => { socket.destroy(); resolve(false); });
  socket.setTimeout(1500, () => { socket.destroy(); resolve(false); });
});
async function until(fn, label, ms = 30_000) {
  const deadline = Date.now() + ms;
  while (Date.now() < deadline) { const value = await fn(); if (value) return value; await sleep(150); }
  throw new Error(`${label} timed out`);
}
async function tool(name, args) {
  const value = await client.callTool({ name: `xenon_${name}`, arguments: args });
  assert(!value.isError, `${name}: ${JSON.stringify(value.structuredContent)}`);
  return value.structuredContent;
}
const fixture = `<!doctype html><meta charset="utf-8"><title>Xenon synthetic video fixture</title>
<h1>Website video playback</h1><video controls muted width="320" height="180"></video><p id="status">Preparing video</p>
<script>
(async()=>{
  const video=document.querySelector('video'), result={codecs:{}};
  for(const [name,type] of Object.entries({vp8:'video/webm; codecs="vp8"',vp9:'video/webm; codecs="vp9"',
    av1:'video/mp4; codecs="av01.0.04M.08"',h264:'video/mp4; codecs="avc1.42E01E"',aac:'audio/mp4; codecs="mp4a.40.2"'}))
    result.codecs[name]={canPlay:video.canPlayType(type),mediaSource:MediaSource.isTypeSupported(type)};
  const wait=ms=>new Promise(r=>setTimeout(r,ms));
  const event=(target,name)=>new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(Error(name+' timed out')),10000);
    target.addEventListener(name,()=>{clearTimeout(timer);resolve()},{once:true});
  });
  let stream, animation, objectUrl;
  try {
    if (${requireH264}) {
      if(!result.codecs.h264.canPlay||!result.codecs.aac.canPlay)throw Error('H.264/AAC missing from this CEF build');
      const loaded=event(video,'loadeddata');video.src='/sample.mp4';await loaded;
      const context=new OfflineAudioContext(1,1,48000);
      const audio=await context.decodeAudioData(await (await fetch('/sample.mp4')).arrayBuffer());
      const samples=audio.getChannelData(0);let energy=0;for(const value of samples)energy+=value*value;
      result.audio={duration:audio.duration,sampleRate:audio.sampleRate,rms:Math.sqrt(energy/samples.length)};
    } else {
      const canvas=document.createElement('canvas');canvas.width=320;canvas.height=180;
      const ctx=canvas.getContext('2d');let frame=0;
      const paint=()=>{ctx.fillStyle=frame++%2?'#1060c0':'#d05020';ctx.fillRect(0,0,320,180)};paint();
      stream=canvas.captureStream(15);animation=setInterval(paint,65);
      const chunks=[],recorder=new MediaRecorder(stream,{mimeType:'video/webm; codecs=vp8'});
      recorder.ondataavailable=e=>chunks.push(e.data);recorder.start();await wait(2500);
      const stopped=event(recorder,'stop');recorder.stop();await stopped;
      objectUrl=URL.createObjectURL(new Blob(chunks,{type:'video/webm'}));
      const loaded=event(video,'loadeddata');video.src=objectUrl;await loaded;
    }
    await video.play();await wait(700);
    result.playback={time:video.currentTime,frames:video.getVideoPlaybackQuality().totalVideoFrames,
      width:video.videoWidth,height:video.videoHeight,error:video.error?.code??null};
    video.pause();const pausedAt=video.currentTime;await wait(250);
    result.pause={paused:video.paused,stable:video.currentTime===pausedAt};
    const seeked=event(video,'seeked');video.currentTime=0.3;await seeked;
    result.seek=video.currentTime;video.volume=0.4;result.volume=video.volume;
    if (${requireH264}) {
      const media=new MediaSource(),opened=event(media,'sourceopen');
      objectUrl=URL.createObjectURL(media);video.src=objectUrl;await opened;
      const buffer=media.addSourceBuffer('video/mp4; codecs="avc1.42C01E, mp4a.40.2"');
      const bytes=await (await fetch('/sample-fragmented.mp4')).arrayBuffer();
      const appended=event(buffer,'updateend');buffer.appendBuffer(bytes);await appended;media.endOfStream();
      await video.play();await wait(700);
      result.streaming={time:video.currentTime,frames:video.getVideoPlaybackQuality().totalVideoFrames,error:video.error?.code??null};
      video.pause();
    }
    document.querySelector('#status').textContent='Video playback complete';
  }catch(error){result.error=error.message;document.querySelector('#status').textContent='Video playback failed';}
  finally{clearInterval(animation);stream?.getTracks().forEach(t=>t.stop());if(objectUrl)URL.revokeObjectURL(objectUrl);}
  await fetch('/report',{method:'POST',body:JSON.stringify(result)});
})();
</script>`;

try {
  assert.equal(process.platform, 'win32');
  await mkdir(profile, { recursive: true });
  const identity = { clientId: `media_${randomBytes(8).toString('hex')}`, token: randomBytes(32).toString('hex'), pipe };
  await writeFile(resolve(profile, 'SYNTHETIC_TEST_PROFILE'), 'XENON_SYNTHETIC_MEDIA_FIXTURE\n');
  await writeFile(resolve(profile, 'ui-settings.json'), JSON.stringify({ version: 1, introductionCompleted: true }));
  await writeFile(resolve(profile, 'broker-state.json'), JSON.stringify({ version: 1,
    clients: [{ id: identity.clientId, name: 'Synthetic media client', tokenHash: createHash('sha256').update(identity.token).digest('hex') }],
    workspaces: [{ id: 'native-default', clients: [identity.clientId] }], accountGrants: [], operations: [] }));
  await writePrivateConfig(configPath, identity);
  server = http.createServer((request, response) => {
    if (request.method === 'POST' && request.url === '/report') {
      let data = '';
      request.on('data', chunk => { data += chunk; if (data.length > 8192) request.destroy(); });
      request.on('end', () => { try { report = JSON.parse(data); response.writeHead(204).end(); } catch { response.writeHead(400).end(); } });
    } else if (request.method === 'GET' && ['/sample.mp4', '/sample-fragmented.mp4'].includes(request.url)) {
      const bytes = request.url === '/sample.mp4' ? sample : fragmentedSample;
      const headers = { 'Content-Type': 'video/mp4', 'Accept-Ranges': 'bytes', 'Cache-Control': 'no-store' };
      if (request.headers.range) {
        const match = /^bytes=(\d+)-(\d*)$/.exec(request.headers.range);
        const start = Number(match?.[1]), end = match?.[2] ? Math.min(Number(match[2]), bytes.length - 1) : bytes.length - 1;
        if (!match || !Number.isSafeInteger(start) || start > end || start >= bytes.length) {
          response.writeHead(416, { 'Content-Range': `bytes */${bytes.length}` }).end(); return;
        }
        response.writeHead(206, { ...headers, 'Content-Length': end - start + 1, 'Content-Range': `bytes ${start}-${end}/${bytes.length}` }).end(bytes.subarray(start, end + 1));
      } else response.writeHead(200, { ...headers, 'Content-Length': bytes.length }).end(bytes);
    } else if (request.method === 'GET' && request.url === '/') {
      response.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' }).end(fixture);
    } else response.writeHead(404).end();
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  startHashes = await hashes();
  browser = spawn(binary, [`--user-data-dir=${profile}`, `--broker-pipe=${pipeName}`], { stdio: 'ignore' });
  let launchError; browser.once('error', error => { launchError = error; });
  await until(() => {
    if (launchError) throw launchError;
    if (browser.exitCode !== null || browser.signalCode !== null) throw new Error('Browser exited before its private pipe became ready');
    return alivePipe();
  }, 'Browser startup', 45_000);
  client = new Client({ name: 'Xenon media regression', version: '1' }, { versionNegotiation: { mode: { pin: '2026-07-28' } } });
  await client.connect(new StdioClientTransport({ command: process.execPath,
    args: [resolve(root, 'adapter/dist/src/cli.js'), 'serve', '--config', configPath], stderr: 'pipe' }));
  const worker = await tool('worker_create', { name: 'Media observer', workspaceId: 'native-default' });
  await tool('tab_create', { agentSessionId: worker.agentSessionId, workspaceId: worker.workspaceId, url: `http://127.0.0.1:${server.address().port}/` });
  await until(() => report, 'Video report');
  console.log(JSON.stringify(report, null, 2));
  assert(!report.error, report.error);
  assert(report.playback.time > 0.2 && report.playback.frames > 2 && report.playback.width === 320 && report.playback.height === 180);
  assert.equal(report.playback.error, null);
  assert(report.pause.paused && report.pause.stable);
  assert(Math.abs(report.seek - 0.3) < 0.05);
  assert.equal(report.volume, 0.4);
  if (requireH264) {
    for (const codec of ['h264', 'aac']) assert(report.codecs[codec].canPlay && report.codecs[codec].mediaSource, `${codec} missing from this CEF build`);
    assert(report.audio.duration > 3.9 && report.audio.sampleRate === 48000 && report.audio.rms > 0.01, 'AAC tone must decode to non-silent audio');
    assert(report.streaming.time > 0.2 && report.streaming.frames > 2 && report.streaming.error === null, 'Media Source MP4 must decode and advance');
  }
} catch (error) { failure = error.message; console.error(error); process.exitCode = 1; }
finally {
  await client?.close().catch(() => {});
  if (browser?.pid && browser.exitCode === null && browser.signalCode === null)
    await runFile('taskkill.exe', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true }).catch(() => {});
  server?.close();
  endHashes = await hashes().catch(() => undefined);
  if (JSON.stringify(startHashes) !== JSON.stringify(endHashes)) { failure = 'Browser binaries changed during the test'; process.exitCode = 1; }
  await mkdir(resolve(root, 'out'), { recursive: true });
  await writeFile(resolve(root, 'out', `${run}.json`), JSON.stringify({ run, startHashes, endHashes,
    requireH264, sampleSha256: createHash('sha256').update(sample).digest('hex'),
    fragmentedSampleSha256: createHash('sha256').update(fragmentedSample).digest('hex'),
    coverage: requireH264 ? 'Synthetic H.264/AAC MP4 playback, AAC waveform decode, Media Source playback, pause, seek and volume. No physical input, audible output or DRM validation.' :
      'Synthetic VP8 decode, pause, seek and volume; codec capability declarations. No physical input, audible output, H.264 decode or DRM validation.',
    report, failure, passed: !failure }, null, 2));
  console.log(`Report: out/${run}.json`);
}
