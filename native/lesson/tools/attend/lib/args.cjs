// Command-line parsing shared by the session runner and the seat client.
//   positional words, `--key value`, `--key=value`, and bare `--flag` (true).
// Unknown options are rejected by the caller through `allow`, so a typo never silently does nothing.
function parseArgs(argv, { flags = [], allow = [] } = {}) {
  const positional = [];
  const options = {};
  for (let i = 0; i < argv.length; i += 1) {
    const word = argv[i];
    if (!word.startsWith('--')) { positional.push(word); continue; }
    const eq = word.indexOf('=');
    const key = eq > 0 ? word.slice(2, eq) : word.slice(2);
    if (!flags.includes(key) && !allow.includes(key)) throw new Error(`unknown option --${key}`);
    if (flags.includes(key)) { options[key] = true; continue; }
    const value = eq > 0 ? word.slice(eq + 1) : argv[++i];
    if (value === undefined) throw new Error(`option --${key} needs a value`);
    options[key] = value;
  }
  return { positional, options };
}

function numberOption(options, key, fallback, { min = 0 } = {}) {
  if (options[key] === undefined) return fallback;
  const value = Number(options[key]);
  if (!Number.isFinite(value) || value < min) throw new Error(`--${key} must be a number >= ${min} (got ${options[key]})`);
  return value;
}

module.exports = { parseArgs, numberOption };
