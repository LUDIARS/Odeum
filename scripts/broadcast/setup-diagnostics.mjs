// @implements SPEC-VANMAC-BROADCAST
// Stable nonsecret exit codes survive Ex's intentional suppression of hook output.
export const setupStages = Object.freeze({
  platform: [20, 'macOS with no arguments is required'],
  account: [21, 'Ex must not run as root'],
  checkout: [22, 'an ordinary main checkout is required'],
  obs: [23, 'OBS executable is missing or inaccessible'],
  desktop: [24, 'Ex must run as the logged-in desktop user'],
  obsStopped: [25, 'OBS must be stopped; process inspection must succeed'],
  network: [26, 'exactly one local Tailscale IPv4 is required'],
  storage: [27, 'artifacts directory must be writable and not a symlink'],
  lock: [28, 'bootstrap lock unavailable; inspect concurrent or interrupted setup'],
  download: [29, 'MediaMTX download, checksum verification or extraction failed'],
  prepare: [30, 'private preparation failed; preserve partial files'],
  existing: [31, 'existing preparation is incomplete or does not match this host'],
  obsInstall: [32, 'OBS configuration conflicts or installation failed'],
  cleanup: [33, 'temporary files or owned lock could not be released'],
});
export function reportSetupFailure(stage) {
  const [code, message] = setupStages[stage];
  process.stderr.write(`Broadcast setup failed (${code}): ${message}\n`);
  process.exitCode = code;
}
