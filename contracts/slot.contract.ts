export default {
  post: (_result: void, slot: string, maxInputs: number) =>
    slot === 'program' || (/^input[1-8]$/.test(slot) && Number(slot.slice(5)) <= maxInputs),
  postThrow: (error: Error) => error instanceof Error,
};
