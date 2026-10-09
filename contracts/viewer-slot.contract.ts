export default {
  post: (result: string | null, live: string[]) => {
    if (live.includes('program')) return result === 'program';
    const inputs = live.filter((slot) => /^input[1-8]$/.test(slot)).sort((a, b) => Number(a.slice(5)) - Number(b.slice(5)));
    return result === (inputs[0] ?? null);
  },
};
