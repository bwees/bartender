import GridIcon from '@lucide/svelte/icons/layout-grid';
import MartiniIcon from '@lucide/svelte/icons/martini';
import CitrusIcon from '@lucide/svelte/icons/citrus';
import SettingsIcon from '@lucide/svelte/icons/settings';

export const navItems = [
  { href: '/', label: 'Recipes', icon: MartiniIcon },
  { href: '/ingredients', label: 'Ingredients', icon: CitrusIcon },
  { href: '/slots', label: 'Slots', icon: GridIcon },
  { href: '/settings', label: 'Settings', icon: SettingsIcon }
];
