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
AAC waveform decoding and Media Source playback. Both use disposable profiles
and loopback HTTP. The MP4 check must fail against the stock CEF distribution.
