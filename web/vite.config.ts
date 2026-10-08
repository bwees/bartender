import { svelteTesting } from '@testing-library/svelte/vite';
import { sveltekit } from '@sveltejs/kit/vite';
import tailwindcss from '@tailwindcss/vite';
import { defineConfig } from 'vitest/config';

const machine = `http://${process.env.BARTENDER_HOST ?? '192.168.4.1'}`;

export default defineConfig({
  plugins: [tailwindcss(), sveltekit(), ...(process.env.VITEST ? [svelteTesting()] : [])],
  server: {
    proxy: {
      '/api': { target: machine, changeOrigin: true },
      '/assets': { target: machine, changeOrigin: true }
    }
  },
  resolve: process.env.VITEST ? { conditions: ['browser'] } : {},
  test: {
    environment: 'jsdom',
    include: ['src/**/*.test.{ts,svelte.ts}'],
    setupFiles: ['src/tests/setup.ts'],
    globals: true
  }
});
