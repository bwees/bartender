<script lang="ts">
  import { Input } from '$lib/components/ui/input/index.js';
  import { Label } from '$lib/components/ui/label/index.js';

  interface Props {
    label: string;
    value: number;
    min?: number;
    max?: number;
    step?: number;
    unit?: string;
    hint?: string;
    id?: string;
    oncommit: (value: number) => void;
  }

  let {
    label,
    value,
    min = 0,
    max = Number.MAX_SAFE_INTEGER,
    step = 1,
    unit,
    hint,
    id = crypto.randomUUID(),
    oncommit
  }: Props = $props();

  function handle(event: Event & { currentTarget: HTMLInputElement }) {
    const parsed = Number(event.currentTarget.value);
    const next = Number.isFinite(parsed) ? Math.min(max, Math.max(min, parsed)) : value;
    event.currentTarget.value = String(next);
    if (next !== value) oncommit(next);
  }
</script>

<div class="space-y-2">
  <Label for={id} class="text-sm font-medium">{label}</Label>
  <div class="flex items-center gap-2">
    <Input
      {id}
      type="number"
      inputmode="decimal"
      {min}
      {max}
      {step}
      {value}
      onchange={handle}
      class="h-12 text-base"
    />
    {#if unit}
      <span class="text-muted-foreground w-16 shrink-0 text-sm">{unit}</span>
    {/if}
  </div>
  {#if hint}
    <p class="text-muted-foreground text-xs">{hint}</p>
  {/if}
</div>
