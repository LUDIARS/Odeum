export default {
  post: (tag: { body: number[] }, sets: { sps: number[]; pps: number[] }) =>
    tag.body[0] === 0x17 && tag.body[1] === 0 && tag.body[5] === 1 && tag.body[9] === 0xff && tag.body[10] === 0xe1 &&
    tag.body[6] === sets.sps[1],
  postThrow: (error: Error) => error instanceof Error,
};
