#include "platform.h"
#include "platform_dummy.h"

time_t (*mocked_imv_time)(void);
time_t (*mocked_imv_file_last_modified)(const char *path);

time_t imv_time(void) { return mocked_imv_time ? mocked_imv_time() : 0; }

time_t imv_file_last_modified(const char *path)
{
  return mocked_imv_time ? mocked_imv_file_last_modified(path) : 0;
}
