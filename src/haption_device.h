#pragma once

/*
 * haption_device.h - Haption Virtuose device backend
 *
 * Provides init/read/shutdown functions that match the same data
 * layout used by the OpenHaptics backend (4x4 column-major transform
 * matrix + 3-component position in mm).
 */

#ifdef HAVE_HAPTION

/// Initialise the Haption device.
/// @param connectionString  e.g. "192.168.100.53:5000"
/// @return true on success
bool haption_init(const char* connectionString);

/// Read the current pose from the Haption device.
/// @param outTransform  16-element column-major 4x4 matrix (OpenGL order)
/// @param outPosition   3-element position vector (mm)
/// @return true on success
bool haption_read_state(double outTransform[16], double outPosition[3]);

/// Shut down the Haption device cleanly.
void haption_shutdown();

#endif // HAVE_HAPTION
