<script lang="ts">
  import ImageIcon from '@lucide/svelte/icons/image';
  import TrashIcon from '@lucide/svelte/icons/trash-2';
  import UploadIcon from '@lucide/svelte/icons/upload';
  import { toast } from 'svelte-sonner';
  import { MAX_ASSET_BYTES } from '$lib/api';
  import { Button } from '$lib/components/ui/button/index.js';
  import { Label } from '$lib/components/ui/label/index.js';
  import { Slider } from '$lib/components/ui/slider/index.js';
  import RecipeArt from './RecipeArt.svelte';
  import { ART_BOX, drawCrop, encodeJpeg, loadImage } from '$lib/image';
  import type { Ingredient, Recipe, Slot } from '$lib/types';

  interface Props {
    recipe: Recipe;
    ingredients: Ingredient[];
    slots?: Slot[];
    preview: string | null;
    onchange: (artwork: Blob | null, preview: string | null) => void;
  }

  let { recipe, ingredients, slots = [], preview, onchange }: Props = $props();

  let source: HTMLImageElement | null = $state(null);
  let canvas: HTMLCanvasElement | null = $state(null);
  let zoom = $state(1);
  let offsetX = $state(0);
  let offsetY = $state(0);
  let dragFrom: { x: number; y: number } | null = null;
  let dragging = $state(false);
  let busy = $state(false);

  $effect(() => {
    const context = canvas?.getContext('2d');
    if (!source || !context) return;
    drawCrop(context, source, ART_BOX, { zoom, offsetX, offsetY });
  });

  async function accept(file: File | undefined) {
    if (!file) return;
    try {
      source = await loadImage(file);
      zoom = 1;
      offsetX = 0;
      offsetY = 0;
    } catch (error) {
      toast.error(error instanceof Error ? error.message : 'That file could not be read.');
    }
  }

  function onDrop(event: DragEvent) {
    event.preventDefault();
    dragging = false;
    void accept(event.dataTransfer?.files?.[0]);
  }

  function pointerScale(): number {
    const rect = canvas?.getBoundingClientRect();
    return rect && rect.width > 0 ? ART_BOX.width / rect.width : 1;
  }

  function onPointerDown(event: PointerEvent) {
    dragFrom = { x: event.clientX, y: event.clientY };
    (event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
  }

  function onPointerMove(event: PointerEvent) {
    if (!dragFrom) return;
    const scale = pointerScale();
    offsetX += (event.clientX - dragFrom.x) * scale;
    offsetY += (event.clientY - dragFrom.y) * scale;
    dragFrom = { x: event.clientX, y: event.clientY };
  }

  function onPointerUp() {
    dragFrom = null;
  }

  async function useCrop() {
    if (!canvas) return;
    busy = true;
    try {
      const blob = await encodeJpeg(canvas, MAX_ASSET_BYTES);
      if (preview) URL.revokeObjectURL(preview);
      onchange(blob, URL.createObjectURL(blob));
      source = null;
    } catch (error) {
      toast.error(error instanceof Error ? error.message : 'That image could not be encoded.');
    } finally {
      busy = false;
    }
  }

  function clear() {
    if (preview) URL.revokeObjectURL(preview);
    onchange(null, null);
    source = null;
  }
</script>

<div class="space-y-3">
  <Label class="text-sm font-medium">Artwork</Label>

  {#if source}
    <div class="space-y-3">
      <canvas
        bind:this={canvas}
        width={ART_BOX.width}
        height={ART_BOX.height}
        onpointerdown={onPointerDown}
        onpointermove={onPointerMove}
        onpointerup={onPointerUp}
        onpointercancel={onPointerUp}
        class="border-input mx-auto aspect-square w-full max-w-[280px] cursor-grab touch-none rounded-lg border active:cursor-grabbing"
      ></canvas>
      <div class="mx-auto max-w-[280px] space-y-2">
        <Slider type="single" bind:value={zoom} min={1} max={4} step={0.05} />
        <p class="text-muted-foreground text-center text-xs">Drag to reposition, slide to zoom.</p>
      </div>
      <div class="flex gap-2">
        <Button class="h-11 flex-1" disabled={busy} onclick={useCrop}>
          {busy ? 'Encoding…' : 'Use this crop'}
        </Button>
        <Button variant="ghost" class="h-11" onclick={() => (source = null)}>Cancel</Button>
      </div>
    </div>
  {:else}
    <div class="flex items-center gap-4">
      <RecipeArt
        {recipe}
        {ingredients}
        {slots}
        {preview}
        class="size-20 shrink-0 rounded-lg border border-white/10"
      />
      <label
        ondragover={(event) => {
          event.preventDefault();
          dragging = true;
        }}
        ondragleave={() => (dragging = false)}
        ondrop={onDrop}
        class="flex flex-1 cursor-pointer flex-col items-center gap-1 rounded-lg border border-dashed p-4 text-center text-sm transition
          {dragging ? 'border-ring bg-accent' : 'border-input'}"
      >
        <UploadIcon class="text-muted-foreground size-5" />
        <span>Drop an image or tap to choose</span>
        <input
          type="file"
          accept="image/*"
          class="sr-only"
          onchange={(event) => {
            void accept(event.currentTarget.files?.[0]);
            event.currentTarget.value = '';
          }}
        />
      </label>
    </div>
    {#if preview || recipe.image}
      <Button variant="ghost" size="sm" class="text-muted-foreground" onclick={clear}>
        <TrashIcon class="size-4" />
        Remove artwork
      </Button>
    {/if}
    <p class="text-muted-foreground flex items-start gap-2 text-xs">
      <ImageIcon class="mt-0.5 size-3.5 shrink-0" />
      <span>
        Cropped to {ART_BOX.width}×{ART_BOX.height} and sent as a baseline JPEG. Removing artwork deletes
        the file from the SD card when you save.
      </span>
    </p>
  {/if}
</div>
