export default {
  post: (message: { type: string; question: string; choices: string[]; multi: boolean }) =>
    message.type === "poll.open" && message.question.length > 0 && message.choices.length >= 2 && message.choices.length <= 6 &&
    message.choices.every(choice => choice.length > 0 && [...choice].length <= 60) && typeof message.multi === "boolean",
  postThrow: (error: Error) => error instanceof Error,
};
