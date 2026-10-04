import { open, lstat } from 'node:fs/promises';
import { createHash, timingSafeEqual } from 'node:crypto';
import { argumentsFor, validateBundle } from './bundle.mjs';
try {
  const args = argumentsFor(['--input', '--sha256']);
  if (!/^[a-f0-9]{64}$/i.test(args['--sha256'])) throw new Error('Invalid digest');
  const stat = await lstat(args['--input']);
  if (!stat.isFile() || stat.isSymbolicLink() || stat.size > 4096) throw new Error('Invalid input file');
  const file = await open(args['--input'], 'r');
  try {
    const opened = await file.stat();
    if (!opened.isFile() || opened.size > 4096 || opened.dev !== stat.dev || opened.ino !== stat.ino) throw new Error('Input changed');
    const bytes = Buffer.alloc(4097);
    const { bytesRead } = await file.read(bytes, 0, bytes.length, 0);
    if (bytesRead !== opened.size || bytesRead > 4096) throw new Error('Input changed');
    const data = bytes.subarray(0, bytesRead);
    const digest = createHash('sha256').update(data).digest();
    if (!timingSafeEqual(digest, Buffer.from(args['--sha256'], 'hex'))) throw new Error('Digest mismatch');
    validateBundle(JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(data)));
    // No persistent state exists. Validation is the complete import operation; no files are extracted.
  } finally { await file.close(); }
} catch {
  process.stderr.write('odeum-relay data import failed\n');
  process.exitCode = 1;
}
