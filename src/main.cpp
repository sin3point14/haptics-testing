#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

#ifdef HAVE_OPENHAPTICS
#include <HD/hd.h>
#include <HDU/hduError.h>
#endif

#ifdef HAVE_HAPTION
#include "haption_device.h"
#endif

#include <GL/glut.h>

// ===================================================================
// Backend selection
// ===================================================================
enum class DeviceBackend {
    None,
#ifdef HAVE_OPENHAPTICS
    OpenHaptics,
#endif
#ifdef HAVE_HAPTION
    Haption,
#endif
};

#if defined(HAVE_OPENHAPTICS)
static DeviceBackend g_backend = DeviceBackend::OpenHaptics;
#elif defined(HAVE_HAPTION)
static DeviceBackend g_backend = DeviceBackend::Haption;
#else
static DeviceBackend g_backend = DeviceBackend::None;
#endif

// ===================================================================
// Haption connection string (default from SvcHaptic conf)
// ===================================================================
#ifdef HAVE_HAPTION
static std::string g_haptionAddr = "127.0.0.1#5000";
#endif

// ===================================================================
// OpenHaptics state
// ===================================================================
#ifdef HAVE_OPENHAPTICS
static HHD g_hDevice = HD_INVALID_HANDLE;
#endif

// ===================================================================
// Shared device state (both backends write into these)
// ===================================================================
static double g_deviceTransform[16] = {
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0
};
static double g_position[3] = {0.0, 0.0, 0.0};

const float g_blueOffsetLocal[3] = {96.893693035, 8.819358212, 138.764763464};

int g_windowWidth = 1024;
int g_windowHeight = 768;

const float g_groundY = -120.0f;
const float g_lightDir[3] = {-0.00f, -1.0f, -0.00f};

// ===================================================================
// Capture / logging (unchanged)
// ===================================================================
struct PoseSample {
    double transform[16];
    double position[3];
    bool ok;
};

enum class CaptureState {
    Idle,
    Waiting,
    Logging,
    Done
};

CaptureState g_captureState = CaptureState::Idle;
int g_waitStartMs = 0;
int g_logStartMs = 0;

void logPose() {
    std::cout << std::fixed << std::setprecision(6)
              << g_position[0] << ',' << g_position[1] << ',' << g_position[2] << '\n'
              << g_deviceTransform[0] << ',' << g_deviceTransform[4] << ',' << g_deviceTransform[8] << '\n'
              << g_deviceTransform[1] << ',' << g_deviceTransform[5] << ',' << g_deviceTransform[9] << '\n'
              << g_deviceTransform[2] << ',' << g_deviceTransform[6] << ',' << g_deviceTransform[10] << '\n';
}

// ===================================================================
// Drawing helpers (unchanged)
// ===================================================================
void drawBox(float size) {
    glutSolidCube(size);
}

void drawGroundPlane() {
    const float half = 350.0f;
    glColor3f(0.46f, 0.49f, 0.52f);
    glBegin(GL_QUADS);
    glVertex3f(-half, g_groundY, -half);
    glVertex3f(half, g_groundY, -half);
    glVertex3f(half, g_groundY, half);
    glVertex3f(-half, g_groundY, half);
    glEnd();

    glColor3f(0.36f, 0.39f, 0.42f);
    glBegin(GL_LINES);
    for (float x = -half; x <= half; x += 35.0f) {
        glVertex3f(x, g_groundY + 0.01f, -half);
        glVertex3f(x, g_groundY + 0.01f, half);
    }
    for (float z = -half; z <= half; z += 35.0f) {
        glVertex3f(-half, g_groundY + 0.01f, z);
        glVertex3f(half, g_groundY + 0.01f, z);
    }
    glEnd();
}

void computeDirectionalShadowMatrix(const float plane[4], const float light[4], float out[16]) {
    const float dot = plane[0] * light[0] + plane[1] * light[1] + plane[2] * light[2] + plane[3] * light[3];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[c * 4 + r] = ((r == c) ? dot : 0.0f) - light[r] * plane[c];
        }
    }
}

void drawBoxPair() {
    glPushMatrix();
    glMultMatrixd(g_deviceTransform);
    glColor3f(1.0f, 0.0f, 0.0f);
    drawBox(4.0f);

    glPushMatrix();
    glTranslatef(g_blueOffsetLocal[0], g_blueOffsetLocal[1], g_blueOffsetLocal[2]);
    glColor3f(0.0f, 0.0f, 1.0f);
    drawBox(4.0f);
    glPopMatrix();

    glPopMatrix();
}

void transformPoint(const double m[16], const float p[3], float out[3]) {
    out[0] = static_cast<float>(m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]);
    out[1] = static_cast<float>(m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]);
    out[2] = static_cast<float>(m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]);
}

void drawShadowQuadAt(const float worldPos[3], float radius) {
    if (std::fabs(g_lightDir[1]) < 1e-5f) {
        return;
    }

    const float t = (g_groundY + 0.05f - worldPos[1]) / g_lightDir[1];
    const float sx = worldPos[0] + g_lightDir[0] * t;
    const float sz = worldPos[2] + g_lightDir[2] * t;

    glBegin(GL_QUADS);
    glVertex3f(sx - radius, g_groundY + 0.05f, sz - radius);
    glVertex3f(sx + radius, g_groundY + 0.05f, sz - radius);
    glVertex3f(sx + radius, g_groundY + 0.05f, sz + radius);
    glVertex3f(sx - radius, g_groundY + 0.05f, sz + radius);
    glEnd();
}

void drawProjectedShadows() {
    const float redLocal[3] = {0.0f, 0.0f, 0.0f};
    const float blueLocal[3] = {g_blueOffsetLocal[0], g_blueOffsetLocal[1], g_blueOffsetLocal[2]};
    float redWorld[3] = {0.0f, 0.0f, 0.0f};
    float blueWorld[3] = {0.0f, 0.0f, 0.0f};
    transformPoint(g_deviceTransform, redLocal, redWorld);
    transformPoint(g_deviceTransform, blueLocal, blueWorld);

    const float redRadius = 5.0f + 0.04f * std::fabs(redWorld[1] - g_groundY);
    const float blueRadius = 5.0f + 0.04f * std::fabs(blueWorld[1] - g_groundY);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glColor4f(0.0f, 0.0f, 0.0f, 0.45f);
    drawShadowQuadAt(redWorld, redRadius);
    drawShadowQuadAt(blueWorld, blueRadius);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// ===================================================================
// OpenHaptics device read
// ===================================================================
#ifdef HAVE_OPENHAPTICS
HDCallbackCode HDCALLBACK copyPoseCallback(void* userData) {
    PoseSample* sample = static_cast<PoseSample*>(userData);
    if (sample == nullptr || g_hDevice == HD_INVALID_HANDLE) {
        return HD_CALLBACK_DONE;
    }

    sample->ok = false;
    hdBeginFrame(g_hDevice);
    HDdouble pos[3], xform[16];
    hdGetDoublev(HD_CURRENT_POSITION, pos);
    hdGetDoublev(HD_CURRENT_TRANSFORM, xform);
    hdEndFrame(g_hDevice);

    const HDErrorInfo err = hdGetError();
    if (!HD_DEVICE_ERROR(err)) {
        for (int i = 0; i < 16; ++i) sample->transform[i] = xform[i];
        for (int i = 0; i < 3; ++i)  sample->position[i]  = pos[i];
        sample->ok = true;
    }
    return HD_CALLBACK_DONE;
}
#endif

// ===================================================================
// Backend-agnostic device read
// ===================================================================
void readDeviceState() {
    switch (g_backend) {
#ifdef HAVE_OPENHAPTICS
    case DeviceBackend::OpenHaptics: {
        if (g_hDevice == HD_INVALID_HANDLE) return;

        PoseSample sample = {
            { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 },
            {0, 0, 0},
            false
        };
        hdScheduleSynchronous(copyPoseCallback, &sample, HD_DEFAULT_SCHEDULER_PRIORITY);
        if (!sample.ok) return;

        for (int i = 0; i < 16; ++i) g_deviceTransform[i] = sample.transform[i];
        for (int i = 0; i < 3; ++i)  g_position[i] = sample.position[i];
        break;
    }
#endif
#ifdef HAVE_HAPTION
    case DeviceBackend::Haption: {
        double xform[16], pos[3];
        if (haption_read_state(xform, pos)) {
            for (int i = 0; i < 16; ++i) g_deviceTransform[i] = xform[i];
            for (int i = 0; i < 3; ++i)  g_position[i] = pos[i];
        }
        break;
    }
#endif
    default:
        break;
    }
}

// ===================================================================
// Display / reshape / input (unchanged logic)
// ===================================================================
void setupCamera() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    const float aspect = static_cast<float>(g_windowWidth) / static_cast<float>(g_windowHeight == 0 ? 1 : g_windowHeight);
    gluPerspective(60.0, aspect, 1.0, 2000.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0.0, 0.0, 400.0,
    0.0, 0.0, 0.0,
    0.0, 1.0, 0.0);
}

void display() {
    readDeviceState();

    const int nowMs = glutGet(GLUT_ELAPSED_TIME);
    if (g_captureState == CaptureState::Waiting && (nowMs - g_waitStartMs) >= 5000) {
        g_captureState = CaptureState::Logging;
        g_logStartMs = nowMs;
        std::cerr << "Q recording started" << std::endl;
    }
    if (g_captureState == CaptureState::Logging) {
        if ((nowMs - g_logStartMs) < 5000) {
            logPose();
        } else {
            g_captureState = CaptureState::Done;
            std::cerr << "Q recording stopped" << std::endl;
        }
    }

    glViewport(0, 0, g_windowWidth, g_windowHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    setupCamera();

    drawGroundPlane();
    drawProjectedShadows();
    drawBoxPair();

    glutSwapBuffers();
}

void reshape(int w, int h) {
    g_windowWidth = w;
    g_windowHeight = h;
}

void idle() {
    glutPostRedisplay();
}

void keyboard(unsigned char key, int, int) {
    if (key == 'q' || key == 'Q') {
        g_captureState = CaptureState::Waiting;
        g_waitStartMs = glutGet(GLUT_ELAPSED_TIME);
        g_logStartMs = 0;
        std::cerr << "Q recording armed (starts in 5s)" << std::endl;
    }
}

// ===================================================================
// Backend-agnostic init / shutdown
// ===================================================================
void shutdownDevice() {
    switch (g_backend) {
#ifdef HAVE_OPENHAPTICS
    case DeviceBackend::OpenHaptics:
        if (g_hDevice != HD_INVALID_HANDLE) {
            hdDisableDevice(g_hDevice);
            g_hDevice = HD_INVALID_HANDLE;
        }
        break;
#endif
#ifdef HAVE_HAPTION
    case DeviceBackend::Haption:
        haption_shutdown();
        break;
#endif
    default:
        break;
    }
}

void initDevice() {
    switch (g_backend) {
#ifdef HAVE_OPENHAPTICS
    case DeviceBackend::OpenHaptics: {
        std::cerr << "Initialising OpenHaptics device..." << std::endl;
        HDErrorInfo error;
        g_hDevice = hdInitDevice(HD_DEFAULT_DEVICE);
        if (HD_DEVICE_ERROR(error = hdGetError())) {
            std::cerr << "Failed to initialize OpenHaptics device: "
                      << hdGetErrorString(error.errorCode) << std::endl;
            std::exit(EXIT_FAILURE);
        }
        hdEnable(HD_FORCE_OUTPUT);
        hdStartScheduler();
        if (HD_DEVICE_ERROR(error = hdGetError())) {
            std::cerr << "Failed to start haptics scheduler: "
                      << hdGetErrorString(error.errorCode) << std::endl;
            shutdownDevice();
            std::exit(EXIT_FAILURE);
        }
        hdMakeCurrentDevice(g_hDevice);
        std::cerr << "OpenHaptics device ready." << std::endl;
        break;
    }
#endif
#ifdef HAVE_HAPTION
    case DeviceBackend::Haption: {
        if (!haption_init(g_haptionAddr.c_str())) {
            std::cerr << "Failed to initialise Haption device at "
                      << g_haptionAddr << std::endl;
            std::exit(EXIT_FAILURE);
        }
        break;
    }
#endif
    default:
        std::cerr << "No device backend available!" << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void initGraphics() {
    glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glShadeModel(GL_SMOOTH);
}

// ===================================================================
// Usage / argument parsing
// ===================================================================
void printUsage(const char* progName) {
    std::cerr << "Usage: " << progName << " [OPTIONS]\n"
              << "\n"
              << "Options:\n"
#ifdef HAVE_OPENHAPTICS
              << "  --openhaptics        Use OpenHaptics (3DS Touch) backend [default]\n"
#endif
#ifdef HAVE_HAPTION
              << "  --haption [ip#port]  Use Haption (Virtuose) backend\n"
              << "                       Default address: 127.0.0.1#5000\n"
#endif
              << "  --help               Show this help\n";
}

void parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

#ifdef HAVE_OPENHAPTICS
        if (arg == "--openhaptics") {
            g_backend = DeviceBackend::OpenHaptics;
            continue;
        }
#endif
#ifdef HAVE_HAPTION
        if (arg == "--haption") {
            g_backend = DeviceBackend::Haption;
            // Optional next arg is ip:port
            if (i + 1 < argc && argv[i+1][0] != '-') {
                g_haptionAddr = argv[++i];
            }
            continue;
        }
#endif
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        }
    }
}

// ===================================================================
// main
// ===================================================================
int main(int argc, char** argv) {
    parseArgs(argc, argv);

    initDevice();

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(g_windowWidth, g_windowHeight);
    glutCreateWindow("Haptics Test");

    initGraphics();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutIdleFunc(idle);
    glutKeyboardFunc(keyboard);

    atexit(shutdownDevice);
    glutMainLoop();
    return 0;
}
