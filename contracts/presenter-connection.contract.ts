export default {
  post: (decision: { retry: boolean; ticketExpired: boolean; delayMs: number }) =>
    decision.ticketExpired ? !decision.retry : decision.retry && decision.delayMs >= 500 && decision.delayMs <= 30000,
};
