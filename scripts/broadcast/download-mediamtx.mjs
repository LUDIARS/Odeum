// @implements SPEC-VANMAC-BROADCAST
import { createHash } from 'node:crypto';
import { writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';

// Official v1.21.1 release asset digests, verified 2026-10-05 through GitHub's API.
const releases = {
  x64: ['amd64', 'be403a36d2225668ea695cbd2c784109bc23ef9a32f886837e43c920b6818813'],
  arm64: ['arm64', '25e20ed41611f1f3103b8359585210b29b11b69fa0d9e11bd11b92f7bbcb42ef'],
};
export async function downloadMediaMtx(directory) {
  const release = releases[process.arch];
  if (!release) throw new Error('Unsupported macOS architecture');
  const response = await fetch(`https://github.com/bluenviron/mediamtx/releases/download/v1.21.1/mediamtx_v1.21.1_darwin_${release[0]}.tar.gz`, {
    signal: AbortSignal.timeout(120_000),
  });
  if (!response.ok || !response.body) throw new Error('MediaMTX download failed');
  const chunks = [];
  let size = 0;
  for await (const chunk of response.body) {
    size += chunk.length;
    if (size > 100 * 1024 * 1024) throw new Error('MediaMTX archive exceeds size limit');
    chunks.push(chunk);
  }
  const archive = Buffer.concat(chunks);
  if (createHash('sha256').update(archive).digest('hex') !== release[1]) throw new Error('MediaMTX checksum mismatch');
  const archivePath = join(directory, 'release.tar.gz');
  await writeFile(archivePath, archive, { flag: 'wx', mode: 0o600 });
  // Extract only the binary to stdout; no archive paths are written to the filesystem.
  const { stdout } = await promisify(execFile)('/usr/bin/tar', ['-xOf', archivePath, 'mediamtx'], {
    cwd: directory, encoding: 'buffer', maxBuffer: 100 * 1024 * 1024, timeout: 30_000,
  });
  const binary = join(directory, 'mediamtx');
  await writeFile(binary, stdout, { flag: 'wx', mode: 0o700 });
  return binary;
}
