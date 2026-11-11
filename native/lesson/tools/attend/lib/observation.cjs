// Turns the raw screen reading of the attendee's seat into the `observe` shape, and keeps what
// the attendee has already been given (private messages) and what it has done (answers acked).

function createObservation(activities) {
  let returnedSeq = 0;
  const acked = new Map();     // activity id -> true once the page said 受け取りました / 回答済み
  const answers = new Map();   // activity id -> what this harness submitted

  const activityId = (prompt) => activities.get(prompt) ?? null;

  function shape(raw, { all = false, consume = true } = {}) {
    const out = {
      lesson_state: raw.lesson_state,
      connection: raw.connection,
      lesson_title: raw.lesson_title,
      section: raw.section,
      slide: raw.slide,
      narration: raw.narration,
      demo: raw.demo,
      activity: null,
      assignment: raw.assignment,
      private_new: raw.private.filter((item) => item.seq > returnedSeq),
      private_count: raw.private.length,
      shared_questions: raw.shared,
      conduct: raw.conduct,
      said: raw.said,
      toast: raw.toast || null,
      private_notice: raw.private_notice,
      tool: raw.tool,
    };
    if (raw.entry_status) out.entry_status = raw.entry_status;
    if (raw.activity) {
      const plan = activityId(raw.activity.prompt);
      const id = plan?.id ?? null;
      if (id && /受け取りました|回答済み/.test(raw.activity.state_note)) acked.set(id, true);
      out.activity = {
        id, kind: plan?.kind ?? null, ...raw.activity,
        my_answer: id ? answers.get(id) ?? null : null,
        my_answer_acked: id ? acked.get(id) === true : false,
      };
    }
    if (all) out.private_all = raw.private;
    if (consume && out.private_new.length) returnedSeq = Math.max(...out.private_new.map((item) => item.seq));
    return out;
  }

  // The part of the screen whose change wakes `wait --for change`. Typing and toasts are left
  // out, so the attendee's own keystrokes do not count as news.
  function digest(raw) {
    const activity = raw.activity && { ...raw.activity, text_value: undefined,
      choices: raw.activity.choices?.map(({ index, label }) => ({ index, label })) };
    return JSON.stringify({
      lesson_state: raw.lesson_state, section: raw.section, slide: raw.slide, narration: raw.narration, demo: raw.demo,
      activity, assignment: raw.assignment, private: raw.private.map((item) => item.seq),
      shared: raw.shared.map((item) => [item.n, item.votes]), conduct: raw.conduct.standing, said: raw.said,
    });
  }

  return {
    shape, digest, activityId,
    recordAnswer(id, value) { if (id) answers.set(id, value); },
    recordAck(id) { if (id) acked.set(id, true); },
    ackedIds: () => [...acked.keys()],
  };
}

module.exports = { createObservation };
