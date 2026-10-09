export default {
  post: (samples: number[]) => samples.length === 1920 && samples.every(s => s >= -1 && s <= 1),
};
