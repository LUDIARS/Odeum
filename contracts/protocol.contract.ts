export default {
  post: (result: { type: number }) => Number.isInteger(result.type) && result.type >= 0 && result.type <= 13,
  postThrow: (error: Error) => error instanceof Error,
};
