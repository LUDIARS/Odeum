// All executable/model assets are installed by setup:gaze and served by Odeum.
// The browser never fetches runtime code or sends camera input to third parties.
const ROOT = '/gaze-assets';
/** @implements SPEC-ODEUM-PLAY-GAZE */
export async function loadFaceLandmarker() {
    const response = await fetch(`${ROOT}/face_landmarker.task`, { method: 'HEAD' });
    if (!response.ok) throw new Error('視線追跡の準備がありません。管理者による設定が必要です。');
    const { FaceLandmarker, FilesetResolver } = await import(`${ROOT}/vision_bundle.mjs`);
    const files = await FilesetResolver.forVisionTasks(`${ROOT}/wasm`);
    const model = await FaceLandmarker.createFromOptions(files, {
        baseOptions: { modelAssetPath: `${ROOT}/face_landmarker.task`, delegate: 'CPU' },
        runningMode: 'VIDEO', numFaces: 1, outputFaceBlendshapes: true,
        outputFacialTransformationMatrixes: false,
    });
    return { detectTracking: (video, time) => model.detectForVideo(video, time), close: () => model.close() };
}
