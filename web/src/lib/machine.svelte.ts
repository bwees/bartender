import * as api from './api';
import { ApiError, describe } from './api';
import { normalise, normaliseLibrary } from './normalise';
import { slotsFor } from './servability';
import type { Config, Ingredient, Library, Recipe, Uuid } from './types';

export class Machine {
  library = $state<Library | null>(null);
  config = $state<Config | null>(null);
  loading = $state(false);
  loadError = $state<string | null>(null);
  saving = $state(false);
  error = $state<string | null>(null);
  pouring = $state(false);

  private queue: Promise<unknown> = Promise.resolve();

  get ready(): boolean {
    return this.library !== null && this.config !== null;
  }

  get ingredients() {
    return this.library?.ingredients ?? [];
  }

  get recipes() {
    return this.library?.recipes ?? [];
  }

  get slots() {
    return this.config ? slotsFor(this.config) : [];
  }

  async load(): Promise<void> {
    this.loading = true;
    this.loadError = null;
    try {
      const [library, config] = await Promise.all([api.getLibrary(), api.getConfig()]);
      this.library = normaliseLibrary(library);
      this.config = normalise(config);
    } catch (error) {
      this.loadError = describe(error);
    } finally {
      this.loading = false;
    }
  }

  updateConfig(mutate: (draft: Config) => void): Promise<boolean> {
    return this.enqueue(async () => {
      const draft = $state.snapshot(this.config);
      if (!draft) throw new Error('The configuration has not loaded yet.');
      mutate(draft);
      this.config = await api.putConfig(normalise(draft));
    });
  }

  saveRecipe(recipe: Recipe, artwork?: Blob | null): Promise<boolean> {
    return this.enqueue(async () => {
      const next = { ...recipe };
      const previous = this.library?.recipes.find((candidate) => candidate.id === next.id);
      if (artwork) {
        await api.putAsset(next.id, artwork);
        next.image = `/assets/${next.id}.jpeg`;
      } else if (previous?.image && !next.image) {
        await api.deleteAsset(next.id);
      }
      const draft = this.libraryDraft();
      const at = draft.recipes.findIndex((candidate) => candidate.id === next.id);
      if (at >= 0) draft.recipes[at] = next;
      else draft.recipes.push(next);
      this.library = await api.putLibrary(draft);
    });
  }

  removeRecipe(id: Uuid): Promise<boolean> {
    return this.enqueue(async () => {
      const draft = this.libraryDraft();
      if (draft.recipes.find((recipe) => recipe.id === id)?.image) await api.deleteAsset(id);
      draft.recipes = draft.recipes.filter((recipe) => recipe.id !== id);
      this.library = await api.putLibrary(draft);
    });
  }

  saveIngredient(ingredient: Ingredient): Promise<boolean> {
    return this.enqueue(async () => {
      const draft = this.libraryDraft();
      const at = draft.ingredients.findIndex((candidate) => candidate.id === ingredient.id);
      if (at >= 0) draft.ingredients[at] = { ...ingredient };
      else draft.ingredients.push({ ...ingredient });
      this.library = await api.putLibrary(draft);
    });
  }

  removeIngredient(id: Uuid): Promise<boolean> {
    return this.enqueue(async () => {
      const draft = this.libraryDraft();
      draft.ingredients = draft.ingredients.filter((ingredient) => ingredient.id !== id);
      draft.recipes = draft.recipes.map((recipe) => ({
        ...recipe,
        pours: recipe.pours.filter((pour) => pour.ingredient !== id)
      }));
      this.library = await api.putLibrary(draft);

      const config = $state.snapshot(this.config);
      if (config && config.slots.some((slot) => slot.ingredient === id)) {
        config.slots = config.slots.map((slot) =>
          slot.ingredient === id ? { ...slot, ingredient: null } : slot
        );
        this.config = await api.putConfig(normalise(config));
      }
    });
  }

  private libraryDraft(): Library {
    const draft = $state.snapshot(this.library);
    if (!draft) throw new Error('The library has not loaded yet.');
    return draft;
  }

  private enqueue(work: () => Promise<void>): Promise<boolean> {
    const run = this.queue.then(async () => {
      this.saving = true;
      this.error = null;
      this.pouring = false;
      try {
        await work();
        return true;
      } catch (error) {
        this.error = describe(error);
        this.pouring = error instanceof ApiError && error.pouring;
        return false;
      } finally {
        this.saving = false;
      }
    });
    this.queue = run.catch(() => undefined);
    return run;
  }
}

export const machine = new Machine();
