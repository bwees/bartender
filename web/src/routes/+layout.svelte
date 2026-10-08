<script lang="ts">
  import '../app.css';
  import { onMount } from 'svelte';
  import { page } from '$app/state';
  import RefreshIcon from '@lucide/svelte/icons/refresh-cw';
  import { Button } from '$lib/components/ui/button/index.js';
  import { Toaster } from '$lib/components/ui/sonner/index.js';
  import SaveIndicator from '$lib/components/SaveIndicator.svelte';
  import { machine } from '$lib/machine.svelte';
  import { navItems } from '$lib/nav';

  let { children } = $props();

  onMount(() => void machine.load());

  function active(href: string): boolean {
    return href === '/' ? page.url.pathname === '/' : page.url.pathname.startsWith(href);
  }
</script>

<Toaster position="top-center" richColors closeButton />

<div class="min-h-dvh md:grid md:grid-cols-[15rem_1fr]">
  <aside class="hidden md:sticky md:top-0 md:flex md:h-dvh md:flex-col md:border-r md:p-4">
    <p class="px-3 pb-4 text-lg font-semibold tracking-tight">Bartender</p>
    <nav class="flex flex-col gap-1">
      {#each navItems as item (item.href)}
        <a
          href={item.href}
          class="flex items-center gap-3 rounded-md px-3 py-2 text-sm transition
            {active(item.href)
            ? 'bg-accent text-accent-foreground font-medium'
            : 'text-muted-foreground hover:bg-accent/50'}"
        >
          <item.icon class="size-4" />
          {item.label}
        </a>
      {/each}
    </nav>
    <div class="mt-auto px-3">
      <SaveIndicator />
    </div>
  </aside>

  <div class="flex min-h-dvh flex-col">
    <header
      class="bg-background/90 sticky top-0 z-30 flex items-center justify-between border-b px-4 py-3 backdrop-blur md:hidden"
    >
      <p class="text-base font-semibold tracking-tight">Bartender</p>
      <SaveIndicator />
    </header>

    <main class="mx-auto w-full max-w-3xl flex-1 px-4 pt-5 pb-28 md:pb-10">
      {#if !machine.ready && machine.loading}
        <p class="text-muted-foreground py-16 text-center text-sm">Reading the machine…</p>
      {:else if !machine.ready && machine.loadError}
        <div class="space-y-4 py-16 text-center">
          <p class="text-sm">{machine.loadError}</p>
          <Button onclick={() => machine.load()}>
            <RefreshIcon class="size-4" />
            Try again
          </Button>
        </div>
      {:else}
        {@render children()}
      {/if}
    </main>
  </div>

  <nav
    class="bg-background/95 fixed inset-x-0 bottom-0 z-30 grid grid-cols-5 border-t pb-[env(safe-area-inset-bottom)] backdrop-blur md:hidden"
  >
    {#each navItems as item (item.href)}
      <a
        href={item.href}
        class="flex flex-col items-center gap-1 py-2.5 text-[11px]
          {active(item.href) ? 'text-foreground' : 'text-muted-foreground'}"
      >
        <item.icon class="size-5" />
        {item.label}
      </a>
    {/each}
  </nav>
</div>
