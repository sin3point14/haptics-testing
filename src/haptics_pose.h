#pragma once

#ifdef _WIN32
#  define HAPTICS_API __declspec(dllexport)
#else
#  define HAPTICS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

HAPTICS_API int haptics_open(const char* backend, const char* device_spec);
HAPTICS_API int haptics_get_pose(double out_transform[16], double out_position[3]);
HAPTICS_API void haptics_close(void);
HAPTICS_API const char* haptics_last_error(void);

#ifdef __cplusplus
}
#endif