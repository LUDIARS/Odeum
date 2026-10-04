export default {
  post: (result: { sub: string; role: string; exp: number }, _ticket: string, now: number) =>
    result.sub.length > 0 && ['presenter', 'viewer', 'service'].includes(result.role) && result.exp > now && result.exp <= now + 300,
  postThrow: (error: Error) => error instanceof Error,
};
