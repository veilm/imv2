#include "platform.h"
#include <sys/stat.h>
#include <time.h>

time_t imv_time(void) { return time(NULL); }

time_t imv_file_last_modified(const char *path)
{
  struct stat file_info;
  if (!stat(path, &file_info)) {
    return 0;
  }

  return file_info.st_mtim.tv_sec;
}
