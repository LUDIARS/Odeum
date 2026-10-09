export default {
  post: (out: number[]) => Array.isArray(out),
  postThrow: (error: { code?: string; message?: string }) =>
    ['connect_rejected', 'publish_rejected', 'unsupported_version', 'invalid_chunk', 'timeout'].includes(error.code ?? ''),
};
