#include <obs-module.h>
#ifdef _WIN32
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
using namespace Microsoft::WRL; //ComPtr
#endif
#include "mdk/Player.h"
using namespace MDK_NS;

#define MS_ENSURE(f, ...) MS_CHECK(f, return __VA_ARGS__;)
#define MS_WARN(f) MS_CHECK(f)
#define MS_CHECK(f, ...)  do { \
        while (FAILED(GetLastError())) {} \
        HRESULT __ms_hr__ = f; \
        if (FAILED(__ms_hr__)) { \
            blog(LOG_WARNING, #f "  ERROR@%d %s: (%#x) ", __LINE__, __FUNCTION__, __ms_hr__); \
            __VA_ARGS__ \
        } \
    } while (false)

constexpr int kMaxSpeedPercent = 400;
class mdkVideoSource {
public:
  mdkVideoSource(obs_source_t* src) : source_(src) {
    setLogHandler([](LogLevel, const char* msg) {
      blog(LOG_INFO, "%s", msg);
      });
    player_.onMediaStatusChanged([this](MediaStatus s) {
      if (flags_added(status_, s, MediaStatus::Loaded)) {
        auto info = player_.mediaInfo();
        w_ = info.video[0].codec.width;
        h_ = info.video[0].codec.height;
      }
      status_ = s;
      return true;
      });
  }

  ~mdkVideoSource() {
    setLogHandler(nullptr);

    obs_enter_graphics();
    gs_texrender_destroy(texrender_);
    obs_leave_graphics();
  }


  gs_texture_t* render() {
    if (!ensureRTV())
      return nullptr;
    player_.renderVideo();
    gs_texrender_end(texrender_);
    return tex_;
  }

  void play(const char* url) {
    player_.setNextMedia(nullptr);
    player_.setState(State::Stopped);
    player_.waitFor(State::Stopped);
    player_.setMedia(nullptr); // 1st url may be the same as current url
    player_.setMedia(url);
    player_.setState(State::Playing);
  }

  uint32_t width() const { return w_; }
  uint32_t height() const { return h_; }
  uint32_t flip() const { return flip_; }

  Player player_;
private:
  bool ensureRTV() {
    if (w_ <= 0 || h_ <= 0)
      return false;
    gs_texrender_reset(texrender_);
    if (!gs_texrender_begin(texrender_, w_, h_)) {
      blog(LOG_ERROR, "failed to begin texrender");
      return false;
    }
    auto tex = gs_texrender_get_texture(texrender_);
    if (tex == tex_)
      return tex_;
    tex_ = tex;
    if (!tex_)
      return false;
#ifdef _WIN32
    if (gs_get_device_type() == GS_DEVICE_DIRECT3D_11) {
      flip_ = 0;
      tex11_ = (ID3D11Texture2D*)gs_texture_get_obj(tex_);
      ComPtr<ID3D11Device> dev11;
      tex11_->GetDevice(&dev11);
      MS_ENSURE(dev11->CreateRenderTargetView(tex11_.Get(), nullptr, &rtv11_), nullptr);
      dev11->GetImmediateContext(&ctx11_);
      D3D11RenderAPI ra;
      ra.context = ctx11_.Get();
      ra.rtv = rtv11_.Get();
      player_.setRenderAPI(&ra);
    }
#endif
    player_.setVideoSurfaceSize(w_, h_);
    return true;
  }

  obs_source_t* source_ = nullptr;
  gs_texture_t* tex_ = nullptr;
  // required by opengl. d3d11 can simply use a texture as rtv, but opengl needs gl api calls here, which is not trival to support all cases because glx or egl used by obs is unknown(mdk does know that)
  gs_texrender_t* texrender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
  uint32_t flip_ = GS_FLIP_V;
  uint32_t w_ = 0;
  uint32_t h_ = 0;

#ifdef _WIN32
  ComPtr<ID3D11DeviceContext> ctx11_;
  ComPtr<ID3D11Texture2D> tex11_;
  ComPtr<ID3D11RenderTargetView> rtv11_;
#endif
  MediaStatus status_;
};

/* ------------------------------------------------------------------------- */

static const char* mdkvideo_getname(void*)
{
  return "MDKVideo";
}

static void mdkvideo_update(void* data, obs_data_t* settings)
{
  auto obj = static_cast<mdkVideoSource*>(data);
  const char* url = obs_data_get_string(settings, "local_file");
  bool loop = obs_data_get_bool(settings, "looping");
  auto speed_percent = (int)obs_data_get_int(settings, "speed_percent");
  if (speed_percent < 1 || speed_percent > kMaxSpeedPercent)
    speed_percent = 100;
  obj->player_.setLoop(loop ? -1 : 0);
  obj->player_.setPlaybackRate(float(speed_percent) / 100.0f);
  auto dec = obs_data_get_string(settings, "gpudecoder");
  obj->player_.setVideoDecoders({ dec, "FFmpeg" });
  auto oldUrl = obj->player_.url();
  if (!oldUrl || strcmp(oldUrl, url) != 0)
    obj->play(url);
}

static void* mdkvideo_create(obs_data_t* settings, obs_source_t* source)
{
  auto obj = new mdkVideoSource(source);
  mdkvideo_update(obj, settings);
  return obj;
}

static void mdkvideo_destroy(void* data)
{
  auto obj = static_cast<mdkVideoSource*>(data);
  delete obj;
}

static uint32_t mdkvideo_width(void* data)
{
  auto obj = static_cast<mdkVideoSource*>(data);
  return obj->width();
}

static uint32_t mdkvideo_height(void* data)
{
  auto obj = static_cast<mdkVideoSource*>(data);
  return obj->height();
}

static void mdkvideo_defaults(obs_data_t* settings)
{
  obs_data_set_default_bool(settings, "looping", false);
  obs_data_set_default_int(settings, "speed_percent", 100);
}

static obs_properties_t* mdkvideo_properties(void* data)
{
  auto obj = static_cast<mdkVideoSource*>(data);
  auto* props = obs_properties_create();
  auto p = obs_properties_add_list(props, "gpudecoder", obs_module_text("GPUDecoder"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(p, "None", "FFmpeg");
#if defined(_WIN32)
  obs_property_list_add_string(p, "D3D11 via MFT", "MFT:d3d=11");
  obs_property_list_add_string(p, "DXVA via MFT", "MFT:d3d=11");
  obs_property_list_add_string(p, "D3D11", "D3D11");
  obs_property_list_add_string(p, "DXVA", "DXVA");
#elif defined(__APPLE__)
  obs_property_list_add_string(p, "VideoToolbox", "VideoToolbox");
#else
  obs_property_list_add_string(p, "VA-API", "VAAPI");
  obs_property_list_add_string(p, "VDPAU", "VDPAU");
#endif
  obs_property_list_add_string(p, "CUDA", "CUDA");
  obs_property_list_add_string(p, "NVDEC", "NVDEC");
  obs_properties_add_path(props, "local_file", obs_module_text("LocalFile"), OBS_PATH_FILE, nullptr, nullptr);
  obs_properties_add_bool(props, "looping", obs_module_text("Looping"));
  auto prop = obs_properties_add_int_slider(props, "speed_percent", obs_module_text("SpeedPercentage"), 1, kMaxSpeedPercent, 1);
  obs_property_int_set_suffix(prop, "%");
  return props;
}

static void mdkvideo_tick(void* data, float seconds)
{
  auto obj = static_cast<mdkVideoSource*>(data);
}

static void mdkvideo_render(void* data, gs_effect_t* effect)
{
  auto obj = static_cast<mdkVideoSource*>(data);
  auto tex = obj->render();
  if (!tex)
    return;
  if (effect) { // if no OBS_SOURCE_CUSTOM_DRAW
    gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex);
    gs_draw_sprite(tex, obj->flip(), obj->width(), obj->height());
  } else {
    effect = obs_get_base_effect(OBS_EFFECT_OPAQUE);
    gs_eparam_t *image =gs_effect_get_param_by_name(effect, "image");
    gs_effect_set_texture(image, tex);
    while (gs_effect_loop(effect, "Draw"))
      gs_draw_sprite(tex, obj->flip(), 0, 0);
  }
}

extern "C" void register_mdkvideo()
{
  static obs_source_info info;
  info.id = "mdkvideo";
  info.type = OBS_SOURCE_TYPE_INPUT;
  info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
  info.get_name = mdkvideo_getname;
  info.create = mdkvideo_create;
  info.destroy = mdkvideo_destroy;
  info.update = mdkvideo_update;
  info.video_render = mdkvideo_render;
  //info.video_tick = mdkvideo_tick;
  info.get_width = mdkvideo_width;
  info.get_height = mdkvideo_height;
  info.get_defaults = mdkvideo_defaults;
  info.get_properties = mdkvideo_properties;
  obs_register_source(&info);
}
