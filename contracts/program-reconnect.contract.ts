export default {
  post: (delayMs: number) => delayMs === 0 || (Number.isInteger(delayMs) && delayMs >= 1000 && delayMs <= 30000),
};
