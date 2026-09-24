#include "thumb_cache.h"

#include "bitmap.h"
#include "image.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Native, versioned format: a cache is disposable and only read by this build. */
struct cache_header {
  uint64_t magic;
  uint64_t dev;
  uint64_t ino;
  int64_t file_size;
  int64_t mtime_sec;
  int64_t mtime_nsec;
  int64_t ctime_sec;
  int64_t ctime_nsec;
  int32_t width;
  int32_t height;
};

#define CACHE_MAGIC UINT64_C(0x494d563254484d01)

static bool source_stat(const char *path, struct stat *st)
{
  return stat(path, st) == 0 && S_ISREG(st->st_mode);
}

static uint64_t path_hash(const char *path, int size)
{
  uint64_t hash = UINT64_C(14695981039346656037);
  for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
    hash = (hash ^ *p) * UINT64_C(1099511628211);
  }
  for (unsigned i = 0; i < sizeof size; ++i) {
    hash = (hash ^ ((unsigned)size >> (i * 8) & 255)) * UINT64_C(1099511628211);
  }
  return hash;
}

static char *cache_path(const char *path, int size, bool create)
{
  const char *base = getenv("XDG_CACHE_HOME");
  char *fallback = NULL;
  if (!base || base[0] != '/') {
    const char *home = getenv("HOME");
    if (!home || home[0] != '/') {
      return NULL;
    }
    fallback = malloc(strlen(home) + sizeof "/.cache");
    if (!fallback) {
      return NULL;
    }
    sprintf(fallback, "%s/.cache", home);
    base = fallback;
  }

  char *dir = malloc(strlen(base) + sizeof "/imv2/thumbs");
  if (!dir) {
    free(fallback);
    return NULL;
  }
  sprintf(dir, "%s/imv2/thumbs", base);
  if (create) {
    char *subdir = strdup(dir);
    if (!subdir) {
      free(dir);
      free(fallback);
      return NULL;
    }
    subdir[strlen(base) + sizeof "/imv2" - 1] = '\0';
    mkdir(base, 0700);
    mkdir(subdir, 0700);
    mkdir(dir, 0700);
    free(subdir);
  }
  char *result = malloc(strlen(dir) + 32);
  if (result) {
    sprintf(result, "%s/%016" PRIx64 ".bin", dir, path_hash(path, size));
  }
  free(dir);
  free(fallback);
  return result;
}

static struct cache_header header_for(struct stat st, int width, int height)
{
  return (struct cache_header){
    .magic = CACHE_MAGIC,
    .dev = (uint64_t)st.st_dev,
    .ino = (uint64_t)st.st_ino,
    .file_size = st.st_size,
    .mtime_sec = st.st_mtim.tv_sec,
    .mtime_nsec = st.st_mtim.tv_nsec,
    .ctime_sec = st.st_ctim.tv_sec,
    .ctime_nsec = st.st_ctim.tv_nsec,
    .width = width,
    .height = height,
  };
}

static bool same_source(struct cache_header cached, struct stat st)
{
  struct cache_header now = header_for(st, cached.width, cached.height);
  return cached.magic == now.magic && cached.dev == now.dev &&
      cached.ino == now.ino && cached.file_size == now.file_size &&
      cached.mtime_sec == now.mtime_sec && cached.mtime_nsec == now.mtime_nsec &&
      cached.ctime_sec == now.ctime_sec && cached.ctime_nsec == now.ctime_nsec;
}

struct imv_image *imv_thumb_cache_read(const char *path, int size)
{
  struct stat st;
  if (!source_stat(path, &st)) {
    return NULL;
  }
  char *name = cache_path(path, size, false);
  if (!name) {
    return NULL;
  }
  FILE *file = fopen(name, "rb");
  free(name);
  if (!file) {
    return NULL;
  }
  struct cache_header header;
  struct imv_image *image = NULL;
  if (fread(&header, sizeof header, 1, file) == 1 &&
      same_source(header, st) && header.width > 0 && header.height > 0 &&
      header.width <= size && header.height <= size) {
    struct imv_bitmap bmp = imv_bitmap_alloc(header.width, header.height);
    if (bmp.data) {
      const size_t bytes = imv_bitmap_size(bmp);
      if (fread(bmp.data, 1, bytes, file) == bytes && fgetc(file) == EOF &&
          source_stat(path, &st) && same_source(header, st)) {
        image = imv_image_create_from_bitmap(bmp);
      } else {
        imv_bitmap_free(bmp);
      }
    }
  }
  fclose(file);
  return image;
}

void imv_thumb_cache_write(const char *path, int size,
    const struct imv_image *image, const struct stat *source)
{
  const struct imv_bitmap *bmp = imv_image_get_bitmap(image);
  struct stat st;
  if (!bmp || !bmp->data || bmp->width <= 0 || bmp->height <= 0 ||
      bmp->width > size || bmp->height > size || !source ||
      !source_stat(path, &st)) {
    return;
  }
  const struct cache_header header = header_for(*source, bmp->width, bmp->height);
  if (!same_source(header, st)) {
    return;
  }
  char *name = cache_path(path, size, true);
  if (!name) {
    return;
  }
  char *temp = malloc(strlen(name) + sizeof ".XXXXXX");
  if (!temp) {
    free(name);
    return;
  }
  sprintf(temp, "%s.XXXXXX", name);
  int fd = mkstemp(temp);
  if (fd >= 0) {
    FILE *file = fdopen(fd, "wb");
    if (file) {
      bool ok = fwrite(&header, sizeof header, 1, file) == 1 &&
          fwrite(bmp->data, 1, imv_bitmap_size(*bmp), file) == imv_bitmap_size(*bmp);
      if (fclose(file) != 0) {
        ok = false;
      }
      struct stat after;
      if (ok && source_stat(path, &after) && same_source(header, after)) {
        rename(temp, name);
      }
    } else {
      close(fd);
    }
    unlink(temp);
  }
  free(temp);
  free(name);
}
