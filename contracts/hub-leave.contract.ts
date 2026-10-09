export default {
  post: (_result: void, _id: number, sid: string) => typeof sid === 'string' && sid.length > 0,
};
