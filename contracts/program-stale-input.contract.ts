export default {
  post: (picture: { timestamp_us: number } | null, nowUs: number, maxAgeMs: number) =>
    picture === null || nowUs - picture.timestamp_us <= maxAgeMs * 1000,
};
