import { isAbsolute } from 'node:path';
export const bundle = Object.freeze({ service: 'odeum-relay', version: 1, persistentData: false, data: {} });

export function argumentsFor(names) {
  const args = process.argv.slice(2);
  if (args.length !== names.length * 2) throw new Error('Invalid arguments');
  const result = {};
  for (let i = 0; i < args.length; i += 2) {
    if (!names.includes(args[i]) || result[args[i]] !== undefined || !args[i + 1]) throw new Error('Invalid arguments');
    result[args[i]] = args[i + 1];
  }
  for (const key of ['--input', '--output']) if (result[key] && !isAbsolute(result[key])) throw new Error('Absolute path required');
  return result;
}

export function validateBundle(value) {
  if (!value || value.service !== bundle.service || value.version !== 1 || value.persistentData !== false ||
      !value.data || Array.isArray(value.data) || typeof value.data !== 'object' || Object.keys(value.data).length !== 0 ||
      Object.keys(value).sort().join(',') !== 'data,persistentData,service,version') throw new Error('Invalid empty data bundle');
}
