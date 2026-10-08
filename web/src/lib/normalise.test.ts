import { describe, expect, it } from 'vitest';
import { normalise, normaliseLibrary } from './normalise';
import type { Config, Library } from './types';

const config: Config = {
  version: 1,
  slots: [{ index: 0, ingredient: 'gin', empty: false }],
  pumpCount: 2,
  pour: { concurrency: 1, pauseWindowSeconds: 30, watchdogFactor: 1.5 },
  cupSensor: { enabled: true, emptyMm: 95, cupMm: 45, debounceSamples: 3 },
  wifi: { ssid: 'Bartender-A3F2', passphrase: 'change-me' },
  pin: '1234',
  ring: { pixels: 16, brightness: 200 }
};

describe('normalise', () => {
  it('leaves the LED ring alone — it is not the screen', () => {
    expect(normalise(config).ring.brightness).toBe(200);
  });

  it('drops the retired per-channel calibration array', () => {
    const older = { ...config, calibration: [{ channel: 0, mlPerSecond: 9 }] } as Config;
    expect(normalise(older)).not.toHaveProperty('calibration');
  });

  it('resizes slots to the pump count', () => {
    expect(normalise(config).slots.map((slot) => slot.index)).toEqual([0, 1]);
  });
});

describe('normaliseLibrary', () => {
  const library: Library = {
    version: 1,
    ingredients: [
      { id: 'gin', name: 'Gin', colour: '#94C7C1', mlPerSecond: 5.5 },
      { id: 'syrup', name: 'Syrup', colour: '#B5651D' } as Library['ingredients'][number]
    ],
    recipes: []
  };

  it('gives an ingredient with no stored rate the default', () => {
    expect(normaliseLibrary(library).ingredients[1].mlPerSecond).toBe(4.2);
  });

  it('keeps a rate the bottle already carries', () => {
    expect(normaliseLibrary(library).ingredients[0].mlPerSecond).toBe(5.5);
  });
});
