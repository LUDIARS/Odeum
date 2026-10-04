export default {
  post: (state: { goodTotal: number; comments: unknown[]; commentLimit: number; stamps: { expiresAt: number }[]; now: number }) =>
    state.goodTotal >= 0 && state.comments.length <= state.commentLimit && state.stamps.every(stamp => stamp.expiresAt > state.now),
  postThrow: (error: Error) => error instanceof Error,
};
