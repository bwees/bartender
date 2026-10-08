import { render } from '@testing-library/svelte';
import { describe, expect, it } from 'vitest';
import RecipeArt from './RecipeArt.svelte';
import type { Ingredient, Recipe } from '$lib/types';

const ingredients: Ingredient[] = [
  { id: 'gin', name: 'Gin', colour: '#94C7C1', mlPerSecond: 4.2 },
  { id: 'campari', name: 'Campari', colour: '#C0392B', mlPerSecond: 3.1 }
];

const recipe: Recipe = {
  id: 'negroni',
  name: 'Negroni',
  pours: [
    { ingredient: 'gin', ml: 30 },
    { ingredient: 'campari', ml: 30 }
  ]
};

describe('RecipeArt', () => {
  it('paints a tile from the ingredient colours when there is no artwork', () => {
    const { container, getByText } = render(RecipeArt, { recipe, ingredients });

    expect(container.querySelector('img')).toBeNull();
    expect(getByText('N')).toBeTruthy();
    expect(container.querySelector('div')?.getAttribute('style')).toContain('rgb(148, 199, 193)');
  });

  it('shows the uploaded preview ahead of the stored path', () => {
    const { container } = render(RecipeArt, {
      recipe: { ...recipe, image: '/assets/negroni.jpeg' },
      ingredients,
      preview: 'blob:local-preview'
    });

    expect(container.querySelector('img')?.getAttribute('src')).toBe('blob:local-preview');
  });

  it('falls back to the tile when the machine cannot serve the stored artwork', async () => {
    const { container, findByText } = render(RecipeArt, {
      recipe: { ...recipe, image: '/assets/negroni.jpeg' },
      ingredients
    });

    const image = container.querySelector('img');
    expect(image).not.toBeNull();
    image?.dispatchEvent(new Event('error'));

    expect(await findByText('N')).toBeTruthy();
  });
});
