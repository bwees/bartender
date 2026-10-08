#pragma once

namespace hmi {

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// The part of a source image that fills a box of a different shape. Drink art is
// uploaded square and shown in three different aspect ratios, and the alternative
// to cropping is black bars down the side of every tile.
Rect coverCrop(int sourceWidth, int sourceHeight, int boxWidth, int boxHeight);

}  // namespace hmi
