export default {
  post: (_result: void, chunkStream: number) => chunkStream >= 2 && chunkStream <= 63,
  postThrow: (error: { code?: string }) => error.code === 'invalid_chunk',
};
