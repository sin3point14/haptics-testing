#include "haptics_pose.h"

#include <cmath>
#include <cstring>
#include <string>

#ifdef HAVE_OPENHAPTICS
#include <HD/hd.h>
#include <HDU/hduError.h>
#endif

#ifdef HAVE_HAPTION
#include "haption_device.h"
#endif

namespace {

enum class Backend {
    None,
#ifdef HAVE_OPENHAPTICS
    OpenHaptics,
#endif
#ifdef HAVE_HAPTION
    Haption,
#endif
};

Backend g_backend = Backend::None;

#ifdef HAVE_OPENHAPTICS
HHD g_device = HD_INVALID_HANDLE;
#endif

std::string g_lastError;

void set_error(const char* message) {
    g_lastError = message ? message : "";
}

bool string_equals(const char* value, const char* expected) {
    return value != nullptr && std::strcmp(value, expected) == 0;
}

void identity(double out_transform[16]) {
    out_transform[0] = 1.0;  out_transform[4] = 0.0;  out_transform[8] = 0.0;  out_transform[12] = 0.0;
    out_transform[1] = 0.0;  out_transform[5] = 1.0;  out_transform[9] = 0.0;  out_transform[13] = 0.0;
    out_transform[2] = 0.0;  out_transform[6] = 0.0;  out_transform[10] = 1.0; out_transform[14] = 0.0;
    out_transform[3] = 0.0;  out_transform[7] = 0.0;  out_transform[11] = 0.0; out_transform[15] = 1.0;
}

#ifdef HAVE_OPENHAPTICS
struct PoseSample {
    double transform[16];
    double position[3];
    bool ok;
};

HDCallbackCode HDCALLBACK copyPoseCallback(void* userData) {
    PoseSample* sample = static_cast<PoseSample*>(userData);
    if (sample == nullptr || g_device == HD_INVALID_HANDLE) {
        return HD_CALLBACK_DONE;
    }

    sample->ok = false;
    hdBeginFrame(g_device);
    HDdouble pos[3], xform[16];
    hdGetDoublev(HD_CURRENT_POSITION, pos);
    hdGetDoublev(HD_CURRENT_TRANSFORM, xform);
    hdEndFrame(g_device);

    const HDErrorInfo err = hdGetError();
    if (!HD_DEVICE_ERROR(err)) {
        for (int i = 0; i < 16; ++i) sample->transform[i] = xform[i];
        for (int i = 0; i < 3; ++i) sample->position[i] = pos[i];
        sample->ok = true;
    }
    return HD_CALLBACK_DONE;
}
#endif

} // namespace

int haptics_open(const char* backend, const char* device_spec) {
    haptics_close();
    g_lastError.clear();

    const char* requested = backend ? backend : "openhaptics";

#ifdef HAVE_OPENHAPTICS
    if (string_equals(requested, "openhaptics") || string_equals(requested, "geomagic")) {
        const char* device_name = (device_spec != nullptr && device_spec[0] != '\0') ? device_spec : HD_DEFAULT_DEVICE;
        HDErrorInfo error;
        g_device = hdInitDevice(device_name);
        if (HD_DEVICE_ERROR(error = hdGetError())) {
            set_error(hdGetErrorString(error.errorCode));
            g_device = HD_INVALID_HANDLE;
            return 0;
        }

        hdEnable(HD_FORCE_OUTPUT);
        hdStartScheduler();
        if (HD_DEVICE_ERROR(error = hdGetError())) {
            set_error(hdGetErrorString(error.errorCode));
            hdDisableDevice(g_device);
            g_device = HD_INVALID_HANDLE;
            return 0;
        }

        hdMakeCurrentDevice(g_device);
        g_backend = Backend::OpenHaptics;
        return 1;
    }
#endif

#ifdef HAVE_HAPTION
    if (string_equals(requested, "haption")) {
        if (!haption_init(device_spec != nullptr ? device_spec : "127.0.0.1#5000")) {
            set_error("Failed to initialize Haption backend");
            return 0;
        }
        g_backend = Backend::Haption;
        return 1;
    }
#endif

    set_error("Requested backend is unavailable in this build");
    return 0;
}

int haptics_get_pose(double out_transform[16], double out_position[3]) {
    if (out_transform == nullptr || out_position == nullptr) {
        set_error("Null output buffer");
        return 0;
    }

    switch (g_backend) {
    case Backend::None:
        set_error("Library is not open");
        return 0;

#ifdef HAVE_OPENHAPTICS
    case Backend::OpenHaptics: {
        PoseSample sample = {
            { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 },
            { 0,0,0 },
            false
        };
        hdScheduleSynchronous(copyPoseCallback, &sample, HD_DEFAULT_SCHEDULER_PRIORITY);
        if (!sample.ok) {
            const HDErrorInfo err = hdGetError();
            set_error(HD_DEVICE_ERROR(err) ? hdGetErrorString(err.errorCode) : "Failed to query pose");
            return 0;
        }
        for (int i = 0; i < 16; ++i) out_transform[i] = sample.transform[i];
        for (int i = 0; i < 3; ++i) out_position[i] = sample.position[i];
        return 1;
    }
#endif

#ifdef HAVE_HAPTION
    case Backend::Haption: {
        double xform[16], pos[3];
        if (!haption_read_state(xform, pos)) {
            set_error("Failed to query Haption pose");
            return 0;
        }
        for (int i = 0; i < 16; ++i) out_transform[i] = xform[i];
        for (int i = 0; i < 3; ++i) out_position[i] = pos[i];
        return 1;
    }
#endif

    default:
        set_error("Unsupported backend state");
        return 0;
    }
}

void haptics_close(void) {
    switch (g_backend) {
#ifdef HAVE_OPENHAPTICS
    case Backend::OpenHaptics:
        if (g_device != HD_INVALID_HANDLE) {
            hdStopScheduler();
            hdDisableDevice(g_device);
            g_device = HD_INVALID_HANDLE;
        }
        break;
#endif
#ifdef HAVE_HAPTION
    case Backend::Haption:
        haption_shutdown();
        break;
#endif
    default:
        break;
    }
    g_backend = Backend::None;
}

const char* haptics_last_error(void) {
    return g_lastError.c_str();
}