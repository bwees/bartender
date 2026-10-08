import { describe, expect, it } from 'vitest';
import { servability, slotsFor, totalMl } from './servability';
import type { Config, Ingredient, Recipe, Slot } from './types';

const gin: Ingredient = { id: 'gin', name: 'Gin', colour: '#94C7C1', abv: 40, mlPerSecond: 4.2 };
const campari: Ingredient = {
  id: 'campari',
  name: 'Campari',
  colour: '#C0392B',
  abv: 25,
  mlPerSecond: 3.1
};
const ingredients = [gin, campari];

const negroni: Recipe = {
  id: 'negroni',
  name: 'Negroni',
  pours: [
    { ingredient: 'gin', ml: 30 },
    { ingredient: 'campari', ml: 30 }
  ]
};

function slot(index: number, ingredient: string | null, empty = false): Slot {
  return { index, ingredient, empty };
}

describe('servability', () => {
  it('is servable when every pour sits in a slot that is not empty', () => {
    const state = servability(negroni, ingredients, [slot(0, 'gin'), slot(1, 'campari')]);
    expect(state).toEqual({ servable: true, reasons: [] });
  });

  it('names the ingredient that is in no slot', () => {
    const state = servability(negroni, ingredients, [slot(0, 'gin')]);
    expect(state.servable).toBe(false);
    expect(state.reasons).toEqual(['Needs Campari — not in any slot']);
  });

  it('distinguishes a loaded but empty slot from a missing ingredient', () => {
    const state = servability(negroni, ingredients, [slot(0, 'gin'), slot(1, 'campari', true)]);
    expect(state.reasons).toEqual(['Campari is marked empty in slot 1']);
  });

  it('lists every slot holding an ingredient that has run out', () => {
    const state = servability(negroni, ingredients, [
      slot(0, 'gin'),
      slot(1, 'campari', true),
      slot(2, 'campari', true)
    ]);
    expect(state.reasons).toEqual(['Campari is marked empty in slot 1, slot 2']);
  });

  it('is servable when one of several slots holding the ingredient is still full', () => {
    const state = servability(negroni, ingredients, [
      slot(0, 'gin'),
      slot(1, 'campari', true),
      slot(2, 'campari')
    ]);
    expect(state.servable).toBe(true);
  });

  it('reports an ingredient that is not in the catalogue', () => {
    const orphan: Recipe = { id: 'x', name: 'X', pours: [{ ingredient: 'deadbeef-1', ml: 10 }] };
    expect(servability(orphan, ingredients, []).reasons).toEqual([
      'Unknown ingredient deadbeef — it is not in the catalogue'
    ]);
  });

  it('reports each missing ingredient once even when it is poured twice', () => {
    const doubled: Recipe = {
      id: 'y',
      name: 'Y',
      pours: [
        { ingredient: 'gin', ml: 10 },
        { ingredient: 'gin', ml: 20 }
      ]
    };
    expect(servability(doubled, ingredients, []).reasons).toHaveLength(1);
  });

  it('matches the firmware and calls a recipe with no pours servable', () => {
    expect(servability({ id: 'z', name: 'Z', pours: [] }, ingredients, []).servable).toBe(true);
  });
});

describe('totalMl', () => {
  it('sums the pours', () => {
    expect(totalMl(negroni)).toBe(60);
  });
});

describe('slotsFor', () => {
  const config = {
    version: 1,
    slots: [slot(0, 'gin'), slot(2, 'campari', true)],
    pumpCount: 4
  } as Config;

  it('produces exactly pumpCount slots indexed from zero', () => {
    expect(slotsFor(config).map((entry) => entry.index)).toEqual([0, 1, 2, 3]);
  });

  it('keeps what the stored document said and blanks the rest', () => {
    expect(slotsFor(config)).toEqual([
      slot(0, 'gin'),
      slot(1, null),
      slot(2, 'campari', true),
      slot(3, null)
    ]);
  });

  it('drops slots beyond the pump count', () => {
    expect(slotsFor({ ...config, pumpCount: 1 })).toEqual([slot(0, 'gin')]);
  });
});
