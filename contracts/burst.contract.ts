export default {
  post: (result: null | { good: number; stamps: Record<string, number> }) =>
    result === null || result.good + Object.values(result.stamps).reduce((a, b) => a + b, 0) > 0,
};
