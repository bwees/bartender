import { render } from '@testing-library/svelte';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import Ingredients from './ingredients/+page.svelte';
import Recipes from './+page.svelte';
import Settings from './settings/+page.svelte';
import Slots from './slots/+page.svelte';
import { machine } from '$lib/machine.svelte';
import library from '../../../sdcard/recipes.json';
import config from '../../../sdcard/config.json';

beforeEach(async () => {
  vi.stubGlobal(
    'fetch',
    vi.fn(async (url: string) =>
      url === '/api/recipes'
        ? new Response(JSON.stringify(library))
        : new Response(JSON.stringify(config))
    )
  );
  await machine.load();
});

afterEach(() => {
  vi.unstubAllGlobals();
});

describe('every page renders against the shipped documents', () => {
  it('lists recipes with their servability', () => {
    const { getByText } = render(Recipes);
    expect(getByText('Negroni')).toBeTruthy();
    expect(getByText('90 ml')).toBeTruthy();
    expect(getByText('Ready to pour')).toBeTruthy();
  });

  it('lists the ingredient catalogue', () => {
    const { getAllByText, getByText } = render(Ingredients);
    expect(getByText('Sweet vermouth')).toBeTruthy();
    expect(getByText('16% vol · 1 drink')).toBeTruthy();
    expect(getAllByText('Loaded')).toHaveLength(3);
  });

  it('shows one card per pump even when the document stores fewer slots', () => {
    const { getAllByText } = render(Slots);
    expect(getAllByText(/^Slot \d$/)).toHaveLength(config.pumpCount);
  });

  it('renders the machine settings', () => {
    const { getByLabelText } = render(Settings);
    expect((getByLabelText('Network name') as HTMLInputElement).value).toBe(config.wifi.ssid);
    expect((getByLabelText('PIN') as HTMLInputElement).value).toBe(config.pin);
  });
});
