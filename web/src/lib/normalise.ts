import type { Config, Library } from './types';
import { slotsFor } from './servability';

export const DEFAULT_ML_PER_SECOND = 4.2;

export function normalise(config: Config): Config {
  return {
    version: 1,
    slots: slotsFor(config),
    pumpCount: Math.max(0, Math.round(config.pumpCount)),
    pour: config.pour,
    cupSensor: config.cupSensor,
    wifi: config.wifi,
    pin: config.pin,
    ring: config.ring
  };
}

export function normaliseLibrary(library: Library): Library {
  return {
    ...library,
    version: 1,
    ingredients: library.ingredients.map((ingredient) => ({
      ...ingredient,
      mlPerSecond: ingredient.mlPerSecond ?? DEFAULT_ML_PER_SECOND
    }))
  };
}
