import { writeFile } from 'node:fs/promises';
import { argumentsFor, bundle } from './bundle.mjs';
try {
  const args = argumentsFor(['--output']);
  await writeFile(args['--output'], `${JSON.stringify(bundle)}\n`, { encoding: 'utf8', flag: 'wx', mode: 0o600 });
} catch {
  process.stderr.write('odeum-relay data export failed\n');
  process.exitCode = 1;
}
