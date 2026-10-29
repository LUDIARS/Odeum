// Affine numeric gaze calibration adapted from Voluptas (9d0a05d).
// Fits screen targets from eye/head features. No frames, I/O or heuristic mapping.
import { FEATURE_COUNT } from './gazeFeatures.js';

// Ridge term keeps the normal equations solvable when a calibration only spans
// a line of targets (e.g. all points on one row) or the head never moved.
const RIDGE = 1e-3;

/** @implements SPEC-ODEUM-PLAY-GAZE */
export function solveLinearSystem(matrix, vector) {
  const n = vector.length;
  const a = matrix.map((row, index) => [...row, vector[index]]);
  for (let column = 0; column < n; column += 1) {
    let pivot = column;
    for (let row = column + 1; row < n; row += 1) {
      if (Math.abs(a[row][column]) > Math.abs(a[pivot][column])) pivot = row;
    }
    if (Math.abs(a[pivot][column]) < 1e-12) return null;
    [a[column], a[pivot]] = [a[pivot], a[column]];
    for (let row = 0; row < n; row += 1) {
      if (row === column) continue;
      const factor = a[row][column] / a[column][column];
      for (let k = column; k <= n; k += 1) a[row][k] -= factor * a[column][k];
    }
  }
  return a.map((row, index) => row[n] / row[index]);
}

// Solves w = argmin Σ (features·w − target)² + ridge‖w‖² per output axis.
/** @implements SPEC-ODEUM-PLAY-GAZE */
export function fitAffineMapping(samples, { ridge = RIDGE } = {}) {
  const usable = samples.filter((sample) => Array.isArray(sample.features)
    && sample.features.length === FEATURE_COUNT
    && sample.features.every(Number.isFinite));
  if (usable.length < FEATURE_COUNT) return null;
  const normal = Array.from({ length: FEATURE_COUNT }, () => Array(FEATURE_COUNT).fill(0));
  const rhsX = Array(FEATURE_COUNT).fill(0);
  const rhsY = Array(FEATURE_COUNT).fill(0);
  for (const { features, x, y } of usable) {
    for (let i = 0; i < FEATURE_COUNT; i += 1) {
      rhsX[i] += features[i] * x;
      rhsY[i] += features[i] * y;
      for (let j = 0; j < FEATURE_COUNT; j += 1) normal[i][j] += features[i] * features[j];
    }
  }
  // Do not regularize the bias term; only the slopes.
  for (let i = 0; i < FEATURE_COUNT - 1; i += 1) normal[i][i] += ridge * usable.length;
  const weightsX = solveLinearSystem(normal, rhsX);
  const weightsY = solveLinearSystem(normal, rhsY);
  if (!weightsX || !weightsY) return null;
  const mapping = { weightsX, weightsY };
  const errors = usable.map((sample) => {
    const predicted = applyMapping(mapping, sample.features);
    return Math.hypot(predicted.x - sample.x, predicted.y - sample.y);
  });
  const fitError = errors.reduce((sum, value) => sum + value, 0) / errors.length;
  return { ...mapping, fitError, sampleCount: usable.length };
}

/** @implements SPEC-ODEUM-PLAY-GAZE */
export function applyMapping(mapping, features) {
  let x = 0;
  let y = 0;
  for (let i = 0; i < FEATURE_COUNT; i += 1) {
    x += mapping.weightsX[i] * features[i];
    y += mapping.weightsY[i] * features[i];
  }
  return { x, y };
}
