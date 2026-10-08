import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';
import { SLOT_HUES, effectiveHue, slotOf } from './hues';
import type { Slot } from './types';

const slots: Slot[] = [
  { index: 0, ingredient: 'gin', empty: false },
  { index: 1, ingredient: 'campari', empty: false }
];

describe('hues', () => {
  it('matches the firmware slot hue table exactly', () => {
    const theme = readFileSync('../firmware/lib/hmi/theme.h', 'utf8');
    const table = theme.match(/kSlotHues\[8\]\s*=\s*\{([^}]*)\}/s);
    expect(table).not.toBeNull();
    const firmware = [...table![1].matchAll(/0x([0-9A-Fa-f]{6})/g)].map(
      (m) => `#${m[1].toUpperCase()}`
    );
    expect(firmware).toEqual([...SLOT_HUES]);
  });

  it('gives a loaded ingredient its slot hue, not its catalogue colour', () => {
    expect(effectiveHue('#94C7C1', slotOf(slots, 'gin'))).toBe(SLOT_HUES[0]);
    expect(effectiveHue('#C0392B', slotOf(slots, 'campari'))).toBe(SLOT_HUES[1]);
  });

  it('falls back to the catalogue colour when the ingredient is in no slot', () => {
    expect(effectiveHue('#8E3B46', slotOf(slots, 'vermouth'))).toBe('#8E3B46');
  });
});
