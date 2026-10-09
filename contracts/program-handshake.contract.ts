export default {
  post: (consumed: number) => Number.isInteger(consumed) && consumed >= 0 && consumed <= 1 + 2 * 1536,
  postThrow: (error: { code?: string }) => error.code === 'unsupported_version',
};
