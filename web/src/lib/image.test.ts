import { describe, expect, it, vi } from 'vitest';
import {
  ART_BOX,
  QUALITY_LADDER,
  clampOffset,
  coverScale,
  drawCrop,
  encodeJpeg,
  fitWithin
} from './image';

describe('fitWithin', () => {
  it('leaves an image that already fits the panel alone', () => {
    expect(fitWithin({ width: 400, height: 400 })).toEqual({ width: 400, height: 400 });
  });

  it('shrinks a photo to the panel without distorting it', () => {
    expect(fitWithin({ width: 4000, height: 3000 })).toEqual({ width: 640, height: 480 });
  });

  it('respects the wider of the two limits', () => {
    expect(fitWithin({ width: 4000, height: 1000 })).toEqual({ width: 800, height: 200 });
  });

  it('never returns a zero dimension', () => {
    expect(fitWithin({ width: 10000, height: 1 })).toEqual({ width: 800, height: 1 });
  });
});

describe('coverScale', () => {
  it('scales a wide image so the short edge fills the square', () => {
    expect(coverScale({ width: 1000, height: 500 }, { width: 400, height: 400 })).toBe(0.8);
  });

  it('scales a small image up rather than leaving gaps', () => {
    expect(coverScale({ width: 100, height: 200 }, { width: 400, height: 400 })).toBe(4);
  });
});

describe('clampOffset', () => {
  it('allows panning by half the overhang in each direction', () => {
    expect(clampOffset(1000, 600, 400)).toBe(100);
    expect(clampOffset(-1000, 600, 400)).toBe(-100);
    expect(clampOffset(50, 600, 400)).toBe(50);
  });

  it('pins an image that exactly fills the box', () => {
    expect(clampOffset(80, 400, 400)).toBe(0);
  });
});

describe('drawCrop', () => {
  it('centres a cover-scaled image inside the square', () => {
    const context = {
      fillStyle: '',
      fillRect: vi.fn(),
      drawImage: vi.fn()
    } as unknown as CanvasRenderingContext2D;
    const image = { width: 1000, height: 500 } as unknown as CanvasImageSource & {
      width: number;
      height: number;
    };

    drawCrop(context, image, { width: 400, height: 400 }, { zoom: 1, offsetX: 0, offsetY: 0 });

    expect(context.fillRect).toHaveBeenCalledWith(0, 0, 400, 400);
    expect(context.drawImage).toHaveBeenCalledWith(image, -200, 0, 800, 400);
  });
});

function canvasYielding(sizes: number[]) {
  const toBlob = vi.fn((callback: (blob: Blob | null) => void, _type: string, quality: number) => {
    const index = QUALITY_LADDER.indexOf(quality);
    const size = sizes[index] ?? sizes.at(-1) ?? 0;
    callback(new Blob([new Uint8Array(size)], { type: 'image/jpeg' }));
  });
  return { toBlob };
}

describe('encodeJpeg', () => {
  it('stops at the first quality that fits the machine budget', async () => {
    const canvas = canvasYielding([900, 400, 100]);
    const blob = await encodeJpeg(canvas, 500);

    expect(blob.size).toBe(400);
    expect(canvas.toBlob).toHaveBeenCalledTimes(2);
    expect(canvas.toBlob.mock.calls[0][1]).toBe('image/jpeg');
    expect(canvas.toBlob.mock.calls[0][2]).toBe(QUALITY_LADDER[0]);
  });

  it('gives up with a readable message when even the lowest quality is too big', async () => {
    const canvas = canvasYielding(QUALITY_LADDER.map(() => 2048));
    await expect(encodeJpeg(canvas, 1024)).rejects.toThrow('2 KiB after compression');
    expect(canvas.toBlob).toHaveBeenCalledTimes(QUALITY_LADDER.length);
  });

  it('reports a browser that cannot encode at all', async () => {
    const canvas = { toBlob: vi.fn((callback: (blob: Blob | null) => void) => callback(null)) };
    await expect(encodeJpeg(canvas, 1024)).rejects.toThrow('could not encode');
  });
});

describe('the artwork box', () => {
  it('never exceeds what the embedded decoder accepts', () => {
    expect(ART_BOX.width).toBeLessThanOrEqual(800);
    expect(ART_BOX.height).toBeLessThanOrEqual(480);
  });
});
