export default {
  post: (result: { tiles: { live: boolean; area: { width: number; height: number } }[]; board: string }, scene: { mode: string }) =>
    (scene.mode === 'standby' || scene.mode === 'ended') ? result.tiles.length === 0
      : scene.mode === 'quad' ? result.tiles.length === 4 && result.tiles.every(t => t.area.width % 2 === 0 && t.area.height % 2 === 0)
      : result.tiles.length === 1,
  postThrow: (error: Error) => error instanceof Error,
};
