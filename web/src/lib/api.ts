import type { Config, Library, Uuid } from './types';

export const MAX_ASSET_BYTES = 512 * 1024;

const statusMessages: Record<number, string> = {
  400: 'The machine rejected the document as invalid.',
  405: 'The machine does not allow that request.',
  409: 'The machine is pouring a drink and will not accept changes until it finishes.',
  413: 'That upload is too large for the machine.',
  415: 'The machine only accepts baseline JPEG images.',
  500: 'The machine could not write to its SD card.'
};

export class ApiError extends Error {
  readonly status: number;
  readonly detail: string;

  constructor(status: number, detail: string) {
    super(statusMessages[status] ?? `The machine returned HTTP ${status}.`);
    this.name = 'ApiError';
    this.status = status;
    this.detail = detail;
  }

  get pouring(): boolean {
    return this.status === 409;
  }
}

export function describe(error: unknown): string {
  if (error instanceof ApiError) return error.message;
  if (error instanceof Error && error.message)
    return `Could not reach the machine: ${error.message}`;
  return 'Could not reach the machine.';
}

async function fail(response: Response): Promise<never> {
  let detail = '';
  try {
    const body = await response.json();
    if (body && typeof body.error === 'string') detail = body.error;
  } catch {
    detail = '';
  }
  throw new ApiError(response.status, detail);
}

async function getJson<T>(path: string): Promise<T> {
  const response = await fetch(path, { headers: { accept: 'application/json' } });
  if (!response.ok) await fail(response);
  return (await response.json()) as T;
}

async function putJson<T>(path: string, document: T): Promise<T> {
  const response = await fetch(path, {
    method: 'PUT',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(document)
  });
  if (!response.ok) await fail(response);
  return (await response.json()) as T;
}

export const getLibrary = () => getJson<Library>('/api/recipes');
export const putLibrary = (library: Library) => putJson<Library>('/api/recipes', library);
export const getConfig = () => getJson<Config>('/api/config');
export const putConfig = (config: Config) => putJson<Config>('/api/config', config);

export async function deleteAsset(id: Uuid): Promise<void> {
  const response = await fetch(`/api/assets/${id}`, { method: 'DELETE' });
  if (!response.ok) await fail(response);
}

export async function putAsset(id: Uuid, jpeg: Blob): Promise<void> {
  if (jpeg.size > MAX_ASSET_BYTES) {
    throw new ApiError(413, `${Math.round(jpeg.size / 1024)} KiB`);
  }
  const response = await fetch(`/api/assets/${id}`, {
    method: 'POST',
    headers: { 'content-type': 'image/jpeg' },
    body: jpeg
  });
  if (!response.ok) await fail(response);
}
