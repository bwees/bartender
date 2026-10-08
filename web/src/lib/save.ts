import { toast } from 'svelte-sonner';
import { machine } from './machine.svelte';

export async function commit(work: Promise<boolean>, success?: string): Promise<boolean> {
  const ok = await work;
  if (ok) {
    if (success) toast.success(success);
    return true;
  }
  toast.error(machine.pouring ? 'The machine is pouring' : 'Could not save', {
    description: machine.error ?? undefined,
    duration: 10000
  });
  return false;
}
