export default {
  post: (result: { role: string }) => result.role === 'service',
  postThrow: (error: Error) => error instanceof Error,
};
