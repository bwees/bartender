<script lang="ts">
  import PlusIcon from '@lucide/svelte/icons/plus';
  import TrashIcon from '@lucide/svelte/icons/trash-2';
  import XIcon from '@lucide/svelte/icons/x';
  import * as AlertDialog from '$lib/components/ui/alert-dialog/index.js';
  import { effectiveHue, slotOf } from '$lib/hues';
  import * as Select from '$lib/components/ui/select/index.js';
  import * as Sheet from '$lib/components/ui/sheet/index.js';
  import { Button } from '$lib/components/ui/button/index.js';
  import { Input } from '$lib/components/ui/input/index.js';
  import { Label } from '$lib/components/ui/label/index.js';
  import ArtworkPicker from './ArtworkPicker.svelte';
  import ServabilityNote from './ServabilityNote.svelte';
  import { machine } from '$lib/machine.svelte';
  import { commit } from '$lib/save';
  import { servability, totalMl } from '$lib/servability';
  import type { Recipe } from '$lib/types';

  interface Props {
    recipe: Recipe;
    isNew: boolean;
    onclose: () => void;
  }

  let { recipe, isNew, onclose }: Props = $props();

  // svelte-ignore state_referenced_locally
  let draft: Recipe = $state($state.snapshot(recipe) as Recipe);
  let artwork = $state<Blob | null>(null);
  let preview = $state<string | null>(null);
  let confirmDelete = $state(false);
  let open = $state(true);

  const total = $derived(totalMl(draft));
  const pourable = $derived(servability(draft, machine.ingredients, machine.slots));
  const nameTaken = $derived(
    machine.recipes.some(
      (other) =>
        other.id !== draft.id && other.name.trim().toLowerCase() === draft.name.trim().toLowerCase()
    )
  );

  function finish() {
    if (preview) URL.revokeObjectURL(preview);
    onclose();
  }

  function addPour() {
    const used = new Set(draft.pours.map((pour) => pour.ingredient));
    const next = machine.ingredients.find((ingredient) => !used.has(ingredient.id));
    draft.pours.push({ ingredient: next?.id ?? machine.ingredients[0]?.id ?? '', ml: 30 });
  }

  function nameOf(id: string): string {
    return machine.ingredients.find((ingredient) => ingredient.id === id)?.name ?? 'Pick one';
  }

  async function save() {
    if (!draft.name.trim()) return;
    draft.name = draft.name.trim();
    if (await commit(machine.saveRecipe(draft, artwork), `${draft.name} saved`)) open = false;
  }

  async function remove() {
    if (await commit(machine.removeRecipe(draft.id), `${draft.name} deleted`)) open = false;
  }
</script>

<Sheet.Root bind:open onOpenChangeComplete={(next) => !next && finish()}>
  <Sheet.Content side="bottom" class="gap-0 p-0 data-[side=bottom]:h-[92dvh] sm:max-w-none">
    <Sheet.Header class="border-b px-4 py-4">
      <Sheet.Title>{isNew ? 'New drink' : draft.name || 'Drink'}</Sheet.Title>
      <Sheet.Description>
        {draft.pours.length} pour{draft.pours.length === 1 ? '' : 's'} · {total} ml total
      </Sheet.Description>
    </Sheet.Header>

    <div class="mx-auto w-full max-w-2xl flex-1 space-y-6 overflow-y-auto px-4 py-5">
      <div class="space-y-2">
        <Label for="recipe-name" class="text-sm font-medium">Name</Label>
        <Input
          id="recipe-name"
          bind:value={draft.name}
          placeholder="Negroni"
          class="h-12 text-base"
        />
        {#if nameTaken}
          <p class="text-xs text-amber-400">Another drink already uses that name.</p>
        {/if}
      </div>

      <ArtworkPicker
        recipe={draft}
        ingredients={machine.ingredients}
        slots={machine.slots}
        {preview}
        onchange={(blob, url) => {
          artwork = blob;
          preview = url;
          if (!blob) delete draft.image;
        }}
      />

      <div class="space-y-3">
        <div class="flex items-center justify-between">
          <Label class="text-sm font-medium">Pours</Label>
          <span class="text-muted-foreground text-sm">{total} ml</span>
        </div>

        {#if machine.ingredients.length === 0}
          <p class="text-muted-foreground text-sm">
            Add an ingredient to the catalogue first — a drink is made of pours from the catalogue.
          </p>
        {/if}

        {#each draft.pours as pour, index (index)}
          <div class="flex items-center gap-2">
            <Select.Root
              type="single"
              value={pour.ingredient}
              onValueChange={(value) => (draft.pours[index].ingredient = value)}
            >
              <Select.Trigger class="h-12 w-full flex-1 text-base">
                {nameOf(pour.ingredient)}
              </Select.Trigger>
              <Select.Content>
                {#each machine.ingredients as ingredient (ingredient.id)}
                  <Select.Item value={ingredient.id} label={ingredient.name}>
                    <span
                      class="mr-2 inline-block size-3 rounded-full ring-1 ring-white/20"
                      style="background-color: {effectiveHue(
                        ingredient.colour,
                        slotOf(machine.slots, ingredient.id)
                      )}"
                    ></span>
                    {ingredient.name}
                  </Select.Item>
                {/each}
              </Select.Content>
            </Select.Root>
            <Input
              type="number"
              inputmode="decimal"
              min={0}
              max={2000}
              step={5}
              value={pour.ml}
              onchange={(event) =>
                (draft.pours[index].ml = Math.max(0, Number(event.currentTarget.value) || 0))}
              aria-label="Millilitres"
              class="h-12 w-24 text-base"
            />
            <Button
              variant="ghost"
              size="icon"
              class="size-12 shrink-0"
              aria-label="Remove pour"
              onclick={() => draft.pours.splice(index, 1)}
            >
              <XIcon class="size-4" />
            </Button>
          </div>
        {/each}

        <Button
          variant="outline"
          class="h-11 w-full"
          disabled={machine.ingredients.length === 0}
          onclick={addPour}
        >
          <PlusIcon class="size-4" />
          Add a pour
        </Button>
      </div>

      <div class="space-y-2">
        <Label class="text-sm font-medium">Right now</Label>
        <ServabilityNote result={pourable} />
      </div>

      {#if !isNew}
        <Button
          variant="ghost"
          class="text-destructive h-11"
          onclick={() => (confirmDelete = true)}
        >
          <TrashIcon class="size-4" />
          Delete this drink
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
        The drink disappears from the touchscreen and its artwork is deleted from the SD card.
      </AlertDialog.Description>
    </AlertDialog.Header>
    <AlertDialog.Footer>
      <AlertDialog.Cancel>Keep it</AlertDialog.Cancel>
      <AlertDialog.Action onclick={remove}>Delete</AlertDialog.Action>
    </AlertDialog.Footer>
  </AlertDialog.Content>
</AlertDialog.Root>
