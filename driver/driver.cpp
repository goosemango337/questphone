// QuestPhone SteamVR driver: virtual HMD + left/right controllers.
// Pose/input arrive as 52-byte UDP packets on 127.0.0.1:5601 from bridge.py.
#include "openvr_driver.h"
#include <atomic>
#include <cstring>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define CLOSESOCK close
#endif
using namespace vr;

#pragma pack(push, 1)
struct State { float q[4]; float ls[2], rs[2]; float trig[2]; float grip[2]; uint32_t buttons; };
#pragma pack(pop)
// buttons: 1=A 2=B 4=X 8=Y 16=Menu 32=System
static State g_state = {{0, 0, 0, 1}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, 0};
static std::atomic<bool> g_run{false};

static void rot(const float* q, const float* v, float* o) {
  float x = q[0], y = q[1], z = q[2], w = q[3];
  float tx = 2 * (y * v[2] - z * v[1]), ty = 2 * (z * v[0] - x * v[2]), tz = 2 * (x * v[1] - y * v[0]);
  o[0] = v[0] + w * tx + (y * tz - z * ty);
  o[1] = v[1] + w * ty + (z * tx - x * tz);
  o[2] = v[2] + w * tz + (x * ty - y * tx);
}

static DriverPose_t makePose(const float* q, const float* p) {
  DriverPose_t d{};
  d.poseIsValid = true;
  d.deviceIsConnected = true;
  d.result = TrackingResult_Running_OK;
  d.qWorldFromDriverRotation.w = 1;
  d.qDriverFromHeadRotation.w = 1;
  d.qRotation.w = q[3]; d.qRotation.x = q[0]; d.qRotation.y = q[1]; d.qRotation.z = q[2];
  for (int i = 0; i < 3; i++) d.vecPosition[i] = p[i];
  return d;
}

static const float HEAD[3] = {0.f, 1.6f, 0.f};

class Hmd : public ITrackedDeviceServerDriver, public IVRDisplayComponent {
 public:
  uint32_t id = k_unTrackedDeviceIndexInvalid;
  EVRInitError Activate(uint32_t i) override {
    id = i;
    auto c = VRProperties()->TrackedDeviceToPropertyContainer(i);
    VRProperties()->SetStringProperty(c, Prop_ModelNumber_String, "QuestPhone");
    VRProperties()->SetStringProperty(c, Prop_ManufacturerName_String, "QuestPhone");
    VRProperties()->SetStringProperty(c, Prop_TrackingSystemName_String, "questphone");
    VRProperties()->SetStringProperty(c, Prop_RenderModelName_String, "generic_hmd");
    VRProperties()->SetFloatProperty(c, Prop_UserIpdMeters_Float, 0.063f);
    VRProperties()->SetFloatProperty(c, Prop_UserHeadToEyeDepthMeters_Float, 0.f);
    VRProperties()->SetFloatProperty(c, Prop_DisplayFrequency_Float, 60.f);
    VRProperties()->SetFloatProperty(c, Prop_SecondsFromVsyncToPhotons_Float, 0.011f);
    VRProperties()->SetBoolProperty(c, Prop_IsOnDesktop_Bool, true);
    return VRInitError_None;
  }
  void Deactivate() override { id = k_unTrackedDeviceIndexInvalid; }
  void EnterStandby() override {}
  void* GetComponent(const char* n) override {
    if (!strcmp(n, IVRDisplayComponent_Version)) return static_cast<IVRDisplayComponent*>(this);
    return nullptr;
  }
  void DebugRequest(const char*, char* r, uint32_t s) override { if (s) r[0] = 0; }
  DriverPose_t GetPose() override { return makePose(g_state.q, HEAD); }
  void Push() {
    if (id != k_unTrackedDeviceIndexInvalid)
      VRServerDriverHost()->TrackedDevicePoseUpdated(id, GetPose(), sizeof(DriverPose_t));
  }
  // The compositor draws into a 1920x888 desktop window (top-left of the main monitor).
  // bridge.py captures that region and streams it to the phone as a side-by-side image.
  void GetWindowBounds(int32_t* x, int32_t* y, uint32_t* w, uint32_t* h) override { *x = 0; *y = 0; *w = 1920; *h = 888; }
  bool IsDisplayOnDesktop() override { return true; }
  bool IsDisplayRealDisplay() override { return false; }
  void GetRecommendedRenderTargetSize(uint32_t* w, uint32_t* h) override { *w = 1440; *h = 1440; }
  void GetEyeOutputViewport(EVREye e, uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h) override {
    *x = e == Eye_Left ? 0 : 960; *y = 0; *w = 960; *h = 888;
  }
  void GetProjectionRaw(EVREye, float* l, float* r, float* t, float* b) override { *l = -1; *r = 1; *t = -1; *b = 1; }
  DistortionCoordinates_t ComputeDistortion(EVREye, float u, float v) override {
    DistortionCoordinates_t d;
    d.rfRed[0] = d.rfGreen[0] = d.rfBlue[0] = u;
    d.rfRed[1] = d.rfGreen[1] = d.rfBlue[1] = v;
    return d;
  }
  bool ComputeInverseDistortion(HmdVector2_t* r, EVREye, uint32_t, float u, float v) override {
    r->v[0] = u; r->v[1] = v;
    return true;
  }
};

class Ctrl : public ITrackedDeviceServerDriver {
 public:
  int hand;  // 0 left, 1 right
  uint32_t id = k_unTrackedDeviceIndexInvalid;
  DriverPose_t pose{};
  VRInputComponentHandle_t b1{}, b2{}, b3{}, trig{}, trigC{}, grip{}, sx{}, sy{}, sc{};
  explicit Ctrl(int h) : hand(h) {}
  EVRInitError Activate(uint32_t i) override {
    id = i;
    auto c = VRProperties()->TrackedDeviceToPropertyContainer(i);
    VRProperties()->SetStringProperty(c, Prop_ModelNumber_String, hand ? "Oculus Quest Controller (Right)" : "Oculus Quest Controller (Left)");
    VRProperties()->SetStringProperty(c, Prop_ControllerType_String, "oculus_touch");
    VRProperties()->SetStringProperty(c, Prop_InputProfilePath_String, "{oculus}/input/touch_profile.json");
    VRProperties()->SetStringProperty(c, Prop_RenderModelName_String, hand ? "oculus_rifts_controller_right" : "oculus_rifts_controller_left");
    VRProperties()->SetInt32Property(c, Prop_ControllerRoleHint_Int32, hand ? TrackedControllerRole_RightHand : TrackedControllerRole_LeftHand);
    auto in = VRDriverInput();
    in->CreateBooleanComponent(c, hand ? "/input/a/click" : "/input/x/click", &b1);
    in->CreateBooleanComponent(c, hand ? "/input/b/click" : "/input/y/click", &b2);
    in->CreateBooleanComponent(c, hand ? "/input/system/click" : "/input/menu/click", &b3);
    in->CreateScalarComponent(c, "/input/trigger/value", &trig, VRScalarType_Absolute, VRScalarUnits_NormalizedOneSided);
    in->CreateBooleanComponent(c, "/input/trigger/click", &trigC);
    in->CreateScalarComponent(c, "/input/grip/value", &grip, VRScalarType_Absolute, VRScalarUnits_NormalizedOneSided);
    in->CreateScalarComponent(c, "/input/thumbstick/x", &sx, VRScalarType_Absolute, VRScalarUnits_NormalizedTwoSided);
    in->CreateScalarComponent(c, "/input/thumbstick/y", &sy, VRScalarType_Absolute, VRScalarUnits_NormalizedTwoSided);
    in->CreateBooleanComponent(c, "/input/thumbstick/click", &sc);
    return VRInitError_None;
  }
  void Deactivate() override { id = k_unTrackedDeviceIndexInvalid; }
  void EnterStandby() override {}
  void* GetComponent(const char*) override { return nullptr; }
  void DebugRequest(const char*, char* r, uint32_t s) override { if (s) r[0] = 0; }
  DriverPose_t GetPose() override { return pose; }
  void Push(const State& s) {
    if (id == k_unTrackedDeviceIndexInvalid) return;
    float off[3] = {hand ? 0.18f : -0.18f, -0.35f, -0.3f}, o[3], p[3];
    rot(s.q, off, o);
    for (int i = 0; i < 3; i++) p[i] = HEAD[i] + o[i];
    pose = makePose(s.q, p);
    auto in = VRDriverInput();
    in->UpdateBooleanComponent(b1, s.buttons & (hand ? 1 : 4), 0);
    in->UpdateBooleanComponent(b2, s.buttons & (hand ? 2 : 8), 0);
    in->UpdateBooleanComponent(b3, s.buttons & (hand ? 32 : 16), 0);
    in->UpdateScalarComponent(trig, s.trig[hand], 0);
    in->UpdateBooleanComponent(trigC, s.trig[hand] > 0.7f, 0);
    in->UpdateScalarComponent(grip, s.grip[hand], 0);
    in->UpdateScalarComponent(sx, hand ? s.rs[0] : s.ls[0], 0);
    in->UpdateScalarComponent(sy, hand ? s.rs[1] : s.ls[1], 0);
    VRServerDriverHost()->TrackedDevicePoseUpdated(id, pose, sizeof(DriverPose_t));
  }
};

class Provider : public IServerTrackedDeviceProvider {
  Hmd hmd;
  Ctrl left{0}, right{1};
  std::thread th;
  void loop() {
#ifdef _WIN32
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
#endif
    sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(5601);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(s, (sockaddr*)&a, sizeof a);
#ifdef _WIN32
    DWORD to = 4;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof to);
#else
    timeval to{0, 4000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
#endif
    while (g_run) {
      State t;
      int n = (int)recv(s, (char*)&t, sizeof t, 0);
      if (n == (int)sizeof t) g_state = t;
      State c = g_state;
      hmd.Push();
      left.Push(c);
      right.Push(c);
    }
    CLOSESOCK(s);
#ifdef _WIN32
    WSACleanup();
#endif
  }

 public:
  EVRInitError Init(IVRDriverContext* ctx) override {
    VR_INIT_SERVER_DRIVER_CONTEXT(ctx);
    VRServerDriverHost()->TrackedDeviceAdded("questphone_hmd", TrackedDeviceClass_HMD, &hmd);
    VRServerDriverHost()->TrackedDeviceAdded("questphone_left", TrackedDeviceClass_Controller, &left);
    VRServerDriverHost()->TrackedDeviceAdded("questphone_right", TrackedDeviceClass_Controller, &right);
    g_run = true;
    th = std::thread(&Provider::loop, this);
    return VRInitError_None;
  }
  void Cleanup() override {
    g_run = false;
    if (th.joinable()) th.join();
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
  }
  const char* const* GetInterfaceVersions() override { return k_InterfaceVersions; }
  void RunFrame() override {}
  bool ShouldBlockStandbyMode() override { return false; }
  void EnterStandby() override {}
  void LeaveStandby() override {}
};

static Provider g_provider;

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
EXPORT void* HmdDriverFactory(const char* name, int* err) {
  if (!strcmp(IServerTrackedDeviceProvider_Version, name)) return &g_provider;
  if (err) *err = VRInitError_Init_InterfaceNotFound;
  return nullptr;
}
