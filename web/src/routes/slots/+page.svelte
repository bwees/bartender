<script lang="ts">
  import * as Select from '$lib/components/ui/select/index.js';
  import { Card } from '$lib/components/ui/card/index.js';
  import { Label } from '$lib/components/ui/label/index.js';
  import { Switch } from '$lib/components/ui/switch/index.js';
  import { machine } from '$lib/machine.svelte';
  import { commit } from '$lib/save';
  import { servability, slotsFor } from '$lib/servability';

  const NONE = 'none';

  const slots = $derived(machine.slots);
  const pourable = $derived(
    machine.recipes.filter((recipe) => servability(recipe, machine.ingredients, slots).servable)
      .length
  );

  function nameOf(id: string | null): string {
    if (!id) return 'Empty';
    return machine.ingredients.find((ingredient) => ingredient.id === id)?.name ?? 'Unknown';
  }

  function setIngredient(index: number, value: string) {
    void commit(
      machine.updateConfig((draft) => {
        const next = slotsFor(draft);
        next[index].ingredient = value === NONE ? null : value;
        next[index].empty = false;
        draft.slots = next;
      })
    );
  }

  function setEmpty(index: number, empty: boolean) {
    void commit(
      machine.updateConfig((draft) => {
        const next = slotsFor(draft);
        next[index].empty = empty;
        draft.slots = next;
      })
    );
  }
</script>

<div class="mb-5">
  <h1 class="text-2xl font-semibold tracking-tight">Slots</h1>
  <p class="text-muted-foreground text-sm">
    {pourable} of {machine.recipes.length} drinks pourable with what is loaded
  </p>
</div>

<div class="grid gap-3 sm:grid-cols-2">
  {#each slots as slot (slot.index)}
    {@const ingredient = machine.ingredients.find((item) => item.id === slot.ingredient)}
    <Card class="gap-3 p-4">
      <div class="flex items-center gap-3">
        <span
          class="size-8 shrink-0 rounded-full ring-1 ring-white/15"
          style="background-color: {ingredient?.colour ?? '#27272a'}; opacity: {slot.empty
            ? 0.3
            : 1}"
        ></span>
        <div class="min-w-0 flex-1">
          <p class="text-muted-foreground text-xs">Slot {slot.index}</p>
          <p class="truncate text-base font-medium {slot.empty ? 'line-through opacity-60' : ''}">
            {nameOf(slot.ingredient)}
          </p>
        </div>
      </div>

      <Select.Root
        type="single"
        value={slot.ingredient ?? NONE}
        onValueChange={(value) => setIngredient(slot.index, value)}
      >
        <Select.Trigger class="h-12 w-full text-base">
          {nameOf(slot.ingredient)}
        </Select.Trigger>
        <Select.Content>
          <Select.Item value={NONE} label="Empty">Empty</Select.Item>
          {#each machine.ingredients as item (item.id)}
            <Select.Item value={item.id} label={item.name}>
              <span
                class="mr-2 inline-block size-3 rounded-full ring-1 ring-white/20"
                style="background-color: {item.colour}"
              ></span>
              {item.name}
            </Select.Item>
          {/each}
        </Select.Content>
      </Select.Root>

      <div class="flex items-center justify-between">
        <Label for="empty-{slot.index}" class="text-sm">Ran out</Label>
        <Switch
          id="empty-{slot.index}"
          checked={slot.empty}
          disabled={!slot.ingredient}
          onCheckedChange={(checked) => setEmpty(slot.index, checked)}
        />
      </div>
    </Card>
  {/each}
</div>

{#if slots.length === 0}
  <Card class="p-8 text-center">
    <p class="text-muted-foreground text-sm">
      This machine reports no pumps. Set the pump count in Settings.
    </p>
  </Card>
{/if}
