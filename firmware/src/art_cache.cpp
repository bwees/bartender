#include "art_cache.h"

#include <SD.h>
#include <esp_heap_caps.h>
#include <src/extra/libs/sjpg/tjpgd.h>

#include <algorithm>
#include <iterator>
#include <list>

#include "artwork.h"
#include "sd_fs.h"

namespace art {
namespace {

// TJpgDec's own recommendation for a baseline decode, and what LVGL's sjpg
// decoder hands it.
constexpr size_t kWorkBytes = 4096;
// Around thirty grid tiles. PSRAM also carries LVGL's two full-screen draw
// buffers, the panel's own, and whatever the portal is holding mid-upload.
constexpr size_t kBudgetBytes = 2 * 1024 * 1024;
// The portal already refuses anything larger, so this only catches art that
// predates it.
constexpr int kMaxSourceWidth = 800;
constexpr int kMaxSourceHeight = 480;
constexpr int kSubBits = 16;

struct Decode {
  File file;
  lv_color_t* pixels = nullptr;
  int width = 0;
};

size_t feed(JDEC* decoder, uint8_t* buffer, size_t length) {
  auto* decode = static_cast<Decode*>(decoder->device);
  SdGuard guard;
  if (buffer) return decode->file.read(buffer, length);
  return decode->file.seek(decode->file.position() + length) ? length : 0;
}

int store(JDEC* decoder, void* bitmap, JRECT* rect) {
  auto* decode = static_cast<Decode*>(decoder->device);
  const auto* rgb = static_cast<const uint8_t*>(bitmap);
  for (int y = rect->top; y <= rect->bottom; y++) {
    lv_color_t* row = decode->pixels + (size_t)y * decode->width;
    for (int x = rect->left; x <= rect->right; x++) {
      row[x] = lv_color_make(rgb[0], rgb[1], rgb[2]);
      rgb += 3;
    }
  }
  return 1;
}

lv_color_t* decodeNative(const std::string& path, int& width, int& height) {
  Decode decode;
  {
    SdGuard guard;
    decode.file = SD.open(path.c_str(), FILE_READ);
  }
  if (!decode.file) return nullptr;

  auto* work = (uint8_t*)heap_caps_malloc(kWorkBytes, MALLOC_CAP_8BIT);
  JDEC decoder;
  bool ok = work != nullptr &&
            jd_prepare(&decoder, feed, work, kWorkBytes, &decode) == JDR_OK &&
            decoder.width <= kMaxSourceWidth && decoder.height <= kMaxSourceHeight;
  if (ok) {
    decode.width = decoder.width;
    decode.pixels = (lv_color_t*)heap_caps_malloc(
        (size_t)decoder.width * decoder.height * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    ok = decode.pixels != nullptr && jd_decomp(&decoder, store, 0) == JDR_OK;
  }
  {
    SdGuard guard;
    decode.file.close();
  }
  heap_caps_free(work);
  if (!ok) {
    heap_caps_free(decode.pixels);
    return nullptr;
  }

  width = decoder.width;
  height = decoder.height;
  return decode.pixels;
}

lv_color_t sampleAt(const lv_color_t* pixels, int width, int height, int32_t x, int32_t y) {
  const int leftColumn = x >> kSubBits;
  const int topRow = y >> kSubBits;
  const int rightColumn = leftColumn + 1 < width ? leftColumn + 1 : leftColumn;
  const int bottomRow = topRow + 1 < height ? topRow + 1 : topRow;
  const uint8_t leftWeight = 255 - (uint8_t)((x >> (kSubBits - 8)) & 0xFF);
  const uint8_t topWeight = 255 - (uint8_t)((y >> (kSubBits - 8)) & 0xFF);

  const lv_color_t* top = pixels + (size_t)topRow * width;
  const lv_color_t* bottom = pixels + (size_t)bottomRow * width;
  return lv_color_mix(lv_color_mix(top[leftColumn], top[rightColumn], leftWeight),
                      lv_color_mix(bottom[leftColumn], bottom[rightColumn], leftWeight),
                      topWeight);
}

int32_t stepFor(int cropExtent, int boxExtent) {
  if (boxExtent < 2) return 0;
  return ((int32_t)(cropExtent - 1) << kSubBits) / (boxExtent - 1);
}

bool decodeInto(lv_img_dsc_t& image, const std::string& path, int width, int height) {
  if (width < 1 || height < 1) return false;

  int nativeWidth = 0;
  int nativeHeight = 0;
  lv_color_t* native = decodeNative(path, nativeWidth, nativeHeight);
  if (!native) return false;

  const size_t bytes = (size_t)width * height * sizeof(lv_color_t);
  auto* pixels = (lv_color_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!pixels) {
    heap_caps_free(native);
    return false;
  }

  const hmi::Rect crop = hmi::coverCrop(nativeWidth, nativeHeight, width, height);
  const int32_t stepX = stepFor(crop.width, width);
  const int32_t stepY = stepFor(crop.height, height);
  for (int row = 0; row < height; row++) {
    const int32_t y = ((int32_t)crop.y << kSubBits) + row * stepY;
    lv_color_t* out = pixels + (size_t)row * width;
    for (int column = 0; column < width; column++)
      out[column] = sampleAt(native, nativeWidth, nativeHeight,
                             ((int32_t)crop.x << kSubBits) + column * stepX, y);
  }
  heap_caps_free(native);

  image.header.always_zero = 0;
  image.header.reserved = 0;
  image.header.cf = LV_IMG_CF_TRUE_COLOR;
  image.header.w = width;
  image.header.h = height;
  image.data_size = bytes;
  image.data = (const uint8_t*)pixels;
  return true;
}

void freePixels(const lv_img_dsc_t& image) { heap_caps_free((void*)image.data); }

struct Entry {
  std::string path;
  // Set by flush(), meaning the bytes behind the path have changed. Nothing hands
  // a stale entry out again, and sweep() is where it is freed.
  bool stale = false;
  // Asked for since the last sweep, which means a widget on the live screen may
  // be drawing from it.
  bool wanted = true;
  lv_img_dsc_t image{};
};

// Most recently asked for at the front, so eviction can work from the back.
std::list<Entry> cache;

bool matches(const Entry& entry, const std::string& path, int width, int height) {
  return !entry.stale && entry.path == path && entry.image.header.w == (uint32_t)width &&
         entry.image.header.h == (uint32_t)height;
}

}  // namespace

const lv_img_dsc_t* cached(const std::string& path, int width, int height) {
  for (auto it = cache.begin(); it != cache.end(); ++it) {
    if (!matches(*it, path, width, height)) continue;
    it->wanted = true;
    cache.splice(cache.begin(), cache, it);
    return &cache.front().image;
  }

  Entry entry;
  entry.path = path;
  if (!decodeInto(entry.image, path, width, height)) return nullptr;
  cache.push_front(std::move(entry));
  return &cache.front().image;
}

const lv_img_dsc_t* owned(const std::string& path, int width, int height) {
  auto* image = new lv_img_dsc_t{};
  if (!decodeInto(*image, path, width, height)) {
    delete image;
    return nullptr;
  }
  return image;
}

void release(const lv_img_dsc_t* image) {
  if (!image) return;
  freePixels(*image);
  delete image;
}

void sweep() {
  size_t bytes = 0;
  for (auto it = cache.begin(); it != cache.end();) {
    if (it->stale) {
      freePixels(it->image);
      it = cache.erase(it);
      continue;
    }
    bytes += it->image.data_size;
    ++it;
  }

  while (bytes > kBudgetBytes) {
    auto victim = std::find_if(cache.rbegin(), cache.rend(),
                               [](const Entry& entry) { return !entry.wanted; });
    if (victim == cache.rend()) break;
    bytes -= victim->image.data_size;
    freePixels(victim->image);
    cache.erase(std::next(victim).base());
  }

  for (auto& entry : cache) entry.wanted = false;
}

void flush() {
  for (auto& entry : cache) entry.stale = true;
}

}  // namespace art
