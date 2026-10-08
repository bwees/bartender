import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { Machine } from './machine.svelte';
import type { Config, Library } from './types';

const library: Library = {
  version: 1,
  ingredients: [
    { id: 'gin', name: 'Gin', colour: '#94C7C1', abv: 40, mlPerSecond: 4.2 },
    { id: 'campari', name: 'Campari', colour: '#C0392B', abv: 25, mlPerSecond: 3.1 }
  ],
  recipes: [
    {
      id: 'negroni',
      name: 'Negroni',
      pours: [
        { ingredient: 'gin', ml: 30 },
        { ingredient: 'campari', ml: 30 }
      ]
    }
  ]
};

const config: Config = {
  version: 1,
  slots: [
    { index: 0, ingredient: 'gin', empty: false },
    { index: 1, ingredient: 'campari', empty: false }
  ],
  pumpCount: 2,
  pour: { concurrency: 1, pauseWindowSeconds: 30, watchdogFactor: 1.5 },
  cupSensor: { enabled: true, emptyMm: 95, cupMm: 45, debounceSamples: 3 },
  wifi: { ssid: 'Bartender-A3F2', passphrase: 'change-me' },
  pin: '1234',
  ring: { pixels: 16, brightness: 60 }
};

interface Call {
  method: string;
  url: string;
  contentType: string | null;
  body: unknown;
}

function server() {
  const stored = { library: structuredClone(library), config: structuredClone(config) };
  const calls: Call[] = [];
  let conflicts = 0;

  const handler = vi.fn(async (url: string, init?: RequestInit) => {
    const method = init?.method ?? 'GET';
    const headers = (init?.headers ?? {}) as Record<string, string>;
    const contentType = headers['content-type'] ?? null;
    const raw = init?.body;
    const body = typeof raw === 'string' ? JSON.parse(raw) : raw;
    calls.push({ method, url, contentType, body });

    if (method !== 'GET' && conflicts > 0) {
      conflicts -= 1;
      return new Response(JSON.stringify({ error: 'pouring' }), { status: 409 });
    }
    if (url === '/api/recipes') {
      if (method === 'PUT') stored.library = body as Library;
      return new Response(JSON.stringify(stored.library), { status: 200 });
    }
    if (url === '/api/config') {
      if (method === 'PUT') stored.config = body as Config;
      return new Response(JSON.stringify(stored.config), { status: 200 });
    }
    if (url.startsWith('/api/assets/')) {
      const status = method === 'DELETE' ? 200 : 201;
      return new Response(JSON.stringify({ ok: true }), { status });
    }
    return new Response(JSON.stringify({ error: 'not found' }), { status: 404 });
  });

  return {
    calls,
    stored,
    handler,
    conflictOnce() {
      conflicts = 1;
    }
  };
}

let fake: ReturnType<typeof server>;

beforeEach(() => {
  fake = server();
  vi.stubGlobal('fetch', fake.handler);
});

afterEach(() => {
  vi.unstubAllGlobals();
});

function plain<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T;
}

async function loaded(): Promise<Machine> {
  const machine = new Machine();
  await machine.load();
  return machine;
}

describe('loading', () => {
  it('reads both documents', async () => {
    const machine = await loaded();
    expect(machine.ready).toBe(true);
    expect(machine.recipes).toHaveLength(1);
    expect(machine.slots).toHaveLength(2);
  });

  it('surfaces a transport failure instead of throwing', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn(async () => {
        throw new TypeError('Failed to fetch');
      })
    );
    const machine = new Machine();
    await machine.load();
    expect(machine.ready).toBe(false);
    expect(machine.loadError).toContain('Failed to fetch');
  });
});

describe('whole-document read-modify-write', () => {
  it('puts the entire library back, not just the changed recipe', async () => {
    const machine = await loaded();
    const recipe = plain(machine.recipes[0]);
    recipe.name = 'Negroni Sbagliato';

    expect(await machine.saveRecipe(recipe)).toBe(true);

    const put = fake.calls.find((call) => call.method === 'PUT');
    expect(put?.url).toBe('/api/recipes');
    expect(put?.body).toEqual({
      version: 1,
      ingredients: library.ingredients,
      recipes: [{ ...library.recipes[0], name: 'Negroni Sbagliato' }]
    });
  });

  it('adopts the document the machine echoes back', async () => {
    const machine = await loaded();
    await machine.saveRecipe({ id: 'spritz', name: 'Spritz', pours: [] });
    expect(machine.recipes.map((recipe) => recipe.id)).toEqual(['negroni', 'spritz']);
    expect(fake.stored.library.recipes).toHaveLength(2);
  });

  it('does not mutate local state until the machine has accepted the write', async () => {
    const machine = await loaded();
    fake.conflictOnce();

    const edited = plain(machine.recipes[0]);
    edited.name = 'Renamed';
    expect(await machine.saveRecipe(edited)).toBe(false);

    expect(machine.recipes[0].name).toBe('Negroni');
    expect(machine.pouring).toBe(true);
    expect(machine.error).toContain('pouring');
  });

  it('serialises overlapping writes so neither is lost', async () => {
    const machine = await loaded();
    const first = machine.updateConfig((draft) => (draft.pin = '9999'));
    const second = machine.updateConfig((draft) => (draft.ring.brightness = 200));
    expect(await Promise.all([first, second])).toEqual([true, true]);

    expect(fake.stored.config.pin).toBe('9999');
    expect(fake.stored.config.ring.brightness).toBe(200);
  });

  it('normalises slots to the pump count before writing', async () => {
    const machine = await loaded();
    await machine.updateConfig((draft) => (draft.pumpCount = 4));

    const put = fake.calls.filter((call) => call.url === '/api/config' && call.method === 'PUT');
    const body = put.at(-1)?.body as Config;
    expect(body.slots.map((slot) => slot.index)).toEqual([0, 1, 2, 3]);
  });

  it('drops the retired calibration array a stored document still carries', async () => {
    fake.stored.config = {
      ...structuredClone(config),
      calibration: [{ channel: 0, mlPerSecond: 9 }]
    } as Config;
    const machine = await loaded();
    await machine.updateConfig((draft) => (draft.pin = '4321'));

    const put = fake.calls.filter((call) => call.url === '/api/config' && call.method === 'PUT');
    expect(put.at(-1)?.body).not.toHaveProperty('calibration');
  });
});

describe('artwork', () => {
  it('posts the raw JPEG then records the path in the same save', async () => {
    const machine = await loaded();
    const jpeg = new Blob([new Uint8Array([0xff, 0xd8, 0xff])], { type: 'image/jpeg' });

    expect(await machine.saveRecipe(plain(machine.recipes[0]), jpeg)).toBe(true);

    const post = fake.calls.find((call) => call.method === 'POST');
    expect(post?.url).toBe('/api/assets/negroni');
    expect(post?.contentType).toBe('image/jpeg');
    expect(post?.body).toBe(jpeg);
    expect(machine.recipes[0].image).toBe('/assets/negroni.jpeg');
  });

  it('refuses an upload larger than the firmware accepts without calling the machine', async () => {
    const machine = await loaded();
    const before = fake.calls.length;
    const huge = new Blob([new Uint8Array(600 * 1024)], { type: 'image/jpeg' });

    expect(await machine.saveRecipe(plain(machine.recipes[0]), huge)).toBe(false);
    expect(fake.calls).toHaveLength(before);
    expect(machine.error).toContain('too large');
  });

  it('leaves the recipe alone when the asset upload is refused mid-pour', async () => {
    const machine = await loaded();
    fake.conflictOnce();
    const jpeg = new Blob([new Uint8Array([0xff, 0xd8])], { type: 'image/jpeg' });

    expect(await machine.saveRecipe(plain(machine.recipes[0]), jpeg)).toBe(false);
    expect(machine.recipes[0].image).toBeUndefined();
    expect(fake.calls.some((call) => call.method === 'PUT')).toBe(false);
  });
});

describe('deleting an ingredient', () => {
  it('drops its pours and frees the slot it occupied', async () => {
    const machine = await loaded();
    expect(await machine.removeIngredient('campari')).toBe(true);

    expect(machine.ingredients.map((ingredient) => ingredient.id)).toEqual(['gin']);
    expect(machine.recipes[0].pours).toEqual([{ ingredient: 'gin', ml: 30 }]);
    expect(fake.stored.config.slots[1]).toEqual({ index: 1, ingredient: null, empty: false });
  });

  it('leaves the configuration alone when the ingredient was in no slot', async () => {
    const machine = await loaded();
    await machine.saveIngredient({ id: 'soda', name: 'Soda', colour: '#FFFFFF', mlPerSecond: 6 });
    const before = fake.calls.filter((call) => call.url === '/api/config').length;

    await machine.removeIngredient('soda');
    expect(fake.calls.filter((call) => call.url === '/api/config')).toHaveLength(before);
  });

  it('deletes the stored jpeg when artwork is cleared from a recipe', async () => {
    const machine = await loaded();
    const jpeg = new Blob([new Uint8Array([0xff, 0xd8, 0xff])], { type: 'image/jpeg' });
    await machine.saveRecipe(plain(machine.recipes[0]), jpeg);

    const cleared = plain(machine.recipes[0]);
    delete cleared.image;
    expect(await machine.saveRecipe(cleared)).toBe(true);

    const call = fake.calls.find((c) => c.method === 'DELETE');
    expect(call?.url).toBe('/api/assets/negroni');
    expect(machine.recipes[0].image).toBeUndefined();
  });

  it('leaves the jpeg alone when a recipe is saved without touching its artwork', async () => {
    const machine = await loaded();
    const jpeg = new Blob([new Uint8Array([0xff, 0xd8, 0xff])], { type: 'image/jpeg' });
    await machine.saveRecipe(plain(machine.recipes[0]), jpeg);

    const renamed = { ...plain(machine.recipes[0]), name: 'Renamed' };
    expect(await machine.saveRecipe(renamed)).toBe(true);

    expect(fake.calls.some((c) => c.method === 'DELETE')).toBe(false);
    expect(machine.recipes[0].image).toBe('/assets/negroni.jpeg');
  });

  it('deletes the jpeg when the whole recipe is removed', async () => {
    const machine = await loaded();
    const jpeg = new Blob([new Uint8Array([0xff, 0xd8, 0xff])], { type: 'image/jpeg' });
    await machine.saveRecipe(plain(machine.recipes[0]), jpeg);

    expect(await machine.removeRecipe('negroni')).toBe(true);

    const call = fake.calls.find((c) => c.method === 'DELETE');
    expect(call?.url).toBe('/api/assets/negroni');
  });

  it('does not call delete when removing a recipe that never had artwork', async () => {
    const machine = await loaded();
    expect(await machine.removeRecipe('negroni')).toBe(true);
    expect(fake.calls.some((c) => c.method === 'DELETE')).toBe(false);
  });
});
