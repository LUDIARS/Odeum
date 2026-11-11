// Composes the recorded views into one side-by-side video and checks that it plays.
// Playwright records each page as VP8 webm. The deliverable is H.264 mp4 (libx264 from the
// system ffmpeg); the raw webm files are kept next to it.
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

function tool(name) {
  try { execFileSync(name, ['-version'], { stdio: 'ignore' }); } catch { throw new Error(`${name} is not on PATH; it is needed to compose the video`); }
  return name;
}

function probe(file) {
  const out = JSON.parse(execFileSync(tool('ffprobe'), ['-v', 'error', '-print_format', 'json', '-show_format', '-show_streams', file], { encoding: 'utf8' }));
  const video = out.streams.find((stream) => stream.codec_type === 'video');
  if (!video) throw new Error(`${file} has no video stream`);
  return { file, codec: video.codec_name, width: video.width, height: video.height, seconds: Number(out.format.duration), bytes: Number(out.format.size) };
}

// views: [{ file, startedAt (ms), width, height }] left to right. The earliest start is t = 0;
// later views are padded at the front so the same moment lines up across views.
function compose({ views, output }) {
  const ffmpeg = tool('ffmpeg');
  const origin = Math.min(...views.map((view) => view.startedAt));
  const height = Math.max(...views.map((view) => view.height));
  const inputs = views.flatMap((view) => ['-i', view.file]);
  const chains = views.map((view, index) => {
    const delay = ((view.startedAt - origin) / 1000).toFixed(3);
    return `[${index}:v]setpts=PTS-STARTPTS,tpad=start_duration=${delay}:color=white,`
      + `scale=${view.width}:${view.height},pad=${view.width}:${height}:0:0:white,setsar=1[v${index}]`;
  });
  const filter = `${chains.join(';')};${views.map((_, index) => `[v${index}]`).join('')}hstack=inputs=${views.length},fps=25,format=yuv420p[out]`;
  execFileSync(ffmpeg, ['-y', '-v', 'error', ...inputs, '-filter_complex', filter, '-map', '[out]', '-c:v', 'libx264', '-preset', 'veryfast',
    '-crf', '28', '-movflags', '+faststart', output], { stdio: ['ignore', 'inherit', 'inherit'] });
  return output;
}

// Decodes the whole file (fails on a broken stream) and saves a few frames for a reviewer.
function verify(file, frameDir, count = 4) {
  const ffmpeg = tool('ffmpeg');
  execFileSync(ffmpeg, ['-v', 'error', '-i', file, '-f', 'null', '-'], { stdio: ['ignore', 'ignore', 'pipe'] });
  const info = probe(file);
  fs.mkdirSync(frameDir, { recursive: true });
  const frames = [];
  for (let i = 0; i < count; i += 1) {
    const at = info.seconds * (0.12 + (0.8 * i) / Math.max(1, count - 1));
    const png = path.join(frameDir, `frame-${String(i + 1).padStart(2, '0')}-${Math.round(at)}s.png`);
    execFileSync(ffmpeg, ['-y', '-v', 'error', '-ss', at.toFixed(2), '-i', file, '-frames:v', '1', png]);
    frames.push(png);
  }
  return { ...info, frames };
}

module.exports = { compose, verify, probe };
