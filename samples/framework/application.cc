//----------------------------------------------------------------------------//
//                                                                            //
// ozz-animation is hosted at http://github.com/guillaumeblanc/ozz-animation  //
// and distributed under the MIT License (MIT).                               //
//                                                                            //
// Copyright (c) Guillaume Blanc                                              //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included in //
// all copies or substantial portions of the Software.                        //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//----------------------------------------------------------------------------//

#define OZZ_INCLUDE_PRIVATE_HEADER  // Allows to include private headers.

#include "framework/application.h"

#include <cassert>
#include <cstdlib>
#include <cstring>

#ifdef __APPLE__
#include <unistd.h>
#endif  // __APPLE__

#if EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#endif  // EMSCRIPTEN

#include "framework/image.h"
#include "framework/internal/camera.h"
#include "framework/internal/imgui_impl.h"
#include "framework/internal/renderer_impl.h"
#include "framework/internal/shooter.h"
#include "framework/profile.h"
#include "framework/renderer.h"
#include "ozz/base/io/stream.h"
#include "ozz/base/log.h"
#include "ozz/base/maths/box.h"
#include "ozz/base/memory/allocator.h"
#include "ozz/options/options.h"

OZZ_OPTIONS_DECLARE_INT(
    max_idle_loops,
    "The maximum number of idle loops the sample application can perform."
    " Application automatically exit when this number of loops is reached."
    " A negative value disables this feature.",
    -1, false);

OZZ_OPTIONS_DECLARE_BOOL(render, "Enables sample redering.", true, false);

namespace {
// Screen resolution presets.
const ozz::sample::Resolution resolution_presets[] = {
    {640, 360},   {640, 480},  {800, 450},  {800, 600},   {1024, 576},
    {1024, 768},  {1280, 720}, {1280, 800}, {1280, 960},  {1280, 1024},
    {1400, 1050}, {1440, 900}, {1600, 900}, {1600, 1200}, {1680, 1050},
    {1920, 1080}, {1920, 1200}};
const int kNumPresets = OZZ_ARRAY_SIZE(resolution_presets);
}  // namespace

// Check resolution argument is within 0 - kNumPresets
static bool ResolutionCheck(const ozz::options::Option& _option,
                            int /*_argc*/) {
  const ozz::options::IntOption& option =
      static_cast<const ozz::options::IntOption&>(_option);
  return option >= 0 && option < kNumPresets;
}

OZZ_OPTIONS_DECLARE_INT_FN(resolution, "Resolution index (0 to 17).", 5, false,
                           &ResolutionCheck);

namespace ozz {
namespace sample {
Application* Application::application_ = nullptr;

Application::Application()
    : exit_(false),
      freeze_(false),
      fix_update_rate(false),
      fixed_update_rate(60.f),
      time_factor_(1.f),
      time_(0.f),
      last_idle_time_(0.),
      show_help_(false),
      vertical_sync_(true),
      swap_interval_(1),
      show_grid_(true),
      show_axes_(true),
      capture_video_(false),
      capture_screenshot_(false),
      fps_(New<Record>(128)),
      update_time_(New<Record>(128)),
      render_time_(New<Record>(128)),
      resolution_(resolution_presets[0]),
      use_sample_gui_(true),
      window_(nullptr),
      mouse_wheel_position_(0.0) {
#ifndef NDEBUG
  // Assert presets are correctly sorted.
  for (int i = 1; i < kNumPresets; ++i) {
    const Resolution& preset_m1 = resolution_presets[i - 1];
    const Resolution& preset = resolution_presets[i];
    assert(preset.width > preset_m1.width || preset.height > preset_m1.height);
  }
#endif  //  NDEBUG
}

Application::~Application() {}

int Application::Run(int _argc, const char** _argv, const char* _version,
                     const char* _title) {
  // Only one application at a time can be ran.
  if (application_) {
    return EXIT_FAILURE;
  }
  application_ = this;

  // Starting application
  log::Out() << "Starting sample \"" << _title << "\" version \"" << _version
             << "\"" << std::endl;
  log::Out() << "Ozz libraries were built with \""
             << math::SimdImplementationName() << "\" SIMD math implementation."
             << std::endl;

  // Parse command line arguments.
  const char* usage =
      "Ozz animation sample. See README.md file for more details.";
  ozz::options::ParseResult result =
      ozz::options::ParseCommandLine(_argc, _argv, _version, usage);
  if (result != ozz::options::kSuccess) {
    exit_ = true;
    return result == ozz::options::kExitSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  // Fetch initial resolution.
  resolution_ = resolution_presets[OPTIONS_resolution];

#ifdef __APPLE__
  // On OSX, when run from Finder, working path is the root path. This does not
  // allow to load resources from relative path.
  // The workaround is to change the working directory to application directory.
  // The proper solution would probably be to use bundles and load data from
  // resource folder.
  chdir(ozz::options::ParsedExecutablePath().c_str());
#endif  // __APPLE__

  // Initialize help.
  ParseReadme();

  // Open an OpenGL window
  bool success = true;
  if (OPTIONS_render) {
    // Initialize GLFW
    if (!glfwInit()) {
      application_ = nullptr;
      return EXIT_FAILURE;
    }

    // Setup GL context.
    const int gl_version_major = 3, gl_version_minor = 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, gl_version_major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, gl_version_minor);
    glfwWindowHint(GLFW_SAMPLES, 4);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif  // NDEBUG

    // Initializes rendering before looping.
    window_ = glfwCreateWindow(resolution_.width, resolution_.height, _title,
                               nullptr, nullptr);
    if (!window_) {
      log::Err()
          << "Failed to create OpenGL window. Required OpenGL version is "
          << gl_version_major << "." << gl_version_minor << "." << std::endl;
      success = false;
    } else {
      glfwMakeContextCurrent(window_);
      log::Out() << "Successfully opened OpenGL window version \""
                 << glGetString(GL_VERSION) << "\"." << std::endl;

      glfwSwapInterval(vertical_sync_ ? swap_interval_ : 0);
      glfwSetWindowUserPointer(window_, this);
      glfwSetWindowSizeCallback(window_, &ResizeCbk);
      glfwSetFramebufferSizeCallback(window_, &ResizeCbk);
      glfwSetWindowCloseCallback(window_, &CloseCbk);
      glfwSetScrollCallback(window_, &ScrollCbk);
      mouse_wheel_position_ = 0.0;

      // Allocates and initializes camera
      camera_ = make_unique<internal::Camera>();
      math::Float3 camera_center;
      math::Float2 camera_angles;
      float distance;
      if (GetCameraInitialSetup(&camera_center, &camera_angles, &distance)) {
        camera_->Reset(camera_center, camera_angles, distance);
      }

      // Allocates and initializes renderer.
      renderer_ = make_unique<internal::RendererImpl>(camera_.get());
      success = renderer_->Initialize();

      if (success) {
        shooter_ = make_unique<internal::Shooter>();
        if (use_sample_gui_) {
          im_gui_ = make_unique<internal::ImGuiImpl>();
        }

        int framebuffer_width = 0;
        int framebuffer_height = 0;
        glfwGetFramebufferSize(window_, &framebuffer_width,
                               &framebuffer_height);
        ResizeCbk(window_, framebuffer_width, framebuffer_height);

        // Loop the sample.
        success = Loop();
        shooter_.reset();
        im_gui_.reset();
      }
      renderer_.reset();
      camera_.reset();
    }

    // Closes window and terminates GLFW.
    if (window_) {
      glfwDestroyWindow(window_);
      window_ = nullptr;
    }
    glfwTerminate();

  } else {
    // Loops without any rendering initialization.
    success = Loop();
  }

  // Notifies that an error occurred.
  if (!success) {
    log::Err() << "An error occurred during sample execution." << std::endl;
  }

  application_ = nullptr;

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

// Helper function to detecte key pressed and released.
template <int _Key>
bool KeyPressed(GLFWwindow* window) {
  static int previous_key = GLFW_RELEASE;
  if (!window) {
    previous_key = GLFW_RELEASE;
    return false;
  }
  const int key = glfwGetKey(window, _Key);
  const bool pressed = previous_key == GLFW_PRESS && key == GLFW_RELEASE;
  previous_key = key;
  return pressed;
}

Application::LoopStatus Application::OneLoop(int _loops) {
  Profiler profile(fps_.get());  // Profiles frame.

  // Tests for a manual exit request.
  if (exit_ ||
      (window_ && glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) ||
      (window_ && glfwWindowShouldClose(window_))) {
    return kBreak;
  }

  // Test for an exit request.
  if (OPTIONS_max_idle_loops > 0 && _loops > OPTIONS_max_idle_loops) {
    return kBreak;
  }

// Don't overload the cpu if the window is not active.
#ifndef EMSCRIPTEN
  if (OPTIONS_render && window_ &&
      glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_FALSE) {
    glfwWaitEvents();  // Wait...

    // Reset last update time in order to stop the time while the app isn't
    // active.
    last_idle_time_ = glfwGetTime();

    return kContinue;  // ...but don't do anything.
  }
  if (OPTIONS_render && window_) {
    glfwPollEvents();
  }
#else
  int width, height;
  if (emscripten_get_canvas_element_size("#canvas", &width, &height) !=
      EMSCRIPTEN_RESULT_SUCCESS) {
    return kBreakFailure;
  }
  if (width != resolution_.width || height != resolution_.height) {
    ResizeCbk(width, height);
  }
#endif  // EMSCRIPTEN

  // Enable/disable help on F1 key.
  show_help_ = show_help_ ^ KeyPressed<GLFW_KEY_F1>(window_);

  // Capture screenshot or video.
  capture_screenshot_ = KeyPressed<GLFW_KEY_S>(window_);
  capture_video_ = capture_video_ ^ KeyPressed<GLFW_KEY_V>(window_);

  // Do the main loop.
  if (!Idle(_loops == 0)) {
    return kBreakFailure;
  }

  // Skips display if "no_render" option is enabled.
  if (OPTIONS_render) {
    if (!Display()) {
      return kBreakFailure;
    }
  }

  return kContinue;
}

void OneLoopCbk(void* _arg) {
  Application* app = reinterpret_cast<Application*>(_arg);
  static int loops = 0;
  app->OneLoop(loops++);
}

bool Application::Loop() {
  // Initialize sample.
  bool success = OnInitialize();

// Emscripten requires to manage the main loop on their own, as browsers don't
// like infinite blocking functions.
#ifdef EMSCRIPTEN
  emscripten_set_main_loop_arg(OneLoopCbk, this, 0, 1);
#else   // EMSCRIPTEN
  // Loops.
  for (int loops = 0; success; ++loops) {
    const LoopStatus status = OneLoop(loops);
    success = status != kBreakFailure;
    if (status != kContinue) {
      break;
    }
  }
#endif  // EMSCRIPTEN

  // De-initialize sample, even in case of initialization failure.
  OnDestroy();

  return success;
}

bool Application::Display() {
  assert(OPTIONS_render);

  bool success = true;

  {  // Profiles rendering excluding GUI.
    Profiler profile(render_time_.get());

    GL(ClearDepth(1.f));
    GL(ClearColor(.4f, .42f, .38f, 1.f));
    GL(Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    // Setup default states
    GL(Enable(GL_CULL_FACE));
    GL(CullFace(GL_BACK));
    GL(Enable(GL_DEPTH_TEST));
    GL(DepthMask(GL_TRUE));
    GL(DepthFunc(GL_LEQUAL));

    // Bind 3D camera matrices.
    camera_->Bind3D();

    // Forwards display event to the inheriting application.
    if (success) {
      success = OnDisplay(renderer_.get());
    }
  }  // Ends profiling.

  // Renders grid and axes at the end as they are transparent.
  if (show_grid_) {
    renderer_->DrawGrid(20, 1.f);
  }
  if (show_axes_) {
    renderer_->DrawAxes(ozz::math::Float4x4::identity());
  }

  // Bind 2D camera matrices.
  camera_->Bind2D();

  // Forwards gui event to the inheriting application.
  if (success) {
    success = Gui();
  }

  if (success) {
    success = OnRenderUiOverlay();
  }

  // Capture back buffer.
  if (capture_screenshot_ || capture_video_) {
    shooter_->Capture(GL_BACK);
    capture_screenshot_ = false;
  }

  // Swaps current window.
  if (window_) {
    glfwSwapBuffers(window_);
  }

  return success;
}

bool Application::Idle(bool _first_frame) {
  // Early out if displaying help.
  if (show_help_) {
    last_idle_time_ = glfwGetTime();
    return true;
  }

  // Compute elapsed time since last idle, and delta time.
  float delta;
  double time = glfwGetTime();
  if (_first_frame ||  // Don't take into account time spent initializing.
      time == 0.) {    // Means glfw isn't initialized (rendering's disabled).
    delta = 1.f / 60.f;
  } else {
    delta = static_cast<float>(time - last_idle_time_);
  }
  last_idle_time_ = time;

  // Update dt, can be scaled, fixed, freezed...
  float update_delta;
  if (freeze_) {
    update_delta = 0.f;
  } else {
    if (fix_update_rate) {
      update_delta = time_factor_ / fixed_update_rate;
    } else {
      update_delta = delta * time_factor_;
    }
  }

  // Increment current application time
  time_ += update_delta;

  // Forwards update event to the inheriting application.
  bool update_result;
  {  // Profiles update scope.
    Profiler profile(update_time_.get());
    update_result = OnUpdate(update_delta, time_);
  }

  // Updates screen shooter object.
  if (shooter_) {
    shooter_->Update();
  }

  // Update camera model-view matrix.
  if (camera_) {
    math::Box scene_bounds;
    GetSceneBounds(&scene_bounds);

    math::Float4x4 camera_transform;
    if (GetCameraOverride(&camera_transform)) {
      camera_->Update(camera_transform, scene_bounds, delta, _first_frame);
    } else {
      camera_->Update(scene_bounds, delta, _first_frame);
    }
  }

  return update_result;
}

bool Application::Gui() {
  bool success = true;
  const float kFormWidth = 200.f;
  const float kHelpMargin = 16.f;

  if (!use_sample_gui_ || !im_gui_) {
    return true;
  }

  // Finds gui area.
  const float kGuiMargin = 2.f;
  ozz::math::RectInt window_rect(0, 0, resolution_.width, resolution_.height);

  // Fills ImGui's input structure.
  internal::ImGuiImpl::Inputs input;
  double cursor_x = 0.0;
  double cursor_y = 0.0;
  if (window_) {
    glfwGetCursorPos(window_, &cursor_x, &cursor_y);
  }
  input.mouse_x = static_cast<int>(cursor_x);
  input.mouse_y = window_rect.height - static_cast<int>(cursor_y);
  input.lmb_pressed =
      window_ &&
      glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

  // Starts frame
  im_gui_->BeginFrame(input, window_rect, renderer_.get());

  // Downcast to public imgui.
  ImGui* im_gui = im_gui_.get();

  // Do floating gui.
  if (!show_help_) {
    success = OnFloatingGui(im_gui);
  }

  // Help gui.
  {
    math::RectFloat rect(kGuiMargin, kGuiMargin,
                         window_rect.width - kGuiMargin * 2.f,
                         window_rect.height - kGuiMargin * 2.f);
    // Doesn't constrain form is it's opened, so it covers all screen.
    ImGui::Form form(im_gui, "Show help", rect, &show_help_, !show_help_);
    if (show_help_) {
      im_gui->DoLabel(help_.c_str(), ImGui::kLeft, false);
    }
  }

  // Do framework gui.
  if (!show_help_ && success &&
      window_rect.width > (kGuiMargin + kFormWidth) * 2.f) {
    static bool open = true;
    math::RectFloat rect(kGuiMargin, kGuiMargin, kFormWidth,
                         window_rect.height - kGuiMargin * 2.f - kHelpMargin);
    ImGui::Form form(im_gui, "Framework", rect, &open, true);
    if (open) {
      success = FrameworkGui();
    }
  }

  // Do sample gui.
  if (!show_help_ && success && window_rect.width > kGuiMargin + kFormWidth) {
    static bool open = true;
    math::RectFloat rect(window_rect.width - kFormWidth - kGuiMargin,
                         kGuiMargin, kFormWidth,
                         window_rect.height - kGuiMargin * 2 - kHelpMargin);
    ImGui::Form form(im_gui, "Sample", rect, &open, true);
    if (open) {
      // Forwards event to the inherited application.
      success = OnGui(im_gui);
    }
  }

  // Ends frame
  im_gui_->EndFrame();

  return success;
}

bool Application::FrameworkGui() {
  char label[64];
  // Downcast to public imgui.
  ImGui* im_gui = im_gui_.get();
  {  // Render statistics
    static bool open = true;
    ImGui::OpenClose stat_oc(im_gui, "Statistics", &open);
    if (open) {
      {  // FPS
        Record::Statistics statistics = fps_->GetStatistics();
        std::snprintf(label, sizeof(label), "FPS: %.0f",
                      statistics.mean == 0.f ? 0.f : 1000.f / statistics.mean);
        static bool fps_open = false;
        ImGui::OpenClose stats(im_gui, label, &fps_open);
        if (fps_open) {
          std::snprintf(label, sizeof(label), "Frame: %.2f ms",
                        statistics.mean);
          im_gui->DoGraph(label, 0.f, statistics.max, statistics.latest,
                          fps_->cursor(), fps_->record_begin(),
                          fps_->record_end());
        }
      }
      {  // Update time
        Record::Statistics statistics = update_time_->GetStatistics();
        std::snprintf(label, sizeof(label), "Update: %.2f ms", statistics.mean);
        static bool update_open = true;  // This is the most relevant for ozz.
        ImGui::OpenClose stats(im_gui, label, &update_open);
        if (update_open) {
          im_gui->DoGraph(nullptr, 0.f, statistics.max, statistics.latest,
                          update_time_->cursor(), update_time_->record_begin(),
                          update_time_->record_end());
        }
      }
      {  // Render time
        Record::Statistics statistics = render_time_->GetStatistics();
        std::snprintf(label, sizeof(label), "Render: %.2f ms", statistics.mean);
        static bool render_open = false;
        ImGui::OpenClose stats(im_gui, label, &render_open);
        if (render_open) {
          im_gui->DoGraph(nullptr, 0.f, statistics.max, statistics.latest,
                          render_time_->cursor(), render_time_->record_begin(),
                          render_time_->record_end());
        }
      }
    }
  }

  {  // Time control
    static bool open = false;
    ImGui::OpenClose stats(im_gui, "Time control", &open);
    if (open) {
      im_gui->DoButton("Freeze", true, &freeze_);
      im_gui->DoCheckBox("Fix update rate", &fix_update_rate, true);
      if (!fix_update_rate) {
        std::snprintf(label, sizeof(label), "Time factor: %.2f", time_factor_);
        im_gui->DoSlider(label, -5.f, 5.f, &time_factor_);
        if (im_gui->DoButton("Reset time factor", time_factor_ != 1.f)) {
          time_factor_ = 1.f;
        }
      } else {
        std::snprintf(label, sizeof(label), "Update rate: %.0f fps",
                      fixed_update_rate);
        im_gui->DoSlider(label, 1.f, 200.f, &fixed_update_rate, .5f, true);
        if (im_gui->DoButton("Reset update rate", fixed_update_rate != 60.f)) {
          fixed_update_rate = 60.f;
        }
      }
    }
  }

  {  // Rendering options
    static bool open = false;
    ImGui::OpenClose options(im_gui, "Options", &open);
    if (open) {
      // Multi-sampling.
      const bool fsaa_available =
          window_ && glfwGetWindowAttrib(window_, GLFW_SAMPLES) != 0;
      static bool fsaa_enabled = false;
      static bool fsaa_initialized = false;
      if (!fsaa_initialized) {
        fsaa_enabled = fsaa_available;
        fsaa_initialized = true;
      }
      if (im_gui->DoCheckBox("Anti-aliasing", &fsaa_enabled, fsaa_available)) {
        if (fsaa_enabled) {
          GL(Enable(GL_MULTISAMPLE));
        } else {
          GL(Disable(GL_MULTISAMPLE));
        }
      }
      // Vertical sync & swap interval
      bool changed = im_gui->DoCheckBox("Vertical sync", &vertical_sync_);
      std::snprintf(label, sizeof(label), "Swap interval: %d", swap_interval_);
      changed |=
          im_gui->DoSlider(label, 1, 4, &swap_interval_, 1.f, vertical_sync_);
      if (changed) {
        if (window_) {
          glfwSwapInterval(vertical_sync_ ? swap_interval_ : 0);
        }
      }

      im_gui->DoCheckBox("Show grid", &show_grid_, true);
      im_gui->DoCheckBox("Show axes", &show_axes_, true);
    }

    // Searches for matching resolution settings.
    int preset_lookup = 0;
    for (; preset_lookup < kNumPresets - 1; ++preset_lookup) {
      const Resolution& preset = resolution_presets[preset_lookup];
      if (preset.width > resolution_.width) {
        break;
      } else if (preset.width == resolution_.width) {
        if (preset.height >= resolution_.height) {
          break;
        }
      }
    }

    std::snprintf(label, sizeof(label), "Resolution: %dx%d", resolution_.width,
                  resolution_.height);
    if (im_gui->DoSlider(label, 0, kNumPresets - 1, &preset_lookup)) {
      // Resolution changed.
      resolution_ = resolution_presets[preset_lookup];
      if (window_) {
        glfwSetWindowSize(window_, resolution_.width, resolution_.height);
      }
    }
  }

  {  // Capture
    static bool open = false;
    ImGui::OpenClose controls(im_gui, "Capture", &open);
    if (open) {
      im_gui->DoButton("Capture video", true, &capture_video_);
      capture_screenshot_ |= im_gui->DoButton(
          "Capture screenshot", !capture_video_, &capture_screenshot_);
    }
  }

  {  // Controls
    static bool open = false;
    ImGui::OpenClose controls(im_gui, "Camera controls", &open);
    if (open) {
      camera_->OnGui(im_gui);
    }
  }
  return true;
}

bool Application::OnInitialize() { return true; }

void Application::OnDestroy() {}

bool Application::OnUpdate(float, float) { return true; }

bool Application::OnGui(ImGui*) { return true; }

bool Application::OnFloatingGui(ImGui*) { return true; }

bool Application::OnDisplay(Renderer*) { return true; }

bool Application::OnRenderUiOverlay() { return true; }

bool Application::GetCameraInitialSetup(math::Float3*, math::Float2*,
                                        float*) const {
  return false;
}

// Default implementation doesn't override camera location.
bool Application::GetCameraOverride(math::Float4x4*) const { return false; }

void Application::GetSceneBounds(math::Box*) const {}

math::Float2 Application::WorldToScreen(const math::Float3& _world) const {
  const math::SimdFloat4 ndc =
      (camera_->projection() * camera_->view()) *
      math::simd_float4::Load(_world.x, _world.y, _world.z, 1.f);

  const math::SimdFloat4 resolution = math::simd_float4::FromInt(
      math::simd_int4::Load(resolution_.width, resolution_.height, 0, 0));
  const ozz::math::SimdFloat4 screen =
      resolution * ((ndc / math::SplatW(ndc)) + math::simd_float4::one()) /
      math::simd_float4::Load1(2.f);
  math::Float2 ret;
  math::Store2PtrU(screen, &ret.x);
  return ret;
}

Record* Application::GetFpsRecord() { return fps_.get(); }

const Record* Application::GetFpsRecord() const { return fps_.get(); }

Record* Application::GetUpdateTimeRecord() { return update_time_.get(); }

const Record* Application::GetUpdateTimeRecord() const {
  return update_time_.get();
}

Record* Application::GetRenderTimeRecord() { return render_time_.get(); }

const Record* Application::GetRenderTimeRecord() const {
  return render_time_.get();
}

void Application::SetUseSampleGui(bool enabled) {
  use_sample_gui_ = enabled;
  if (!use_sample_gui_) {
    im_gui_.reset();
  }
}

Application* Application::GetCurrent() { return application_; }

double Application::MouseWheelPosition() {
  return application_ ? application_->mouse_wheel_position_ : 0.0;
}

void Application::ResizeCbk(GLFWwindow* _window, int _width, int _height) {
  Application* app =
      reinterpret_cast<Application*>(glfwGetWindowUserPointer(_window));
  if (!app) {
    return;
  }

  int framebuffer_width = _width;
  int framebuffer_height = _height;
  if (_window) {
    glfwGetFramebufferSize(_window, &framebuffer_width, &framebuffer_height);
  }

  app->resolution_.width = framebuffer_width;
  app->resolution_.height = framebuffer_height;

  GL(Viewport(0, 0, framebuffer_width, framebuffer_height));

  if (app->camera_) {
    app->camera_->Resize(framebuffer_width, framebuffer_height);
  }
  if (app->shooter_) {
    app->shooter_->Resize(framebuffer_width, framebuffer_height);
  }
}

void Application::CloseCbk(GLFWwindow* _window) {
  Application* app =
      reinterpret_cast<Application*>(glfwGetWindowUserPointer(_window));
  if (!app) {
    return;
  }
  app->exit_ = true;
  if (_window) {
    glfwSetWindowShouldClose(_window, GLFW_FALSE);
  }
}

void Application::ScrollCbk(GLFWwindow* _window, double /*_xoffset*/,
                            double _yoffset) {
  Application* app =
      reinterpret_cast<Application*>(glfwGetWindowUserPointer(_window));
  if (!app) {
    return;
  }
  app->mouse_wheel_position_ += _yoffset;
}

void Application::ParseReadme() {
  const char* error_message = "Unable to find README.md help file.";

  // Get README file, opens as binary to avoid conversions.
  ozz::io::File file("README.md", "rb");
  if (!file.opened()) {
    help_ = error_message;
    return;
  }

  // Allocate enough space to store the whole file.
  const size_t read_length = file.Size();
  ozz::memory::Allocator* allocator = ozz::memory::default_allocator();
  char* content = reinterpret_cast<char*>(allocator->Allocate(read_length, 4));

  // Read the content
  if (file.Read(content, read_length) == read_length) {
    help_ = ozz::string(content, content + read_length);
  } else {
    help_ = error_message;
  }

  // Deallocate temporary buffer;
  allocator->Deallocate(content);
}
}  // namespace sample
}  // namespace ozz
