<script lang="ts">
  import PlusIcon from '@lucide/svelte/icons/plus';
  import { Button } from '$lib/components/ui/button/index.js';
  import { Card } from '$lib/components/ui/card/index.js';
  import RecipeArt from '$lib/components/RecipeArt.svelte';
  import RecipeSheet from '$lib/components/RecipeSheet.svelte';
  import ServabilityNote from '$lib/components/ServabilityNote.svelte';
  import { machine } from '$lib/machine.svelte';
  import { servability, totalMl } from '$lib/servability';
  import type { Recipe } from '$lib/types';
  import { uuid } from '$lib/uuid';

  let editing = $state<Recipe | null>(null);
  let creating = $state(false);

  const rows = $derived(
    machine.recipes.map((recipe) => ({
      recipe,
      total: totalMl(recipe),
      pourable: servability(recipe, machine.ingredients, machine.slots)
    }))
  );
  const ready = $derived(rows.filter((row) => row.pourable.servable).length);

  function create() {
    creating = true;
    editing = { id: uuid(), name: '', pours: [] };
  }

  function open(recipe: Recipe) {
    creating = false;
    editing = recipe;
  }
</script>

<div class="mb-5 flex items-end justify-between gap-4">
  <div>
    <h1 class="text-2xl font-semibold tracking-tight">Recipes</h1>
    <p class="text-muted-foreground text-sm">
      {ready} of {rows.length} ready to pour right now
    </p>
  </div>
  <Button class="h-11" onclick={create}>
    <PlusIcon class="size-4" />
    New
  </Button>
</div>

{#if rows.length === 0}
  <Card class="p-8 text-center">
    <p class="text-muted-foreground text-sm">
      No drinks yet. Add ingredients to the catalogue, then build a drink out of pours.
    </p>
  </Card>
{:else}
  <ul class="space-y-3">
    {#each rows as row (row.recipe.id)}
      <li>
        <button
          type="button"
          class="hover:bg-accent/40 focus-visible:ring-ring bg-card flex w-full items-center gap-4 rounded-xl border p-3 text-left transition focus-visible:ring-2 focus-visible:outline-none"
          onclick={() => open(row.recipe)}
        >
          <RecipeArt
            recipe={row.recipe}
            ingredients={machine.ingredients}
            slots={machine.slots}
            class="size-16 shrink-0 rounded-lg border border-white/10"
          />
          <div class="min-w-0 flex-1 space-y-1.5">
            <div class="flex items-baseline justify-between gap-3">
              <span class="truncate text-base font-medium">{row.recipe.name}</span>
              <span class="text-muted-foreground shrink-0 text-sm">{row.total} ml</span>
            </div>
            <ServabilityNote result={row.pourable} />
          </div>
        </button>
      </li>
    {/each}
  </ul>
{/if}

{#if editing}
  <RecipeSheet
    recipe={editing}
    isNew={creating}
    onclose={() => {
      editing = null;
      creating = false;
    }}
  />
{/if}
