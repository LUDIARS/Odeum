// @implements SPEC-VANMAC-BROADCAST
import { importData } from './data-format.mjs';
importData(process.argv.slice(2)).catch(() => {
  process.stderr.write('Broadcast import failed: invalid input, checksum or format\n');
  process.exitCode = 1;
});
