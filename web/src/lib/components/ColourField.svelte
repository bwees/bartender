<script lang="ts">
  import { Input } from '$lib/components/ui/input/index.js';
  import { Label } from '$lib/components/ui/label/index.js';
  import { SLOT_HUES } from '$lib/hues';

  interface Props {
    value: string;
    label?: string;
  }

  let { value = $bindable(), label = 'Colour' }: Props = $props();

  const swatches = SLOT_HUES;

  function normalise(raw: string): string {
    const trimmed = raw.trim();
    const hex = trimmed.startsWith('#') ? trimmed : `#${trimmed}`;
    return /^#[0-9a-fA-F]{6}$/.test(hex) ? hex.toUpperCase() : value;
  }
</script>

<div class="space-y-3">
  <Label class="text-sm font-medium">{label}</Label>
  <div class="flex items-center gap-3">
    <input
      type="color"
      aria-label="{label} picker"
      bind:value
      class="border-input h-12 w-14 shrink-0 cursor-pointer rounded-md border bg-transparent p-1"
    />
    <Input
      value={value.toUpperCase()}
      onchange={(event) => (value = normalise(event.currentTarget.value))}
      spellcheck={false}
      class="h-12 font-mono text-base"
      aria-label="{label} hex value"
    />
  </div>
  <div class="flex flex-wrap gap-2">
    {#each swatches as swatch (swatch)}
      <button
        type="button"
        aria-label="Use {swatch}"
        onclick={() => (value = swatch)}
        style="background-color: {swatch}"
        class="size-9 rounded-full ring-offset-2 ring-offset-background {value.toUpperCase() ===
        swatch
          ? 'ring-2 ring-ring'
          : 'ring-1 ring-white/15'}"
      ></button>
    {/each}
  </div>
  <p class="text-muted-foreground text-xs">
    Used while this ingredient is not loaded in a slot. Once it is, the touchscreen and the LED ring
    both use that slot's fixed hue — the eight swatches above — so the two never disagree.
  </p>
</div>
