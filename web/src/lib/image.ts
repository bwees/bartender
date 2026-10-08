export const SCREEN_WIDTH = 800;
export const SCREEN_HEIGHT = 480;
export const ART_SIZE = 400;
export const QUALITY_LADDER = [0.85, 0.72, 0.6, 0.48, 0.36];

export interface Size {
  width: number;
  height: number;
}

export function fitWithin(size: Size, maxWidth = SCREEN_WIDTH, maxHeight = SCREEN_HEIGHT): Size {
  const scale = Math.min(1, maxWidth / size.width, maxHeight / size.height);
  return {
    width: Math.max(1, Math.floor(size.width * scale)),
    height: Math.max(1, Math.floor(size.height * scale))
  };
}

export const ART_BOX = fitWithin({ width: ART_SIZE, height: ART_SIZE });

export function coverScale(image: Size, box: Size): number {
  return Math.max(box.width / image.width, box.height / image.height);
}

export function clampOffset(offset: number, drawnExtent: number, boxExtent: number): number {
  const slack = Math.max(0, drawnExtent - boxExtent) / 2;
  return Math.min(slack, Math.max(-slack, offset));
}

export interface CropTransform {
  zoom: number;
  offsetX: number;
  offsetY: number;
}

export function drawCrop(
  context: CanvasRenderingContext2D,
  image: CanvasImageSource & Size,
  box: Size,
  transform: CropTransform
): void {
  const scale = coverScale(image, box) * transform.zoom;
  const width = image.width * scale;
  const height = image.height * scale;
  const x = (box.width - width) / 2 + clampOffset(transform.offsetX, width, box.width);
  const y = (box.height - height) / 2 + clampOffset(transform.offsetY, height, box.height);
  context.fillStyle = '#000000';
  context.fillRect(0, 0, box.width, box.height);
  context.drawImage(image, x, y, width, height);
}

export interface JpegSource {
  toBlob(callback: (blob: Blob | null) => void, type: string, quality: number): void;
}

export async function encodeJpeg(source: JpegSource, maxBytes: number): Promise<Blob> {
  let smallest: Blob | null = null;
  for (const quality of QUALITY_LADDER) {
    const blob = await new Promise<Blob | null>((resolve) =>
      source.toBlob(resolve, 'image/jpeg', quality)
    );
    if (!blob) continue;
    smallest = blob;
    if (blob.size <= maxBytes) return blob;
  }
  if (!smallest) throw new Error('The browser could not encode that image as a JPEG.');
  throw new Error(
    `That image is still ${Math.round(smallest.size / 1024)} KiB after compression. Try a simpler picture.`
  );
}

export function loadImage(file: Blob): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(file);
    const image = new Image();
    image.onload = () => {
      URL.revokeObjectURL(url);
      resolve(image);
    };
    image.onerror = () => {
      URL.revokeObjectURL(url);
      reject(new Error('That file is not an image the browser can read.'));
    };
    image.src = url;
  });
}
