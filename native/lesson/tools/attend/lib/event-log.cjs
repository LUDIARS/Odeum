// JSONL log of the session: what the attendee's seat showed, what it did, what the operator script
// and the classmates did. `v` is seconds into the composed video (its origin is the first recorded
// page), so a line can be found in the video directly. Test artefact only: it lives in the work dir.
const fs = require('fs');

function createEventLog(file) {
  const fd = fs.openSync(file, 'w');
  let origin = Date.now();
  return {
    setOrigin(ms) { origin = ms; },
    origin: () => origin,
    seconds: () => Math.round((Date.now() - origin) / 100) / 10,
    write(kind, body) {
      const line = { t: new Date().toISOString(), v: Math.round((Date.now() - origin) / 100) / 10, kind, ...body };
      fs.writeSync(fd, `${JSON.stringify(line)}\n`);
      return line;
    },
    close() { fs.closeSync(fd); },
  };
}

module.exports = { createEventLog };
