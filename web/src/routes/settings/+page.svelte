<script lang="ts">
  import EyeIcon from '@lucide/svelte/icons/eye';
  import EyeOffIcon from '@lucide/svelte/icons/eye-off';
  import * as Card from '$lib/components/ui/card/index.js';
  import { Button } from '$lib/components/ui/button/index.js';
  import { Input } from '$lib/components/ui/input/index.js';
  import { Label } from '$lib/components/ui/label/index.js';
  import { Slider } from '$lib/components/ui/slider/index.js';
  import { Switch } from '$lib/components/ui/switch/index.js';
  import NumberField from '$lib/components/NumberField.svelte';
  import { machine } from '$lib/machine.svelte';
  import { commit } from '$lib/save';
  import type { Config } from '$lib/types';

  const config = $derived(machine.config);

  let showPassphrase = $state(false);
  let ringBrightness = $state(0);
  let ssidError = $state('');
  let passphraseError = $state('');
  let pinError = $state('');

  $effect(() => {
    ringBrightness = machine.config?.ring.brightness ?? 0;
  });

  function edit(mutate: (draft: Config) => void) {
    void commit(machine.updateConfig(mutate));
  }

  function saveSsid(value: string) {
    const ssid = value.trim();
    ssidError = ssid.length === 0 || ssid.length > 32 ? 'Between 1 and 32 characters.' : '';
    if (!ssidError) edit((draft) => (draft.wifi.ssid = ssid));
  }

  function savePassphrase(value: string) {
    passphraseError = value.length < 8 || value.length > 63 ? 'WPA2 needs 8 to 63 characters.' : '';
    if (!passphraseError) edit((draft) => (draft.wifi.passphrase = value));
  }

  function savePin(value: string) {
    pinError = /^\d{4,8}$/.test(value) ? '' : 'Four to eight digits.';
    if (!pinError) edit((draft) => (draft.pin = value));
  }
</script>

<div class="mb-5">
  <h1 class="text-2xl font-semibold tracking-tight">Machine</h1>
  <p class="text-muted-foreground text-sm">Changes are written as soon as you leave a field.</p>
</div>

{#if config}
  <div class="space-y-4">
    <Card.Root>
      <Card.Header>
        <Card.Title>Pumps</Card.Title>
        <Card.Description>How many channels exist. Slots follow this number.</Card.Description>
      </Card.Header>
      <Card.Content class="space-y-5">
        <NumberField
          label="Pump count"
          value={config.pumpCount}
          min={0}
          max={24}
          oncommit={(value) => edit((draft) => (draft.pumpCount = value))}
        />
      </Card.Content>
    </Card.Root>

    <Card.Root>
      <Card.Header>
        <Card.Title>Pouring</Card.Title>
        <Card.Description>How the scheduler drives the pumps.</Card.Description>
      </Card.Header>
      <Card.Content class="space-y-5">
        <NumberField
          label="Concurrency"
          value={config.pour.concurrency}
          min={1}
          max={Math.max(1, config.pumpCount)}
          hint="How many pumps may run at once."
          oncommit={(value) => edit((draft) => (draft.pour.concurrency = value))}
        />
        <NumberField
          label="Pause window"
          value={config.pour.pauseWindowSeconds}
          min={0}
          max={600}
          unit="seconds"
          hint="How long a pour may sit paused before it is abandoned."
          oncommit={(value) => edit((draft) => (draft.pour.pauseWindowSeconds = value))}
        />
        <NumberField
          label="Watchdog factor"
          value={config.pour.watchdogFactor}
          min={1}
          max={10}
          step={0.1}
          hint="Multiple of the expected run time before a pump is cut off."
          oncommit={(value) => edit((draft) => (draft.pour.watchdogFactor = value))}
        />
      </Card.Content>
    </Card.Root>

    <Card.Root>
      <Card.Header>
        <Card.Title>Cup sensor</Card.Title>
        <Card.Description>Distance readings that decide whether a cup is present.</Card.Description>
      </Card.Header>
      <Card.Content class="space-y-5">
        <div class="flex items-center justify-between">
          <Label for="cup-enabled" class="text-sm font-medium">Require a cup</Label>
          <Switch
            id="cup-enabled"
            checked={config.cupSensor.enabled}
            onCheckedChange={(checked) => edit((draft) => (draft.cupSensor.enabled = checked))}
          />
        </div>
        <NumberField
          label="Empty reading"
          value={config.cupSensor.emptyMm}
          min={0}
          max={2000}
          step={0.5}
          unit="mm"
          hint="Distance measured with nothing under the nozzles."
          oncommit={(value) => edit((draft) => (draft.cupSensor.emptyMm = value))}
        />
        <NumberField
          label="Cup reading"
          value={config.cupSensor.cupMm}
          min={0}
          max={2000}
          step={0.5}
          unit="mm"
          hint="Distance measured with a cup in place."
          oncommit={(value) => edit((draft) => (draft.cupSensor.cupMm = value))}
        />
        <NumberField
          label="Debounce samples"
          value={config.cupSensor.debounceSamples}
          min={1}
          max={50}
          hint="Consecutive agreeing readings before the state flips."
          oncommit={(value) => edit((draft) => (draft.cupSensor.debounceSamples = value))}
        />
      </Card.Content>
    </Card.Root>

    <Card.Root>
      <Card.Header>
        <Card.Title>LED ring</Card.Title>
        <Card.Description>Lights ingredient colours while a drink pours.</Card.Description>
      </Card.Header>
      <Card.Content class="space-y-5">
        <NumberField
          label="Pixels"
          value={config.ring.pixels}
          min={0}
          max={256}
          oncommit={(value) => edit((draft) => (draft.ring.pixels = value))}
        />
        <div class="space-y-3">
          <div class="flex items-center justify-between">
            <Label class="text-sm font-medium">Ring brightness</Label>
            <span class="text-muted-foreground text-sm">{ringBrightness} / 255</span>
          </div>
          <Slider
            type="single"
            bind:value={ringBrightness}
            min={0}
            max={255}
            step={1}
            onValueCommit={(value) => edit((draft) => (draft.ring.brightness = value))}
          />
        </div>
      </Card.Content>
    </Card.Root>

    <Card.Root>
      <Card.Header>
        <Card.Title>Touchscreen</Card.Title>
        <Card.Description>
          The panel on the front of the machine. Its backlight is not adjustable — the companion
          MCU's brightness command has no effect on this hardware.
        </Card.Description>
      </Card.Header>
      <Card.Content class="space-y-5">
        <div class="space-y-2">
          <Label for="pin" class="text-sm font-medium">PIN</Label>
          <Input
            id="pin"
            inputmode="numeric"
            value={config.pin}
            onchange={(event) => savePin(event.currentTarget.value)}
            class="h-12 max-w-40 font-mono text-lg tracking-[0.4em]"
          />
          <p class="text-muted-foreground text-xs">Gates the settings screens on the panel.</p>
          {#if pinError}<p class="text-xs text-amber-400">{pinError}</p>{/if}
        </div>
      </Card.Content>
    </Card.Root>

    <Card.Root>
      <Card.Header>
        <Card.Title>Access point</Card.Title>
        <Card.Description>
          Saving either field drops this connection — the machine restarts its AP. Reconnect with
          the new details.
        </Card.Description>
      </Card.Header>
      <Card.Content class="space-y-5">
        <div class="space-y-2">
          <Label for="wifi-ssid" class="text-sm font-medium">Network name</Label>
          <Input
            id="wifi-ssid"
            value={config.wifi.ssid}
            onchange={(event) => saveSsid(event.currentTarget.value)}
            class="h-12 text-base"
          />
          {#if ssidError}<p class="text-xs text-amber-400">{ssidError}</p>{/if}
        </div>
        <div class="space-y-2">
          <Label for="wifi-pass" class="text-sm font-medium">Passphrase</Label>
          <div class="flex items-center gap-2">
            <Input
              id="wifi-pass"
              type={showPassphrase ? 'text' : 'password'}
              value={config.wifi.passphrase}
              autocomplete="off"
              onchange={(event) => savePassphrase(event.currentTarget.value)}
              class="h-12 text-base"
            />
            <Button
              variant="outline"
              size="icon"
              class="size-12 shrink-0"
              aria-label={showPassphrase ? 'Hide passphrase' : 'Show passphrase'}
              onclick={() => (showPassphrase = !showPassphrase)}
            >
              {#if showPassphrase}<EyeOffIcon class="size-4" />{:else}<EyeIcon
                  class="size-4"
                />{/if}
            </Button>
          </div>
          {#if passphraseError}<p class="text-xs text-amber-400">{passphraseError}</p>{/if}
        </div>
      </Card.Content>
    </Card.Root>
  </div>
{/if}
