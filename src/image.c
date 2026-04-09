#include "image.h"

#include "bitmap.h"

#include <math.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>

#ifdef IMV_BACKEND_LIBRSVG
#include <cairo.h>
#endif

struct imv_image {
  enum image_type type;
  union {
    struct imv_bitmap bitmap;
    #ifdef IMV_BACKEND_LIBRSVG
    RsvgHandle *svg;
    #endif
  } val;
};

enum image_type imv_image_get_type(struct imv_image *image) {
  assert(image);
  return image->type;
}

struct imv_image *imv_image_create_from_bitmap(struct imv_bitmap bmp)
{
  struct imv_image *image = calloc(1, sizeof *image);
  image->type = IMV_IMAGE_BITMAP;
  image->val.bitmap = bmp;
  return image;
}

#ifdef IMV_BACKEND_LIBRSVG
struct imv_image *imv_image_create_from_svg(RsvgHandle *handle)
{
  struct imv_image *image = calloc(1, sizeof *image);
  image->type = IMV_IMAGE_SVG;
  image->val.svg = handle;
  return image;
}
#endif

void imv_image_free(struct imv_image *image)
{
  if (!image) {
    return;
  }

  switch (image->type) {
    case IMV_IMAGE_BITMAP:
      imv_bitmap_free(image->val.bitmap);
      break;
#ifdef IMV_BACKEND_LIBRSVG
    case IMV_IMAGE_SVG: {
      g_object_unref(image->val.svg);
      break;
    }
#endif
  }

  free(image);
}

int imv_image_width(const struct imv_image *image)
{
  if (!image) {
    return 0;
  }
  switch (image->type) {
    case IMV_IMAGE_BITMAP:
      return image->val.bitmap.width;
#ifdef IMV_BACKEND_LIBRSVG
    case IMV_IMAGE_SVG: {
      RsvgDimensionData dims;
      rsvg_handle_get_dimensions(image->val.svg, &dims);
      return dims.width;
    }
#endif
  }
  assert(false);
}

int imv_image_height(const struct imv_image *image)
{
  if (!image) {
    return 0;
  }
  switch (image->type) {
    case IMV_IMAGE_BITMAP:
      return image->val.bitmap.height;
#ifdef IMV_BACKEND_LIBRSVG
    case IMV_IMAGE_SVG: {
      RsvgDimensionData dims;
      rsvg_handle_get_dimensions(image->val.svg, &dims);
      return dims.height;
    }
#endif
  }
  assert(false);
}

static unsigned char sample_channel(const unsigned char *src,
    int src_width, int src_height, int stride,
    double fx, double fy, int channel)
{
  if (src_width <= 0 || src_height <= 0) {
    return 0;
  }

  int x0 = (int)floor(fx);
  int y0 = (int)floor(fy);
  int x1 = x0 + 1;
  int y1 = y0 + 1;
  if (x0 < 0) {
    x0 = 0;
  }
  if (y0 < 0) {
    y0 = 0;
  }
  if (x1 >= src_width) {
    x1 = src_width - 1;
  }
  if (y1 >= src_height) {
    y1 = src_height - 1;
  }

  const double tx = fx - floor(fx);
  const double ty = fy - floor(fy);

  const unsigned char c00 = src[y0 * stride + x0 * 4 + channel];
  const unsigned char c10 = src[y0 * stride + x1 * 4 + channel];
  const unsigned char c01 = src[y1 * stride + x0 * 4 + channel];
  const unsigned char c11 = src[y1 * stride + x1 * 4 + channel];

  const double top = c00 + (c10 - c00) * tx;
  const double bottom = c01 + (c11 - c01) * tx;
  const double value = top + (bottom - top) * ty;
  if (value <= 0) {
    return 0;
  }
  if (value >= 255) {
    return 255;
  }
  return (unsigned char)(value + 0.5);
}

static struct imv_image *thumbnail_bitmap(const struct imv_bitmap *bmp,
    int max_width, int max_height)
{
  const int src_width = bmp->width;
  const int src_height = bmp->height;
  if (src_width <= 0 || src_height <= 0 || max_width <= 0 || max_height <= 0) {
    return NULL;
  }

  const double scale_x = (double)max_width / (double)src_width;
  const double scale_y = (double)max_height / (double)src_height;
  const double scale = fmin(1.0, fmin(scale_x, scale_y));
  const int dst_width = (int)fmax(1.0, floor(src_width * scale + 0.5));
  const int dst_height = (int)fmax(1.0, floor(src_height * scale + 0.5));

  struct imv_bitmap thumb = imv_bitmap_alloc(dst_width, dst_height);
  if (!thumb.data) {
    return NULL;
  }

  if (scale == 1.0) {
    memcpy(thumb.data, bmp->data, imv_bitmap_size(thumb));
    return imv_image_create_from_bitmap(thumb);
  }

  for (int y = 0; y < dst_height; ++y) {
    const double src_y = ((double)y + 0.5) * src_height / dst_height - 0.5;
    for (int x = 0; x < dst_width; ++x) {
      const double src_x = ((double)x + 0.5) * src_width / dst_width - 0.5;
      unsigned char *dst = thumb.data + ((size_t)y * (size_t)dst_width + (size_t)x) * 4;
      for (int channel = 0; channel < 4; ++channel) {
        dst[channel] = sample_channel(bmp->data, src_width, src_height,
            src_width * 4, src_x, src_y, channel);
      }
    }
  }

  return imv_image_create_from_bitmap(thumb);
}

#ifdef IMV_BACKEND_LIBRSVG
static unsigned char unpremultiply_channel(unsigned char value,
    unsigned char alpha)
{
  if (alpha == 0 || value == 0) {
    return 0;
  }
  const int result = ((int)value * 255 + alpha / 2) / alpha;
  return result > 255 ? 255 : result;
}

static struct imv_image *thumbnail_svg(RsvgHandle *handle,
    int src_width, int src_height, int max_width, int max_height)
{
  if (src_width <= 0 || src_height <= 0 || max_width <= 0 || max_height <= 0) {
    return NULL;
  }

  const double scale = fmin(1.0, fmin((double)max_width / src_width,
      (double)max_height / src_height));
  const int dst_width = (int)fmax(1.0, floor(src_width * scale + 0.5));
  const int dst_height = (int)fmax(1.0, floor(src_height * scale + 0.5));

  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
      dst_width, dst_height);
  cairo_t *cr = cairo_create(surface);

  cairo_scale(cr, scale, scale);
  rsvg_handle_render_cairo(handle, cr);
  cairo_destroy(cr);
  cairo_surface_flush(surface);

  const unsigned char *src = cairo_image_surface_get_data(surface);
  const int stride = cairo_image_surface_get_stride(surface);

  struct imv_bitmap bmp = imv_bitmap_alloc(dst_width, dst_height);
  if (!bmp.data) {
    cairo_surface_destroy(surface);
    return NULL;
  }

  for (int y = 0; y < dst_height; ++y) {
    const unsigned char *src_row = src + (size_t)y * (size_t)stride;
    unsigned char *dst_row = bmp.data + (size_t)y * (size_t)dst_width * 4;
    for (int x = 0; x < dst_width; ++x) {
      const unsigned char b = src_row[x * 4 + 0];
      const unsigned char g = src_row[x * 4 + 1];
      const unsigned char r = src_row[x * 4 + 2];
      const unsigned char a = src_row[x * 4 + 3];
      dst_row[x * 4 + 0] = unpremultiply_channel(r, a);
      dst_row[x * 4 + 1] = unpremultiply_channel(g, a);
      dst_row[x * 4 + 2] = unpremultiply_channel(b, a);
      dst_row[x * 4 + 3] = a;
    }
  }

  cairo_surface_destroy(surface);
  return imv_image_create_from_bitmap(bmp);
}
#endif

struct imv_image *imv_image_thumbnail(const struct imv_image *image,
    int max_width, int max_height)
{
  if (!image) {
    return NULL;
  }

  const struct imv_bitmap *bmp = imv_image_get_bitmap(image);
  if (bmp) {
    return thumbnail_bitmap(bmp, max_width, max_height);
  }

#ifdef IMV_BACKEND_LIBRSVG
  RsvgHandle *svg = imv_image_get_svg(image);
  if (svg) {
    return thumbnail_svg(svg, imv_image_width(image), imv_image_height(image),
        max_width, max_height);
  }
#endif

  return NULL;
}

/* Non-public functions, only used by imv_canvas */
const struct imv_bitmap *imv_image_get_bitmap(const struct imv_image *image)
{
  assert(image);
  return image->type == IMV_IMAGE_BITMAP ? &image->val.bitmap : NULL;
}

#ifdef IMV_BACKEND_LIBRSVG
RsvgHandle *imv_image_get_svg(const struct imv_image *image)
{
  return image->type == IMV_IMAGE_SVG ? image->val.svg : NULL;
}
#endif

/* vim:set ts=2 sts=2 sw=2 et: */
