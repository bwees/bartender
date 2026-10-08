<script lang="ts">
  import { page } from '$app/state';
  import { Button } from '$lib/components/ui/button/index.js';
</script>

<div class="space-y-4 py-20 text-center">
  <p class="text-4xl font-semibold">{page.status}</p>
  <p class="text-muted-foreground text-sm">
    {page.error?.message ?? 'That screen does not exist on this machine.'}
  </p>
  <Button href="/">Back to recipes</Button>
</div>
