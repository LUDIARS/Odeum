export default {
  post: (delayMs: number) => Number.isInteger(delayMs) && delayMs >= 500 && delayMs <= 30000,
};
