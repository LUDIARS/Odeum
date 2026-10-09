export default {
  post: (_result: void, id: number) => Number.isInteger(id) && id > 0,
};
