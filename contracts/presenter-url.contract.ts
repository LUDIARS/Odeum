export default {
  post: (result: { relay: string; ticket: string }) =>
    result.relay.startsWith("wss://") && result.ticket.split(".").length === 3 && result.ticket.split(".").every(part => part.length > 0),
  postThrow: (error: Error) => error instanceof Error,
};
