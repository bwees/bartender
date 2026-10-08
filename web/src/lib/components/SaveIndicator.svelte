<script lang="ts">
  import RefreshIcon from '@lucide/svelte/icons/refresh-cw';
  import CheckIcon from '@lucide/svelte/icons/check';
  import { machine } from '$lib/machine.svelte';
</script>

<span class="text-muted-foreground flex items-center gap-1.5 text-xs">
  {#if machine.saving}
    <RefreshIcon class="size-3.5 animate-spin" />
    Saving
  {:else if machine.error}
    <span class="size-2 rounded-full bg-amber-400"></span>
    Not saved
  {:else}
    <CheckIcon class="size-3.5 text-emerald-400" />
    Saved
  {/if}
</span>
