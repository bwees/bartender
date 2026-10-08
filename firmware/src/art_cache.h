#pragma once

#include <lvgl.h>

#include <string>

// Drink art is one square baseline JPEG per drink on the card, and every place it
// appears on screen wants different pixels. LVGL 8 has no scaler cheap enough to
// run per frame, so each context gets its own buffer decoded at exactly its size.
namespace art {

// Survives a screen swap, so returning to the grid does not decode it again.
// Null whenever the card holds nothing readable at that path.
const lv_img_dsc_t* cached(const std::string& path, int width, int height);

// Outside the cache and owned by the caller. Full-bleed attract art is 750 KiB a
// drink, which is affordable for the one on screen and not for a shelf of them.
const lv_img_dsc_t* owned(const std::string& path, int width, int height);
void release(const lv_img_dsc_t* image);

// Trims the cache to its budget. Call it once the outgoing screen has been freed,
// the only moment a cached buffer is certainly not bound to a widget.
void sweep();

// Marks every cached buffer for replacement, for when a portal upload puts new
// bytes behind a path already in hand. Frees nothing: the screen showing it lives.
void flush();

}  // namespace art
