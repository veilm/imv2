#ifndef IMV_PLATFORM_DUMMY_H
#define IMV_PLATFORM_DUMMY_H

#include <time.h>

extern time_t (*mocked_imv_time)(void);
extern time_t (*mocked_imv_file_last_modified)(const char *path);

#endif
