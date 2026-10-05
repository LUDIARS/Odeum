// @implements SPEC-VANMAC-BROADCAST
/** Adapt the shared program layout to macOS and connect its four shared sources. */
export function macSceneCollection(template, inputUrls, uuid) {
  const result = structuredClone(template);
  result.name = 'Odeum_VANMAC';
  for (const source of result.sources) {
    if (source.id !== 'text_gdiplus') continue;
    source.id = 'text_ft2_source'; source.versioned_id = 'text_ft2_source_v2';
    source.settings = { text: source.settings.text, font: { face: 'Hiragino Sans', size: 60, style: 'Regular', flags: 0 },
      color1: 0xffeeeeee, color2: 0xffeeeeee, outline: false, drop_shadow: false,
      custom_width: 1920, word_wrap: true };
  }
  inputUrls.forEach((url, index) => {
    const input = index + 1;
    const scene = result.sources.find(s => s.name === `SOURCE ${input}`);
    if (!scene || scene.settings.items.length !== 2) throw new Error('Unexpected source scene template');
    const media = { name: `SRT input ${input}`, uuid: uuid(), id: 'ffmpeg_source', versioned_id: 'ffmpeg_source',
      settings: { is_local_file: false, input: url, input_format: 'mpegts', close_when_inactive: false,
        restart_on_activate: false, clear_on_media_end: true, reconnect_delay_sec: 2, buffering_mb: 2 },
      mixers: 1, sync: 0, flags: 0, volume: 1, balance: 0.5, enabled: true, muted: false,
      hotkeys: {}, monitoring_type: 0, private_settings: {} };
    result.sources.push(media);
    // Keep the slate underneath. The media source clears when disconnected.
    const item = structuredClone(scene.settings.items[1]);
    item.name = media.name; item.source_uuid = media.uuid; item.id = 3;
    scene.settings.items.push(item); scene.settings.id_counter = 4;
  });
  return result;
}

/** OBS recording output carries the complete program back to the SRT router. */
export function macProfile(base, programUrl) {
  return base.replace('Name=Odeum YouTube 1080p30', 'Name=Odeum_VANMAC')
    .replace('Encoder=obs_nvenc_h264_tex', 'Encoder=obs_x264')
    .replace('RecType=Standard', 'RecType=FFmpeg') + `
FFOutputToFile=false
FFURL=${programUrl}
FFFormat=mpegts
FFFormatMimeType=video/MP2T
FFMCustom=muxdelay=0
FFVEncoder=libx264
FFVEncoderId=27
FFVBitrate=1000
FFVGOPSize=30
FFVCustom=preset=ultrafast tune=zerolatency bf=0
FFRescale=true
FFRescaleRes=640x360
FFAEncoder=aac
FFAEncoderId=86018
FFABitrate=128
FFAudioMixes=1
`;
}

export const streamEncoder = { rate_control: 'CBR', bitrate: 10000, keyint_sec: 2,
  preset: 'veryfast', profile: 'high', tune: 'zerolatency', x264opts: 'bframes=0' };
