<script lang="ts">
  import CheckIcon from '@lucide/svelte/icons/check';
  import TriangleAlertIcon from '@lucide/svelte/icons/triangle-alert';
  import { Badge } from '$lib/components/ui/badge/index.js';
  import type { Servability } from '$lib/servability';

  interface Props {
    result: Servability;
    compact?: boolean;
  }

  let { result, compact = false }: Props = $props();
</script>

{#if result.servable}
  <Badge variant="secondary" class="border-emerald-500/30 bg-emerald-500/15 text-emerald-300">
    <CheckIcon class="size-3.5" />
    Ready to pour
  </Badge>
{:else}
  <div class="space-y-1">
    <Badge variant="secondary" class="border-amber-500/30 bg-amber-500/15 text-amber-300">
      <TriangleAlertIcon class="size-3.5" />
      Cannot pour
    </Badge>
    {#if !compact}
      <ul class="text-muted-foreground space-y-0.5 text-xs">
        {#each result.reasons as reason (reason)}
          <li>{reason}</li>
        {/each}
      </ul>
    {/if}
  </div>
{/if}
