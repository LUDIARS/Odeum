// @implements SPEC-VANMAC-BROADCAST
import { exportData } from './data-format.mjs';
exportData(process.argv.slice(2)).catch(() => {
  process.stderr.write('Broadcast export failed: invalid output or existing file\n');
  process.exitCode = 1;
});
