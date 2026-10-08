<script lang="ts">
  import PlusIcon from '@lucide/svelte/icons/plus';
  import { Badge } from '$lib/components/ui/badge/index.js';
  import { Button } from '$lib/components/ui/button/index.js';
  import { Card } from '$lib/components/ui/card/index.js';
  import IngredientSheet from '$lib/components/IngredientSheet.svelte';
  import { machine } from '$lib/machine.svelte';
  import { DEFAULT_ML_PER_SECOND } from '$lib/normalise';
  import type { Ingredient } from '$lib/types';
  import { uuid } from '$lib/uuid';

  let editing = $state<Ingredient | null>(null);
  let creating = $state(false);

  const rows = $derived(
    machine.ingredients.map((ingredient) => ({
      ingredient,
      loaded: machine.slots.some((slot) => slot.ingredient === ingredient.id && !slot.empty),
      uses: machine.recipes.filter((recipe) =>
        recipe.pours.some((pour) => pour.ingredient === ingredient.id)
      ).length
    }))
  );

  function create() {
    creating = true;
    editing = { id: uuid(), name: '', colour: '#94C7C1', mlPerSecond: DEFAULT_ML_PER_SECOND };
  }
</script>

<div class="mb-5 flex items-end justify-between gap-4">
  <div>
    <h1 class="text-2xl font-semibold tracking-tight">Ingredients</h1>
    <p class="text-muted-foreground text-sm">The catalogue every drink pours from</p>
  </div>
  <Button class="h-11" onclick={create}>
    <PlusIcon class="size-4" />
    New
  </Button>
</div>

{#if rows.length === 0}
  <Card class="p-8 text-center">
    <p class="text-muted-foreground text-sm">Nothing in the catalogue yet.</p>
  </Card>
{:else}
  <ul class="space-y-2">
    {#each rows as row (row.ingredient.id)}
      <li>
        <button
          type="button"
          class="hover:bg-accent/40 focus-visible:ring-ring bg-card flex w-full items-center gap-4 rounded-xl border p-3 text-left transition focus-visible:ring-2 focus-visible:outline-none"
          onclick={() => {
            creating = false;
            editing = row.ingredient;
          }}
        >
          <span
            class="size-9 shrink-0 rounded-full ring-1 ring-white/20"
            style="background-color: {row.ingredient.colour}"
          ></span>
          <div class="min-w-0 flex-1">
            <p class="truncate text-base font-medium">{row.ingredient.name}</p>
            <p class="text-muted-foreground text-xs">
              {row.ingredient.abv !== undefined ? `${row.ingredient.abv}% vol · ` : ''}{row.uses}
              drink{row.uses === 1 ? '' : 's'}
            </p>
          </div>
          {#if row.loaded}
            <Badge variant="secondary" class="shrink-0">Loaded</Badge>
          {/if}
        </button>
      </li>
    {/each}
  </ul>
{/if}

{#if editing}
  <IngredientSheet
    ingredient={editing}
    isNew={creating}
    onclose={() => {
      editing = null;
      creating = false;
    }}
  />
{/if}
