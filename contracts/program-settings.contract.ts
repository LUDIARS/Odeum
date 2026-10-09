export default {
  post: (_result: void, s: { video_bitrate_kbps: number; keyframe_interval_s: number; b_frames: number; volume: number }) =>
    s.video_bitrate_kbps >= 6000 && s.video_bitrate_kbps <= 10000 && s.keyframe_interval_s >= 1 && s.keyframe_interval_s <= 4 &&
    s.b_frames >= 0 && s.b_frames <= 2 && s.volume >= 0 && s.volume <= 2,
  postThrow: (error: Error) => error instanceof Error,
};
