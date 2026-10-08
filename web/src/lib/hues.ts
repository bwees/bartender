import type { Slot, Uuid } from './types';

// Mirrors hmi::kSlotHues in firmware/lib/hmi/theme.h. The eight land exactly on
// RGB565 words and stay out of the touchscreen accent's blue lane, so a loaded
// bottle always takes its slot's hue on both the panel and the LED ring. Keep
// this list and that one in step.
export const SLOT_HUES = [
  '#B8A0F0',
  '#A8B8C8',
  '#C88C40',
  '#C8D058',
  '#78D048',
  '#F0C430',
  '#E04868',
  '#48C8C0'
] as const;

export function slotOf(slots: Slot[], ingredient: Uuid): number {
  return slots.findIndex((slot) => slot.ingredient === ingredient);
}

export function effectiveHue(catalogueColour: string, slotIndex: number): string {
  if (slotIndex >= 0) return SLOT_HUES[slotIndex % SLOT_HUES.length];
  return catalogueColour;
}
