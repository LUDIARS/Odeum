export default {
  post: (_result: void, ticket: { role: string; slot?: string }) => ['presenter', 'viewer', 'overlay', 'producer'].includes(ticket.role),
  postThrow: (error: Error & { code?: string }) =>
    ['presenter_exists', 'producer_exists', 'slot_unavailable', 'capacity', 'forbidden', 'invite_conflict'].includes(error.code ?? ''),
};
