/*
 * haption_device.cpp - Haption Virtuose device backend implementation
 *
 * Uses the VirtuoseAPI (C interface) to connect to a Haption device
 * via the SvcHaptic service over Ethernet.  The device runs in
 * COMMAND_TYPE_IMPEDANCE mode with zero force (transparent / passive)
 * so we can read position without commanding any force feedback.
 *
 * The Virtuose API returns a 7-float pose:
 *   [tx, ty, tz, qx, qy, qz, qw]   (metres, unit quaternion)
 *
 * We convert this to:
 *   - a 4x4 column-major rotation matrix  (matching OpenGL / OpenHaptics)
 *   - a 3-component position in millimetres (matching OpenHaptics workspace)
 */

#ifdef HAVE_HAPTION

#include "haption_device.h"

#include <cmath>
#include <cstring>
#include <iostream>

extern "C" {
#include "virtuoseAPI.h"
}

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static VirtContext g_vc = nullptr;

// ---------------------------------------------------------------------------
// Quaternion  →  4×4 column-major matrix  (rotation only, no scale)
// ---------------------------------------------------------------------------
static void quatToMatrix(float qx, float qy, float qz, float qw, double m[16])
{
    // Normalise (defensive)
    const double len = std::sqrt(
        static_cast<double>(qx)*qx + static_cast<double>(qy)*qy +
        static_cast<double>(qz)*qz + static_cast<double>(qw)*qw);
    const double inv = (len > 1e-12) ? (1.0 / len) : 1.0;

    const double x = qx * inv;
    const double y = qy * inv;
    const double z = qz * inv;
    const double w = qw * inv;

    const double xx = x * x, yy = y * y, zz = z * z;
    const double xy = x * y, xz = x * z, yz = y * z;
    const double wx = w * x, wy = w * y, wz = w * z;

    // Column 0
    m[0]  = 1.0 - 2.0*(yy + zz);
    m[1]  =       2.0*(xy + wz);
    m[2]  =       2.0*(xz - wy);
    m[3]  = 0.0;

    // Column 1
    m[4]  =       2.0*(xy - wz);
    m[5]  = 1.0 - 2.0*(xx + zz);
    m[6]  =       2.0*(yz + wx);
    m[7]  = 0.0;

    // Column 2
    m[8]  =       2.0*(xz + wy);
    m[9]  =       2.0*(yz - wx);
    m[10] = 1.0 - 2.0*(xx + yy);
    m[11] = 0.0;

    // Column 3 (translation filled in by caller)
    m[12] = 0.0;
    m[13] = 0.0;
    m[14] = 0.0;
    m[15] = 1.0;
}

// ---------------------------------------------------------------------------
// Periodic haptic callback (runs at ~333 Hz inside the API's RT thread)
// ---------------------------------------------------------------------------
static void hapticCallback(VirtContext vc, void* /*userData*/)
{
    // Send zero force/torque to keep the device transparent (free-moving)
    float zeroForce[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    virtSetForce(vc, zeroForce);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool haption_init(const char* connectionString)
{
    std::cerr << "[Haption] Connecting to " << connectionString << " ..." << std::endl;

    g_vc = virtOpen(connectionString);
    if (g_vc == nullptr) {
        std::cerr << "[Haption] virtOpen failed." << std::endl;
        return false;
    }

    // Print version info
    int apiMajor = 0, apiMinor = 0;
    virtAPIVersion(&apiMajor, &apiMinor);
    std::cerr << "[Haption] VirtuoseAPI version: " << apiMajor << "." << apiMinor << std::endl;

    int ctrlMajor = 0, ctrlMinor = 0;
    virtGetControlerVersion(g_vc, &ctrlMajor, &ctrlMinor);
    std::cerr << "[Haption] Controller version: " << ctrlMajor << "." << ctrlMinor << std::endl;

    int devType = 0, serialNum = 0;
    if (virtGetDeviceID(g_vc, &devType, &serialNum) == 0) {
        std::cerr << "[Haption] Device type: " << devType
                  << "  Serial: " << serialNum << std::endl;
    }

    // Configure impedance mode (force/position) — we send zero force
    if (virtSetCommandType(g_vc, COMMAND_TYPE_IMPEDANCE) != 0) {
        std::cerr << "[Haption] Failed to set command type: "
                  << virtGetErrorMessage(virtGetErrorCode(g_vc)) << std::endl;
        virtClose(g_vc);
        g_vc = nullptr;
        return false;
    }

    // 3 ms time step (typical haptic rate ~333 Hz)
    virtSetTimeStep(g_vc, 0.003f);

    // Allow re-indexing on all axes while deadman is released
    virtSetIndexingMode(g_vc, INDEXING_ALL_FORCE_FEEDBACK_INHIBITION);

    // Register the periodic haptic callback (required before virtStartLoop)
    float timeStep = 0.003f;  // 3 ms → ~333 Hz
    if (virtSetPeriodicFunction(g_vc, hapticCallback, &timeStep, nullptr) != 0) {
        std::cerr << "[Haption] Failed to set periodic function: "
                  << virtGetErrorMessage(virtGetErrorCode(g_vc)) << std::endl;
        virtClose(g_vc);
        g_vc = nullptr;
        return false;
    }

    // Power on
    if (virtSetPowerOn(g_vc, 1) != 0) {
        std::cerr << "[Haption] Failed to power on: "
                  << virtGetErrorMessage(virtGetErrorCode(g_vc)) << std::endl;
        virtClose(g_vc);
        g_vc = nullptr;
        return false;
    }

    // Start the haptic loop
    if (virtStartLoop(g_vc) != 0) {
        std::cerr << "[Haption] Failed to start loop: "
                  << virtGetErrorMessage(virtGetErrorCode(g_vc)) << std::endl;
        virtSetPowerOn(g_vc, 0);
        virtClose(g_vc);
        g_vc = nullptr;
        return false;
    }

    std::cerr << "[Haption] Device initialised successfully." << std::endl;
    return true;
}

bool haption_read_state(double outTransform[16], double outPosition[3])
{
    if (g_vc == nullptr) {
        return false;
    }

    // pose = [tx, ty, tz, qx, qy, qz, qw]   (metres, quaternion)
    float pose[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    if (virtGetPosition(g_vc, pose) != 0) {
        return false;
    }

    // Convert quaternion to 4x4 rotation matrix (column-major)
    quatToMatrix(pose[3], pose[4], pose[5], pose[6], outTransform);

    // Convert translation from metres to millimetres and place in
    // both the matrix column-3 and the position output.
    const double tx_mm = static_cast<double>(pose[0]) * 1000.0;
    const double ty_mm = static_cast<double>(pose[1]) * 1000.0;
    const double tz_mm = static_cast<double>(pose[2]) * 1000.0;

    outTransform[12] = tx_mm;
    outTransform[13] = ty_mm;
    outTransform[14] = tz_mm;

    outPosition[0] = tx_mm;
    outPosition[1] = ty_mm;
    outPosition[2] = tz_mm;

    return true;
}

void haption_shutdown()
{
    if (g_vc == nullptr) {
        return;
    }

    std::cerr << "[Haption] Shutting down..." << std::endl;

    virtSetPowerOn(g_vc, 0);
    virtStopLoop(g_vc);
    virtClose(g_vc);
    g_vc = nullptr;

    std::cerr << "[Haption] Done." << std::endl;
}

#endif // HAVE_HAPTION
