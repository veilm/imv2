#include "bitmap.h"
#include "image.h"
#include "thumb_cache.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
  char directory[] = "/tmp/imv-thumb-cache-XXXXXX";
  assert(mkdtemp(directory));
  assert(setenv("XDG_CACHE_HOME", directory, 1) == 0);

  char source[sizeof directory + 16];
  snprintf(source, sizeof source, "%s/source", directory);
  FILE *file = fopen(source, "wb");
  assert(file);
  assert(fwrite("original", 1, 8, file) == 8);
  assert(fclose(file) == 0);

  struct stat before;
  assert(stat(source, &before) == 0);
  struct imv_bitmap bmp = imv_bitmap_alloc(2, 1);
  assert(bmp.data);
  const unsigned char pixels[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  memcpy(bmp.data, pixels, sizeof pixels);
  struct imv_image *image = imv_image_create_from_bitmap(bmp);
  assert(image);
  assert(imv_thumb_cache_read(source, 64) == NULL);
  imv_thumb_cache_write(source, 64, image, &before);

  struct imv_image *cached = imv_thumb_cache_read(source, 64);
  assert(cached);
  const struct imv_bitmap *loaded = imv_image_get_bitmap(cached);
  assert(loaded && loaded->width == 2 && loaded->height == 1);
  assert(memcmp(loaded->data, pixels, sizeof pixels) == 0);
  imv_image_free(cached);
  assert(imv_thumb_cache_read(source, 96) == NULL);

  char cache_dir[sizeof directory + 16];
  snprintf(cache_dir, sizeof cache_dir, "%s/imv2/thumbs", directory);
  DIR *dir = opendir(cache_dir);
  assert(dir);
  struct dirent *entry;
  char cache_file[sizeof cache_dir + sizeof entry->d_name + 1] = {0};
  while ((entry = readdir(dir))) {
    if (entry->d_name[0] != '.') {
      assert(snprintf(cache_file, sizeof cache_file, "%s/%s", cache_dir,
          entry->d_name) < (int)sizeof cache_file);
      break;
    }
  }
  closedir(dir);
  assert(cache_file[0]);
  file = fopen(cache_file, "wb");
  assert(file);
  assert(fputc(0, file) != EOF);
  assert(fclose(file) == 0);
  assert(imv_thumb_cache_read(source, 64) == NULL);
  imv_thumb_cache_write(source, 64, image, &before);
  cached = imv_thumb_cache_read(source, 64);
  assert(cached);
  imv_image_free(cached);

  file = fopen(source, "ab");
  assert(file);
  assert(fputc('!', file) != EOF);
  assert(fclose(file) == 0);
  assert(imv_thumb_cache_read(source, 64) == NULL);
  imv_thumb_cache_write(source, 64, image, &before);
  assert(imv_thumb_cache_read(source, 64) == NULL);

  imv_image_free(image);
  assert(unlink(cache_file) == 0);
  assert(unlink(source) == 0);
  assert(rmdir(cache_dir) == 0);
  snprintf(cache_dir, sizeof cache_dir, "%s/imv2", directory);
  assert(rmdir(cache_dir) == 0);
  assert(rmdir(directory) == 0);
  return 0;
}
