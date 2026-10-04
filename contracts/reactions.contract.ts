export default {
  post: (result: { accepted: number; limited: boolean }, _message: unknown) =>
    Number.isInteger(result.accepted) && result.accepted >= 0 && result.accepted <= 30,
};
