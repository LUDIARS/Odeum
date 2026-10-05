// @implements SPEC-VANMAC-BROADCAST
/** Build one authenticated SRT router; clients cannot publish another input or the program. */
export function relayConfiguration(host, port, secret) {
  const paths = Object.fromEntries(['input1', 'input2', 'input3', 'input4', 'program'].map(path => [path, {
    source: 'publisher', overridePublisher: false, maxReaders: path === 'program' ? 4 : 1,
    srtPublishPassphrase: secret(), srtReadPassphrase: secret(),
  }]));
  const parent = { user: 'parent', pass: secret(), ips: [host], permissions: [
    ...[1, 2, 3, 4].map(i => ({ action: 'read', path: `input${i}` })),
    { action: 'publish', path: 'program' },
  ] };
  const clients = [1, 2, 3, 4].map(i => ({ user: `sender${i}`, pass: secret(), ips: [], permissions: [
    { action: 'publish', path: `input${i}` }, { action: 'read', path: 'program' },
  ] }));
  const url = (action, path, account) => `srt://${host}:${port}?mode=caller&latency=300000&pkt_size=1316`
    + `&streamid=${action}:${path}:${account.user}:${account.pass}`
    + `&passphrase=${paths[path][action === 'publish' ? 'srtPublishPassphrase' : 'srtReadPassphrase']}&pbkeylen=32`;
  return {
    configuration: {
      logLevel: 'warn', logDestinations: ['stdout'],
      api: false, metrics: false, pprof: false, playback: false,
      rtsp: false, rtmp: false, hls: false, webrtc: false, moq: false,
      srt: true, srtAddress: `${host}:${port}`, authMethod: 'internal',
      authInternalUsers: [parent, ...clients], paths,
    },
    parentInputs: [1, 2, 3, 4].map(i => url('read', `input${i}`, parent)),
    parentOutput: url('publish', 'program', parent),
    clients: clients.map((account, index) => ({ input: index + 1,
      sendUrl: url('publish', `input${index + 1}`, account), returnUrl: url('read', 'program', account) })),
  };
}
