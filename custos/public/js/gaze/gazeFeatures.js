// Adapted from Voluptas private tracking (9d0a05d); owned source, no sibling runtime dependency.
// Gaze feature extraction from MediaPipe Face Landmarker output
// (spec/feature/emotion-capture-companion.md §視線推定). Pure functions over
// the 478-point landmark array so the estimator can be unit-tested with
// synthetic faces; the MediaPipe call itself lives in faceLandmarkerAdapter.js.
//
// Feature vector (all dimensionless):
//   irisX  — mean horizontal iris position within the eye opening in image
//            space, 0 = image-left corner, 1 = image-right corner (the same
//            direction for both eyes, so averaging does not cancel out)
//   irisY  — mean vertical iris position within the eye opening, 0 = upper lid,
//            1 = lower lid
//   yaw    — head yaw proxy from nose-to-cheek asymmetry (-1..1)
//   pitch  — head pitch proxy from nose height between forehead and chin (-1..1)
// The affine calibration (gazeCalibration.js) maps [irisX, irisY, yaw, pitch, 1]
// to normalized screen coordinates.

// MediaPipe Face Landmarker indices (canonical face mesh + iris refinement).
export const LANDMARKS = Object.freeze({
  rightEyeOuter: 33,
  rightEyeInner: 133,
  rightEyeUpper: 159,
  rightEyeLower: 145,
  rightIrisCenter: 468,
  leftEyeInner: 362,
  leftEyeOuter: 263,
  leftEyeUpper: 386,
  leftEyeLower: 374,
  leftIrisCenter: 473,
  noseTip: 1,
  forehead: 10,
  chin: 152,
  rightCheek: 234,
  leftCheek: 454,
});

export const FEATURE_COUNT = 5; // irisX, irisY, yaw, pitch, bias

// Below this eye-opening height (relative to eye width) the eye is closed or
// blinking and iris position is meaningless.
const MINIMUM_OPEN_RATIO = 0.12;

function ratioAlong(value, from, to) {
  const span = to - from;
  if (Math.abs(span) < 1e-6) return null;
  return (value - from) / span;
}

// leftCorner / rightCorner are in image space (smaller x / larger x), not the
// anatomical inner/outer naming, so both eyes produce irisX in one direction.
function eyeFeatures(landmarks, { leftCorner, rightCorner, upper, lower, iris }) {
  const leftPoint = landmarks[leftCorner];
  const rightPoint = landmarks[rightCorner];
  const upperPoint = landmarks[upper];
  const lowerPoint = landmarks[lower];
  const irisPoint = landmarks[iris];
  if (!leftPoint || !rightPoint || !upperPoint || !lowerPoint || !irisPoint) return null;
  const width = Math.hypot(rightPoint.x - leftPoint.x, rightPoint.y - leftPoint.y);
  const height = Math.hypot(lowerPoint.x - upperPoint.x, lowerPoint.y - upperPoint.y);
  if (width < 1e-6 || height / width < MINIMUM_OPEN_RATIO) return null;
  const irisX = ratioAlong(irisPoint.x, leftPoint.x, rightPoint.x);
  const irisY = ratioAlong(irisPoint.y, upperPoint.y, lowerPoint.y);
  if (irisX === null || irisY === null) return null;
  return { irisX, irisY };
}

/** @implements SPEC-ODEUM-PLAY-GAZE */
export function headPoseProxy(landmarks) {
  const nose = landmarks[LANDMARKS.noseTip];
  const rightCheek = landmarks[LANDMARKS.rightCheek];
  const leftCheek = landmarks[LANDMARKS.leftCheek];
  const forehead = landmarks[LANDMARKS.forehead];
  const chin = landmarks[LANDMARKS.chin];
  if (!nose || !rightCheek || !leftCheek || !forehead || !chin) return null;
  const faceWidth = leftCheek.x - rightCheek.x;
  const faceHeight = chin.y - forehead.y;
  if (Math.abs(faceWidth) < 1e-6 || Math.abs(faceHeight) < 1e-6) return null;
  // Centered nose → 0; nose towards a cheek → ±1 (turned head).
  const yaw = ((nose.x - rightCheek.x) / faceWidth) * 2 - 1;
  // Nose sits ~60% down a frontal face; deviations mean pitch.
  const pitch = ((nose.y - forehead.y) / faceHeight - 0.6) * 2;
  return { yaw, pitch };
}

// Returns the feature vector for one frame, or null when the eyes are closed /
// the face is not usable. Landmarks are {x, y} in normalized image space. The
// subject's right eye sits on the image left, so its outer corner (33) is the
// image-left corner while for the left eye the inner corner (362) is.
/** @implements SPEC-ODEUM-PLAY-GAZE */
export function extractGazeFeatures(landmarks) {
  if (!Array.isArray(landmarks) || landmarks.length < 478) return null;
  const right = eyeFeatures(landmarks, {
    leftCorner: LANDMARKS.rightEyeOuter,
    rightCorner: LANDMARKS.rightEyeInner,
    upper: LANDMARKS.rightEyeUpper,
    lower: LANDMARKS.rightEyeLower,
    iris: LANDMARKS.rightIrisCenter,
  });
  const left = eyeFeatures(landmarks, {
    leftCorner: LANDMARKS.leftEyeInner,
    rightCorner: LANDMARKS.leftEyeOuter,
    upper: LANDMARKS.leftEyeUpper,
    lower: LANDMARKS.leftEyeLower,
    iris: LANDMARKS.leftIrisCenter,
  });
  const eyes = [right, left].filter(Boolean);
  if (eyes.length === 0) return null;
  const pose = headPoseProxy(landmarks) || { yaw: 0, pitch: 0 };
  const irisX = eyes.reduce((sum, eye) => sum + eye.irisX, 0) / eyes.length;
  const irisY = eyes.reduce((sum, eye) => sum + eye.irisY, 0) / eyes.length;
  return [irisX, irisY, pose.yaw, pose.pitch, 1];
}
