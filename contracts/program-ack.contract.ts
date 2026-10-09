export default {
  post: (sequence: number | null) => sequence === null || (Number.isInteger(sequence) && sequence >= 0 && sequence <= 0xffffffff),
};
