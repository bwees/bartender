import { render } from '@testing-library/svelte';
import { createRawSnippet, tick } from 'svelte';
import { afterEach, describe, expect, it, vi } from 'vitest';
import Layout from './+layout.svelte';
import { machine } from '$lib/machine.svelte';
import { navItems } from '$lib/nav';

const children = createRawSnippet(() => ({ render: () => '<p>the page</p>' }));

afterEach(() => {
  vi.unstubAllGlobals();
  machine.library = null;
  machine.config = null;
  machine.loadError = null;
});

describe('the shell', () => {
  it('offers every screen twice — a sidebar on a laptop, a tab bar on a phone', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn(async () => new Response('{}', { status: 500 }))
    );
    const { getAllByText } = render(Layout, { props: { children } });
    await tick();

    for (const item of navItems) {
      expect(getAllByText(item.label)).toHaveLength(2);
    }
  });

  it('explains an unreachable machine instead of showing an empty page', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn(async () => {
        throw new TypeError('Failed to fetch');
      })
    );
    const { findByText, queryByText } = render(Layout, { props: { children } });

    expect(await findByText(/Failed to fetch/)).toBeTruthy();
    expect(await findByText('Try again')).toBeTruthy();
    expect(queryByText('the page')).toBeNull();
  });
});
