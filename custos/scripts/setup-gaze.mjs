// Explicit build-time preparation; browsers only load same-origin local assets.
import { mkdir, cp, copyFile, readFile, writeFile, rename, unlink } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';
import { createHash } from 'node:crypto';

const root = fileURLToPath(new URL('../', import.meta.url));
const source = resolve(root, 'node_modules/@mediapipe/tasks-vision');
const output = resolve(root, 'public/gaze-assets');
const modelUrl = 'https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task';
const modelHash = '64184e229b263107bc2b804c6625db1341ff2bb731874b0bcc2fe6544e0bc9ff';
const temporary = resolve(output, 'face_landmarker.task.partial');
try {
    const metadata = JSON.parse(await readFile(resolve(source, 'package.json'), 'utf8'));
    if (metadata.version !== '1.0.1') throw new Error('Install the locked MediaPipe dependency with npm ci first.');
    await mkdir(output, { recursive: true });
    const localModel = process.env.ODEUM_GAZE_MODEL_PATH;
    let bytes;
    if (localModel) bytes = await readFile(localModel);
    else {
        const response = await fetch(modelUrl, { redirect: 'error', signal: AbortSignal.timeout(60_000) });
        if (!response.ok) throw new Error(`Model download failed: HTTP ${response.status}`);
        bytes = Buffer.from(await response.arrayBuffer());
    }
    if (createHash('sha256').update(bytes).digest('hex') !== modelHash) throw new Error('Face model checksum mismatch.');
    await cp(resolve(source, 'wasm'), resolve(output, 'wasm'), { recursive: true });
    await copyFile(resolve(source, 'vision_bundle.mjs'), resolve(output, 'vision_bundle.mjs'));
    await writeFile(resolve(output, 'NOTICE.txt'), 'MediaPipe Tasks Vision 1.0.1, Google, Apache-2.0. https://github.com/google-ai-edge/mediapipe\nFace Landmarker float16 v1. Camera inference runs on the device.\n', 'utf8');
    await writeFile(temporary, bytes);
    await rename(temporary, resolve(output, 'face_landmarker.task'));
    process.stdout.write('Odeum local gaze assets prepared.\n');
} catch (error) {
    process.stderr.write(`Gaze preparation failed: ${error.message}\n`);
    process.exitCode = 1;
} finally {
    await unlink(temporary).catch((error) => { if (error.code !== 'ENOENT') process.stderr.write('Could not remove partial model file.\n'); });
}
