import type { Config, Ingredient, Recipe, Slot, Uuid } from './types';

export interface Servability {
  servable: boolean;
  reasons: string[];
}

export function slotsFor(config: Config): Slot[] {
  const byIndex = new Map(config.slots.map((slot) => [slot.index, slot]));
  return Array.from({ length: Math.max(0, config.pumpCount) }, (_, index) => {
    const existing = byIndex.get(index);
    return { index, ingredient: existing?.ingredient ?? null, empty: existing?.empty ?? false };
  });
}

export function loadedIngredients(slots: Slot[]): Set<Uuid> {
  return new Set(
    slots.filter((slot) => !slot.empty && slot.ingredient).map((slot) => slot.ingredient as Uuid)
  );
}

export function totalMl(recipe: Recipe): number {
  return recipe.pours.reduce((sum, pour) => sum + (Number.isFinite(pour.ml) ? pour.ml : 0), 0);
}

export function servability(recipe: Recipe, ingredients: Ingredient[], slots: Slot[]): Servability {
  const names = new Map(ingredients.map((ingredient) => [ingredient.id, ingredient.name]));
  const reasons: string[] = [];

  for (const id of new Set(recipe.pours.map((pour) => pour.ingredient))) {
    const holders = slots.filter((slot) => slot.ingredient === id);
    if (holders.some((slot) => !slot.empty)) continue;

    const name = names.get(id);
    if (!name) {
      reasons.push(`Unknown ingredient ${id.slice(0, 8)} — it is not in the catalogue`);
    } else if (holders.length > 0) {
      const where = holders.map((slot) => `slot ${slot.index}`).join(', ');
      reasons.push(`${name} is marked empty in ${where}`);
    } else {
      reasons.push(`Needs ${name} — not in any slot`);
    }
  }

  return { servable: reasons.length === 0, reasons };
}
