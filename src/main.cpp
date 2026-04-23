#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>

#include <HD/hd.h>
#include <HDU/hduError.h>

#include <GL/glut.h>

HHD g_hDevice = HD_INVALID_HANDLE;

HDdouble g_deviceTransform[16] = {
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0
};
HDdouble g_position[3] = {0.0, 0.0, 0.0};

const float g_blueOffsetLocal[3] = {+26.75698006, -11.32325751, +130.50953227};

int g_windowWidth = 1024;
int g_windowHeight = 768;

const float g_groundY = -120.0f;
const float g_lightDir[3] = {-0.00f, -1.0f, -0.00f};

struct PoseSample {
    HDdouble transform[16];
    HDdouble position[3];
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

void transformPoint(const HDdouble m[16], const float p[3], float out[3]) {
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

HDCallbackCode HDCALLBACK copyPoseCallback(void* userData) {
    PoseSample* sample = static_cast<PoseSample*>(userData);
    if (sample == nullptr || g_hDevice == HD_INVALID_HANDLE) {
        return HD_CALLBACK_DONE;
    }

    sample->ok = false;
    hdBeginFrame(g_hDevice);
    hdGetDoublev(HD_CURRENT_POSITION, sample->position);
    hdGetDoublev(HD_CURRENT_TRANSFORM, sample->transform);
    hdEndFrame(g_hDevice);

    const HDErrorInfo err = hdGetError();
    if (!HD_DEVICE_ERROR(err)) {
        sample->ok = true;
    }
    return HD_CALLBACK_DONE;
}

void readDeviceState() {
    if (g_hDevice == HD_INVALID_HANDLE) {
        return;
    }

    PoseSample sample = {
        {
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0
        },
        {0.0, 0.0, 0.0},
        false
    };

    hdScheduleSynchronous(copyPoseCallback, &sample, HD_DEFAULT_SCHEDULER_PRIORITY);
    if (!sample.ok) {
        return;
    }

    for (int i = 0; i < 16; ++i) {
        g_deviceTransform[i] = sample.transform[i];
    }
    for (int i = 0; i < 3; ++i) {
        g_position[i] = sample.position[i];
    }
}

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

void shutdownDevice() {
    if (g_hDevice != HD_INVALID_HANDLE) {
        hdDisableDevice(g_hDevice);
        g_hDevice = HD_INVALID_HANDLE;
    }
}

void initDevice() {
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
}

void initGraphics() {
    glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glShadeModel(GL_SMOOTH);
}

int main(int argc, char** argv) {
    initDevice();

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(g_windowWidth, g_windowHeight);
    glutCreateWindow("Hello");

    initGraphics();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutIdleFunc(idle);
    glutKeyboardFunc(keyboard);

    atexit(shutdownDevice);
    glutMainLoop();
    return 0;
}
