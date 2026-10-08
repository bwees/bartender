#include "artwork.h"

#include <cstdint>

namespace hmi {

Rect coverCrop(int sourceWidth, int sourceHeight, int boxWidth, int boxHeight) {
  if (sourceWidth <= 0 || sourceHeight <= 0 || boxWidth <= 0 || boxHeight <= 0) return {};

  Rect crop;
  const bool sourceIsWider =
      (int64_t)sourceWidth * boxHeight > (int64_t)boxWidth * sourceHeight;
  if (sourceIsWider) {
    crop.height = sourceHeight;
    crop.width = (int)((int64_t)sourceHeight * boxWidth / boxHeight);
  } else {
    crop.width = sourceWidth;
    crop.height = (int)((int64_t)sourceWidth * boxHeight / boxWidth);
  }
  if (crop.width < 1) crop.width = 1;
  if (crop.height < 1) crop.height = 1;
  if (crop.width > sourceWidth) crop.width = sourceWidth;
  if (crop.height > sourceHeight) crop.height = sourceHeight;

  crop.x = (sourceWidth - crop.width) / 2;
  crop.y = (sourceHeight - crop.height) / 2;
  return crop;
}

}  // namespace hmi
