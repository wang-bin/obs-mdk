#include <obs-module.h>
#include <util/platform.h>
#ifdef _WIN32
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
using namespace Microsoft::WRL; //ComPtr
#endif
#include "mdk/Player.h"
using namespace MDK_NS;
#include <list>
#include <regex>
using namespace std;

#define S_PLAYLIST "playlist"
#define S_LOOP "loop"
#define S_SHUFFLE "shuffle"

#define T_(text) obs_module_text(text)
#define T_PLAYLIST T_("Playlist")
#define T_LOOP T_("LoopPlaylist")
#define T_SHUFFLE T_("shuffle")

#define EXTENSIONS_VIDEO                                                       \
	"*.3gp *.3gpp *.asf *.avi;"                         \
	"*.dv *.evo *.f4v *.flv;"   \
	"*.m2v *.m2t *.m2ts *.m4v *.mkv *.mov *.mp2 *.mp2v *.mp4;" \
	"*.mp4v *.mpeg *.mpg *.mts;"      \
	"*.mtv *.mxf *.nsv *.nuv *.ogg *.ogm *.ogv;"    \
	"*.rm *.rmvb *.ts *.vob *.webm *.wm *.wmv"

#define EXTENSIONS_PLAYLIST "*.cue *.m3u *.m3u8 *.pls;"

#define EXTENSIONS_MEDIA \
	EXTENSIONS_VIDEO " " EXTENSIONS_PLAYLIST

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
	  next_it_ = urls_.cend();
	  setLogHandler([](LogLevel level, const char *msg) {
		  int lv = LOG_DEBUG;
		  switch (level) {
		  case LogLevel::Info:
			  lv = LOG_INFO;
			  break;
		  case LogLevel::Warning:
			  lv = LOG_WARNING;
			  break;
		  case LogLevel::Error:
			  lv = LOG_ERROR;
			  break;
          default:
              break;
		  }
		  blog(lv, "%s", msg);
	  });
    player_.onMediaStatusChanged([this](MediaStatus s) {
      if (flags_added(status_, s, MediaStatus::Loaded)) {
        auto info = player_.mediaInfo();
        w_ = info.video[0].codec.width;
        h_ = info.video[0].codec.height;
	obs_source_media_started(source_);
      }
      status_ = s;
      return true;
      });
    player_.currentMediaChanged([this] {
	    if (!player_.url())
		    return;
	    if (next_it_ == urls_.cend()) {
		    if (!loop_)
			    return;
		    next_it_ = urls_.cbegin();
	    }
	    player_.setNextMedia(next_it_->data());
	    std::advance(next_it_, 1);
    });

	player_.onStateChanged([this](State s) {
		if (s == State::Stopped)
			obs_source_media_stop(source_);
	});

	play_pause_hotkey = obs_hotkey_register_source(
		source_, "MDKVideoSource.PlayPause", obs_module_text("PlayPause"),
		hotkeyPlayPause, this);

	restart_hotkey = obs_hotkey_register_source(
		source_, "MDKVideoSource.Restart", obs_module_text("Restart"),
		hotkeyRestart, this);

	stop_hotkey = obs_hotkey_register_source(source_, "MDKVideoSource.Stop",
		obs_module_text("Stop"),
		hotkeyStop, this);
  }

  ~mdkVideoSource() {
    setLogHandler(nullptr); // TODO: in module unload

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
    player_.set(State::Stopped);
    player_.waitFor(State::Stopped);
    player_.setMedia(nullptr); // 1st url may be the same as current url
    player_.setMedia(url);
    player_.set(State::Playing);
  }

  uint32_t width() const { return w_; }
  uint32_t height() const { return h_; }
  uint32_t flip() const { return flip_; }

  void setUrls(const list<string> &urls, bool loop)
  {
	  loop_ = loop;
	  urls_ = urls;
	  player_.setNextMedia(nullptr);
	  if (urls_.empty())
		  return;
	  string next;
	  auto now = player_.url();
	  if (!now) {
		  next_it_ = urls_.cbegin();
		  if (++next_it_ == urls_.cend() && loop_)
			  next_it_ = urls_.cbegin();
		  play(urls_.front().data());
		  return;
	  }
	  auto it = find(urls_.cbegin(), urls_.cend(), now);
	  if (it != urls_.cend())
		  next_it_ = ++it;
	  if (it == urls_.cend()) {
		  if (!loop_) {
			  player_.setNextMedia(nullptr);
			  return;
		  }
		  next_it_ = urls_.cbegin();
	  }
	  player_.setNextMedia(next_it_->data());
  }

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
      D3D11RenderAPI ra{};
      ra.rtv = (ID3D11Texture2D *)gs_texture_get_obj(tex_);
      player_.setRenderAPI(&ra);
    }
#endif
    player_.setVideoSurfaceSize(w_, h_);
    return true;
  }


  static void hotkeyPlayPause(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
  {
	  auto c = static_cast<mdkVideoSource *>(data);
	  auto state = obs_source_media_get_state(c->source_);
	  if (pressed && obs_source_active(c->source_)) {
		  if (state == OBS_MEDIA_STATE_PLAYING)
			  obs_source_media_play_pause(c->source_, true);
		  else if (state == OBS_MEDIA_STATE_PAUSED)
			  obs_source_media_play_pause(c->source_, false);
	  }
  }

  static void hotkeyRestart(void *data, obs_hotkey_id, obs_hotkey_t*, bool pressed)
  {
	  auto c = static_cast<mdkVideoSource *>(data);
	  if (pressed && obs_source_active(c->source_))
		  obs_source_media_restart(c->source_);
  }

  static void hotkeyStop(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
  {
	  auto c = static_cast<mdkVideoSource *>(data);
	  if (pressed && obs_source_active(c->source_))
		  obs_source_media_stop(c->source_);
  }

  bool loop_ = true;

  obs_source_t *source_ = nullptr;
  gs_texture_t *tex_ = nullptr;
  // required by opengl. d3d11 can simply use a texture as rtv, but opengl needs gl api calls here, which is not trival to support all cases because glx or egl used by obs is unknown(mdk does know that)
  gs_texrender_t* texrender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
  uint32_t flip_ = GS_FLIP_V;
  uint32_t w_ = 0;
  uint32_t h_ = 0;

  MediaStatus status_;

  obs_hotkey_id play_pause_hotkey;
  obs_hotkey_id restart_hotkey;
  obs_hotkey_id stop_hotkey;
  obs_hotkey_id playlist_next_hotkey;
  obs_hotkey_id playlist_prev_hotkey;

  mutable list<string>::const_iterator next_it_;
  list<string> urls_;
};

/* ------------------------------------------------------------------------- */

static const char* mdkvideo_getname(void*)
{
  return "MDKVideo";
}

static void mdkvideo_update(void* data, obs_data_t* settings)
{
  auto obj = static_cast<mdkVideoSource*>(data);
  //const char* url = obs_data_get_string(settings, "local_file");
  bool loop = obs_data_get_bool(settings, "looping");
  auto speed_percent = (int)obs_data_get_int(settings, "speed_percent");
  if (speed_percent < 1 || speed_percent > kMaxSpeedPercent)
    speed_percent = 100;
  //obj->player_.setLoop(loop ? -1 : 0);
  obj->player_.setPlaybackRate(float(speed_percent) / 100.0f);
  string dec = obs_data_get_string(settings, "hwdecoder");
  std::regex re(",");
  std::sregex_token_iterator first{dec.begin(), dec.end(), re, -1}, last;
  vector<string> decs = { first, last };
  decs.insert(decs.end(), { "hap", "FFmpeg", "dav1d" });
  obj->player_.setDecoders(MediaType::Video, decs);

  auto urls = obs_data_get_array(settings, S_PLAYLIST);
  auto nb_urls = obs_data_array_count(urls);
  list<string> new_urls;
  for (size_t i = 0; i < nb_urls; i++) {
		obs_data_t *item = obs_data_array_item(urls, i);
		string p = obs_data_get_string(item, "value");
		auto dir = os_opendir(p.data());
		if (dir) {
			for (auto ent = os_readdir(dir); ent; ent = os_readdir(dir)) {
				if (ent->directory)
					continue;
				auto ext = os_get_path_extension(ent->d_name);
				if (strstr(EXTENSIONS_MEDIA, ext))
					new_urls.push_back(p + "/" + ent->d_name);
			}
			os_closedir(dir);
		} else {
			new_urls.push_back(p);
		}
		obs_data_release(item);
	}
	obj->setUrls(new_urls, loop);
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
  obs_data_set_default_bool(settings, "looping", true);
  obs_data_set_default_int(settings, "speed_percent", 100);
}

static obs_properties_t* mdkvideo_properties(void*)
{
  auto* props = obs_properties_create();
  auto p = obs_properties_add_list(props, "hwdecoder", obs_module_text("HWDecoder"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
  obs_property_list_add_string(p, "Auto",
#if defined(_WIN32)
      "MFT:d3d=11,D3D11,CUDA");
  obs_property_list_add_string(p, "D3D11 via MFT", "MFT:d3d=11");
  obs_property_list_add_string(p, "D3D12 via MFT", "MFT:d3d=12");
  obs_property_list_add_string(p, "D3D11", "D3D11");
#elif defined(__APPLE__)
    "VT");
  obs_property_list_add_string(p, "VT", "VT");
  obs_property_list_add_string(p, "VideoToolbox", "VideoToolbox");
#else
    "VAAPI,VDPAU,CUDA");
  obs_property_list_add_string(p, "VA-API", "VAAPI");
  obs_property_list_add_string(p, "VDPAU", "VDPAU");
#endif
#if !(__APPLE__+0)
  obs_property_list_add_string(p, "CUDA", "CUDA");
  obs_property_list_add_string(p, "NVDEC", "NVDEC");
#endif
  obs_property_list_add_string(p, "None", "FFmpeg");
  //obs_properties_add_path(props, "local_file", obs_module_text("LocalFile"), OBS_PATH_FILE, nullptr, nullptr);
  obs_properties_add_bool(props, "looping", obs_module_text("Looping"));
  auto prop = obs_properties_add_int_slider(props, "speed_percent", obs_module_text("SpeedPercentage"), 1, kMaxSpeedPercent, 1);
  obs_property_int_set_suffix(prop, "%");

  auto filters = string("MediaFiles (") + EXTENSIONS_MEDIA + ")";
  obs_properties_add_editable_list(props, S_PLAYLIST, T_PLAYLIST,
				   OBS_EDITABLE_LIST_TYPE_FILES_AND_URLS,
				   filters.data(), nullptr);
  return props;
}

#if 0
static void mdkvideo_tick(void* data, float /*seconds*/)
{
  auto obj = static_cast<mdkVideoSource*>(data);
}
#endif

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

static void mdkvideo_play_pause(void *data, bool pause)
{
	auto obj = static_cast<mdkVideoSource *>(data);
	obj->player_.set(pause ? State::Paused : State::Playing);
}

static void mdkvideo_stop(void *data)
{
	auto obj = static_cast<mdkVideoSource *>(data);
	obj->player_.setNextMedia(nullptr);
	obj->player_.set(State::Stopped);
}

static void mdkvideo_restart(void *data)
{
	auto obj = static_cast<mdkVideoSource *>(data);
	obj->player_.set(State::Playing);
}

static int64_t mdkvideo_get_duration(void *data)
{
	auto obj = static_cast<mdkVideoSource *>(data);
	return obj->player_.mediaInfo().duration;
}

static int64_t mdkvideo_get_time(void *data)
{
	auto obj = static_cast<mdkVideoSource *>(data);
	return obj->player_.position();
}

static void mdkvideo_set_time(void *data, int64_t ms)
{
	auto obj = static_cast<mdkVideoSource *>(data);
	obj->player_.seek(ms);
}

static enum obs_media_state mdkvideo_get_state(void *data)
{
	auto obj = static_cast<mdkVideoSource *>(data);
	auto s = obj->player_.mediaStatus();
	if (test_flag(s & MediaStatus::Loading))
		return OBS_MEDIA_STATE_OPENING;
	if (test_flag(s & MediaStatus::Buffering))
		return OBS_MEDIA_STATE_BUFFERING;
	switch (obj->player_.state()) {
	case State::Playing:
		return OBS_MEDIA_STATE_PLAYING;
	case State::Paused:
		return OBS_MEDIA_STATE_PAUSED;
	case State::Stopped:
		return OBS_MEDIA_STATE_STOPPED;
	default:
		break;
	}
	return OBS_MEDIA_STATE_NONE;
}

static void mdkvideo_activate(void *data)
{
	mdkvideo_play_pause(data, false);
}

static void mdkvideo_deactivate(void *data)
{
	mdkvideo_play_pause(data, true);
}

extern "C" void register_mdkvideo()
{
  static obs_source_info info;
  info.id = "mdkvideo";
  info.type = OBS_SOURCE_TYPE_INPUT;
  info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CONTROLLABLE_MEDIA | OBS_SOURCE_DO_NOT_DUPLICATE; // | OBS_SOURCE_CUSTOM_DRAW;
  info.get_name = mdkvideo_getname;
  info.create = mdkvideo_create;
  info.destroy = mdkvideo_destroy;
  info.update = mdkvideo_update;
  info.video_render = mdkvideo_render;
  //info.video_tick = mdkvideo_tick;
  info.activate = mdkvideo_activate;
  info.deactivate = mdkvideo_deactivate;
  info.get_width = mdkvideo_width;
  info.get_height = mdkvideo_height;
  info.get_defaults = mdkvideo_defaults;
  info.get_properties = mdkvideo_properties;
  info.icon_type = OBS_ICON_TYPE_MEDIA;
  info.media_play_pause = mdkvideo_play_pause;
  info.media_restart = mdkvideo_restart;
  info.media_stop = mdkvideo_stop;
  info.media_get_duration = mdkvideo_get_duration;
  info.media_get_time = mdkvideo_get_time;
  info.media_set_time = mdkvideo_set_time;
  info.media_get_state = mdkvideo_get_state;
  obs_register_source(&info);
}
