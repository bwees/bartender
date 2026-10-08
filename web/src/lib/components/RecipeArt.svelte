<script lang="ts">
  import type { Ingredient, Recipe, Slot } from '$lib/types';
  import { effectiveHue, slotOf } from '$lib/hues';

  interface Props {
    recipe: Recipe;
    ingredients: Ingredient[];
    slots?: Slot[];
    preview?: string | null;
    class?: string;
  }

  let { recipe, ingredients, slots = [], preview = null, class: className = '' }: Props = $props();

  let broken = $state(false);
  const src = $derived(preview ?? recipe.image ?? null);

  const colours = $derived.by(() => {
    const byId = new Map(
      ingredients.map((ingredient) => [
        ingredient.id,
        effectiveHue(ingredient.colour, slotOf(slots, ingredient.id))
      ])
    );
    const found = recipe.pours
      .map((pour) => byId.get(pour.ingredient))
      .filter((colour): colour is string => Boolean(colour));
    return found.length > 0 ? found : ['#3f3f46'];
  });

  const gradient = $derived(
    colours.length === 1
      ? `linear-gradient(150deg, ${colours[0]}, #18181b)`
      : `linear-gradient(150deg, ${colours.join(', ')})`
  );

  $effect(() => {
    src;
    broken = false;
  });
</script>

{#if src && !broken}
  <img
    {src}
    alt="Artwork for {recipe.name}"
    onerror={() => (broken = true)}
    class="bg-muted object-cover {className}"
  />
{:else}
  <div
    style="background-image: {gradient}"
    aria-hidden="true"
    class="flex items-center justify-center {className}"
  >
    <span class="text-2xl font-semibold text-white/80 drop-shadow">
      {recipe.name.trim().charAt(0).toUpperCase() || '?'}
    </span>
  </div>
{/if}
