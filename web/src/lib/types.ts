export type Uuid = string;

export interface Ingredient {
  id: Uuid;
  name: string;
  colour: string;
  abv?: number;
  mlPerSecond: number;
}

export interface Pour {
  ingredient: Uuid;
  ml: number;
}

export interface Recipe {
  id: Uuid;
  name: string;
  image?: string;
  pours: Pour[];
}

export interface Library {
  version: 1;
  ingredients: Ingredient[];
  recipes: Recipe[];
}

export interface Slot {
  index: number;
  ingredient: Uuid | null;
  empty: boolean;
}

export interface PourConfig {
  concurrency: number;
  pauseWindowSeconds: number;
  watchdogFactor: number;
}

export interface CupSensorConfig {
  enabled: boolean;
  emptyMm: number;
  cupMm: number;
  debounceSamples: number;
}

export interface WifiConfig {
  ssid: string;
  passphrase: string;
}

export interface RingConfig {
  pixels: number;
  brightness: number;
}

export interface Config {
  version: 1;
  slots: Slot[];
  pumpCount: number;
  pour: PourConfig;
  cupSensor: CupSensorConfig;
  wifi: WifiConfig;
  pin: string;
  ring: RingConfig;
}
