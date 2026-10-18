# Synthetic video fixture

The two MP4 files contain four seconds of generated test colors and a
440 Hz sine wave. No recording, external media, user data or credential is used.
They are Apache-2.0 Xenon test assets with H.264 constrained baseline video at
320 x 180 / 15 fps and 48 kHz mono AAC audio. The ordinary MP4 exercises HTML
video and seeking; the fragmented version exercises Media Source playback.

Generated with FFmpeg 9.0.2 (test tooling only, not distributed with Xenon):

```powershell
ffmpeg -f lavfi -i 'testsrc2=size=320x180:rate=15:duration=4' `
  -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=4' `
  -c:v libx264 -profile:v baseline -level:v 3.0 -pix_fmt yuv420p -g 15 -crf 35 `
  -c:a aac -b:a 48k -movflags '+frag_keyframe+empty_moov+default_base_moof+global_sidx' `
  -shortest synthetic-h264-aac-fragmented.mp4
ffmpeg -i synthetic-h264-aac-fragmented.mp4 -c copy -movflags '+faststart' synthetic-h264-aac.mp4
```

Run `node tests/media-integration.mjs` for the stock engine's synthetic WebM
baseline, and `node tests/media-integration.mjs --require-h264` to require MP4,
nonblank decoded pictures, non-silent AAC from the media element, seek/resume,
and Media Source playback with video and audio. Use `--windows-codecs` for the
Windows-decoder build; this additionally verifies its engine provenance.
Use `--browser-dir <directory>` to test a separate complete production browser
output instead of `build/app/Release`; keep its matching engine and resources
together. The report binds the tested application and engine hashes.
Web Audio `decodeAudioData` is recorded separately and is not required for the
Windows path. Both modes use disposable profiles
and loopback HTTP. The MP4 check must fail against the stock CEF distribution.

For sustained playback diagnosis, generate larger synthetic files in an ignored
directory using a local FFmpeg tool (do not ship it or commit these larger files):

```powershell
New-Item -ItemType Directory -Force .cache/media-sustained | Out-Null
ffmpeg -f lavfi -i 'testsrc2=size=1920x1080:rate=60:duration=35' `
  -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=35' `
  -c:v libx264 -threads 4 -preset fast -profile:v high -level:v 4.2 `
  -pix_fmt yuv420p -g 120 -crf 28 -c:a aac -b:a 128k `
  -movflags '+frag_keyframe+empty_moov+default_base_moof' -shortest `
  .cache/media-sustained/synthetic-h264-aac-fragmented.mp4
ffmpeg -i .cache/media-sustained/synthetic-h264-aac-fragmented.mp4 `
  -c copy -movflags '+faststart' .cache/media-sustained/synthetic-h264-aac.mp4
node tests/media-integration.mjs --windows-codecs --sustained-dir .cache/media-sustained
```

This measures 30 seconds per phase after warmup: direct MP4, fully buffered MSE
with sampled PCM through Web Audio, and MSE through the normal media audio
renderer with muted output. Each phase uses a fresh element and audio context;
reusing a context across source changes can distort the measured media clock.
The last phase does not measure PCM or audible output. Reports include frame
callback gaps over 80 ms, dropped frames, animation gaps, buffering events,
sampled silence, and media/audio clock progress. The diagnostic fails on stalls,
large frame gaps, at least 1% dropped frames, or slow playback. These are local
performance thresholds, not deterministic CI checks or proof of physical display
cadence, short audio glitches, adaptive network streaming or arbitrary websites.
The automated suite does not select the test tab in the native shell, so it also
does not establish foreground window presentation. Check a visible player and its
dropped-frame statistics separately when diagnosing a displayed-video freeze.

Add `--animated-background` to open two synthetic CSS animations behind the video
tab. This mode waits for manual input: select **Xenon sustained video fixture** in
the disposable browser, click **Start playback test**, and leave it selected
until completion. Start within 30 seconds of the prompt. Each background page
animates a large gradient every five seconds, exercising concurrent presentation
without external websites, profile data or network traffic. Record the visible
window check separately: the report does not verify native foreground selection.
