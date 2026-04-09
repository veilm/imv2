#include "backend.h"
#include "bitmap.h"
#include "image.h"
#include "log.h"
#include "source.h"
#include "source_private.h"

#include <stdbool.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <webp/demux.h>

struct private {
  WebPData webp_data;
  WebPAnimDecoder *dec;
  WebPAnimInfo anim_info;
  int prev_timestamp;
  bool is_mmaped;
};

static void free_private(void *raw_pvt)
{
  if (raw_pvt == NULL) {
    return;
  }

  struct private *pvt = raw_pvt;

  WebPAnimDecoderDelete(pvt->dec);

  if (pvt->is_mmaped) {
    munmap((void *)pvt->webp_data.bytes, pvt->webp_data.size);
  }

  free(pvt);
}

static void next_frame(void *raw_pvt, struct imv_image **img, int *frametime)
{
  *img = NULL;
  *frametime = 0;

  imv_log(IMV_DEBUG, "libwebp: next_frame or first_frame called\n");

  struct private *pvt = raw_pvt;

  if (!WebPAnimDecoderHasMoreFrames(pvt->dec)) {
    WebPAnimDecoderReset(pvt->dec);
  }

  uint8_t *buf;
  int timestamp;

  if (!WebPAnimDecoderGetNext(pvt->dec, &buf, &timestamp)) {
    imv_log(IMV_ERROR, "libwebp: failed to get next frame\n");
    return;
  }

  *frametime = timestamp - pvt->prev_timestamp;
  pvt->prev_timestamp = timestamp;

  struct imv_bitmap bmp = imv_bitmap_alloc(
      pvt->anim_info.canvas_width, pvt->anim_info.canvas_height);
  if (!bmp.data) {
    return;
  }

  memcpy(bmp.data, buf, imv_bitmap_size(bmp));

  *img = imv_image_create_from_bitmap(bmp);
}

static const struct imv_source_vtable vtable = {
  .load_first_frame = next_frame,
  .load_next_frame = next_frame,
  .free = free_private
};

static enum backend_result open_memory_internal(struct private priv,
                                                struct imv_source **src)
{
  if (priv.webp_data.bytes == NULL) {
    return BACKEND_BAD_PATH;
  }

  WebPAnimDecoderOptions opt;
  if (!WebPAnimDecoderOptionsInit(&opt)) {
    imv_log(IMV_DEBUG, "libwebp: WebPAnimDecoderOptionsInit() failed\n");
    return BACKEND_ERROR;
  }
  opt.color_mode = MODE_RGBA;

  priv.dec = WebPAnimDecoderNew(&priv.webp_data, &opt);
  if (!priv.dec) {
    imv_log(IMV_DEBUG, "libwebp: error interpreting file as webp\n");
    return BACKEND_UNSUPPORTED;
  }

  WebPAnimDecoderGetInfo(priv.dec, &priv.anim_info);

  struct private *pvt = calloc(1, sizeof *pvt);
  *pvt = priv;

  *src = imv_source_create(&vtable, pvt);

  return BACKEND_SUCCESS;
}

static enum backend_result open_memory(void *data, size_t data_len, struct imv_source **src)
{
  imv_log(IMV_DEBUG, "libwebp: open_memory called\n");
  struct private priv = {
    .webp_data = { .bytes = data, .size = data_len },
    .is_mmaped = false
  };
  return open_memory_internal(priv, src);
}

static enum backend_result open_path(const char *path, struct imv_source **src)
{
  imv_log(IMV_DEBUG, "libwebp: open_path(%s)\n", path);

  struct private priv = {0};

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    goto close;
  }

  long data_len = lseek(fd, 0, SEEK_END);
  if (data_len < 0) {
    goto close;
  }
  void *data = mmap(NULL, data_len, PROT_READ, MAP_PRIVATE, fd, 0);
  if (data == NULL || data == MAP_FAILED) {
    imv_log(IMV_ERROR, "libwebp: failed to map file into memory\n");
    goto close;
  }

  priv.webp_data.bytes = data;
  priv.webp_data.size = data_len;
  priv.is_mmaped = true;

close:
  if (fd >= 0)
    close(fd);

  return open_memory_internal(priv, src);
}

const struct imv_backend imv_backend_libwebp = {
  .name = "libwebp",
  .description = "The official WebP implementation.",
  .website = "https://developers.google.com/speed/webp",
  .license = "The Modified BSD License",
  .open_path = &open_path,
  .open_memory = &open_memory,
};
