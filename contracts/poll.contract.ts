export default {
  post: (result: { counts: number[]; answered: number }) =>
    result.answered >= 0 && result.counts.length >= 2 && result.counts.length <= 6 && result.counts.every(n => n >= 0 && n <= result.answered),
  postThrow: (error: Error) => error instanceof Error,
};
