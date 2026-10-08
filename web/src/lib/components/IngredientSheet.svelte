<script lang="ts">
  import TrashIcon from '@lucide/svelte/icons/trash-2';
  import * as AlertDialog from '$lib/components/ui/alert-dialog/index.js';
  import * as Sheet from '$lib/components/ui/sheet/index.js';
  import { Button } from '$lib/components/ui/button/index.js';
  import { Input } from '$lib/components/ui/input/index.js';
  import { Label } from '$lib/components/ui/label/index.js';
  import ColourField from './ColourField.svelte';
  import { machine } from '$lib/machine.svelte';
  import { commit } from '$lib/save';
  import type { Ingredient } from '$lib/types';

  interface Props {
    ingredient: Ingredient;
    isNew: boolean;
    onclose: () => void;
  }

  let { ingredient, isNew, onclose }: Props = $props();

  // svelte-ignore state_referenced_locally
  let draft: Ingredient = $state($state.snapshot(ingredient) as Ingredient);
  let confirmDelete = $state(false);
  let open = $state(true);

  const usedBy = $derived(
    machine.recipes.filter((recipe) => recipe.pours.some((pour) => pour.ingredient === draft.id))
  );
  const inSlots = $derived(machine.slots.filter((slot) => slot.ingredient === draft.id));
  const secondsFor30ml = $derived((30 / Math.max(draft.mlPerSecond, 0.1)).toFixed(1));

  async function save() {
    if (!draft.name.trim()) return;
    draft.name = draft.name.trim();
    if (await commit(machine.saveIngredient(draft), `${draft.name} saved`)) open = false;
  }

  async function remove() {
    if (await commit(machine.removeIngredient(draft.id), `${draft.name} deleted`)) open = false;
  }
</script>

<Sheet.Root bind:open onOpenChangeComplete={(next) => !next && onclose()}>
  <Sheet.Content side="bottom" class="gap-0 p-0 data-[side=bottom]:h-[88dvh] sm:max-w-none">
    <Sheet.Header class="border-b px-4 py-4">
      <Sheet.Title>{isNew ? 'New ingredient' : draft.name || 'Ingredient'}</Sheet.Title>
      <Sheet.Description>
        Used by {usedBy.length} drink{usedBy.length === 1 ? '' : 's'}
      </Sheet.Description>
    </Sheet.Header>

    <div class="mx-auto w-full max-w-2xl flex-1 space-y-6 overflow-y-auto px-4 py-5">
      <div class="space-y-2">
        <Label for="ingredient-name" class="text-sm font-medium">Name</Label>
        <Input
          id="ingredient-name"
          bind:value={draft.name}
          placeholder="Gin"
          class="h-12 text-base"
        />
      </div>

      <ColourField bind:value={draft.colour} />

      <div class="space-y-2">
        <Label for="ingredient-abv" class="text-sm font-medium">ABV</Label>
        <div class="flex items-center gap-2">
          <Input
            id="ingredient-abv"
            type="number"
            inputmode="decimal"
            min={0}
            max={100}
            step={0.5}
            value={draft.abv ?? ''}
            placeholder="optional"
            onchange={(event) => {
              const raw = event.currentTarget.value.trim();
              if (raw === '') delete draft.abv;
              else draft.abv = Math.min(100, Math.max(0, Number(raw) || 0));
            }}
            class="h-12 text-base"
          />
          <span class="text-muted-foreground w-16 shrink-0 text-sm">% vol</span>
        </div>
        <p class="text-muted-foreground text-xs">Leave blank for mixers and juices.</p>
      </div>

      <div class="space-y-2">
        <Label for="ingredient-rate" class="text-sm font-medium">Flow rate</Label>
        <div class="flex items-center gap-2">
          <Input
            id="ingredient-rate"
            type="number"
            inputmode="decimal"
            min={0.1}
            max={100}
            step={0.1}
            value={draft.mlPerSecond}
            onchange={(event) => {
              const parsed = Number(event.currentTarget.value);
              draft.mlPerSecond = Number.isFinite(parsed)
                ? Math.min(100, Math.max(0.1, parsed))
                : 0.1;
              event.currentTarget.value = String(draft.mlPerSecond);
            }}
            class="h-12 text-base"
          />
          <span class="text-muted-foreground w-16 shrink-0 text-sm">ml/s</span>
        </div>
        <p class="text-muted-foreground text-xs">
          A 30 ml pour takes {secondsFor30ml} s. Thick liquids run slower than spirits, and the rate travels
          with the bottle — it stays right whichever pump this is plugged into.
        </p>
      </div>

      {#if !isNew}
        <Button
          variant="ghost"
          class="text-destructive h-11"
          onclick={() => (confirmDelete = true)}
        >
          <TrashIcon class="size-4" />
          Delete this ingredient
        </Button>
      {/if}
    </div>

    <Sheet.Footer class="flex-row gap-2 border-t px-4 py-3">
      <Button variant="outline" class="h-12 flex-1" onclick={() => (open = false)}>Cancel</Button>
      <Button class="h-12 flex-1" disabled={!draft.name.trim() || machine.saving} onclick={save}>
        {machine.saving ? 'Saving…' : 'Save'}
      </Button>
    </Sheet.Footer>
  </Sheet.Content>
</Sheet.Root>

<AlertDialog.Root bind:open={confirmDelete}>
  <AlertDialog.Content>
    <AlertDialog.Header>
      <AlertDialog.Title>Delete {draft.name}?</AlertDialog.Title>
      <AlertDialog.Description>
        {#if usedBy.length > 0}
          {usedBy.length} drink{usedBy.length === 1 ? '' : 's'} pour this: {usedBy
            .map((recipe) => recipe.name)
            .join(', ')}. Those pours are removed too.
        {:else}
          Nothing pours this ingredient.
        {/if}
        {#if inSlots.length > 0}
          It is loaded in {inSlots.map((slot) => `slot ${slot.index}`).join(', ')}, which will be
          cleared.
        {/if}
      </AlertDialog.Description>
    </AlertDialog.Header>
    <AlertDialog.Footer>
      <AlertDialog.Cancel>Keep it</AlertDialog.Cancel>
      <AlertDialog.Action onclick={remove}>Delete</AlertDialog.Action>
    </AlertDialog.Footer>
  </AlertDialog.Content>
</AlertDialog.Root>
