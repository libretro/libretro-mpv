#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include <mpv/client.h>
#include <mpv/opengl_cb.h>

#include "libretro.h"

static struct retro_log_callback logging;
static retro_log_printf_t log_cb;

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static mpv_handle *mpv;
static mpv_opengl_cb_context *mpv_gl;

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
   (void)level;
   va_list va;
   va_start(va, fmt);
   vfprintf(stderr, fmt, va);
   va_end(va);
}

void retro_init(void)
{
	mpv = mpv_create();

	log_cb(RETRO_LOG_DEBUG, "Test\n");

	if(!mpv)
		log_cb(RETRO_LOG_ERROR, "failed creating context\n");

	if(mpv_initialize(mpv) < 0)
		log_cb(RETRO_LOG_ERROR, "mpv init failed");

    // The OpenGL API is somewhat separate from the normal mpv API. This only
    // returns NULL if no OpenGL support is compiled.
    mpv_opengl_cb_context *mpv_gl = mpv_get_sub_api(mpv, MPV_SUB_API_OPENGL_CB);
    if(!mpv_gl)
		log_cb(RETRO_LOG_ERROR, "failed to create mpv GL API handle");

	// Actually using the opengl_cb state has to be explicitly requested.
	// Otherwise, mpv will create a separate platform window.
	if(mpv_set_option_string(mpv, "vo", "opengl-cb") < 0)
		log_cb(RETRO_LOG_ERROR, "failed to set VO");

	if(mpv_set_option_string(mpv, "ao", "null") < 0)
		log_cb(RETRO_LOG_ERROR, "failed to set AO");

	return;
}

void retro_deinit(void)
{
    mpv_terminate_destroy(mpv);
	return;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   log_cb(RETRO_LOG_INFO, "Plugging device %u into port %u.\n", device, port);
	return;
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "mpv";
   info->library_version  = "v1";
   info->need_fullpath    = true;
   info->valid_extensions = "mkv|avi|f4v|f4f|3gp|ogm|flv|mp4|mp3|flac|ogg|m4a|webm|3g2|mov|wmv|mpg|mpeg|vob|asf|divx|m2p|m2ts|ps|ts|mxf|wma|wav";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
	float sampling_rate = 30000.0f;

	struct retro_variable var = { .key = "test_aspect" };
	environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var);

	var.key = "test_samplerate";

	if(environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
		sampling_rate = strtof(var.value, NULL);

	info->timing = (struct retro_system_timing) {
		.fps = 60.0,
		.sample_rate = sampling_rate,
	};

	info->geometry = (struct retro_game_geometry) {
		.base_width   = 320,
		.base_height  = 240,
		.max_width    = 320,
		.max_height   = 240,
	};
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   static const struct retro_variable vars[] = {
      { "test_samplerate", "Sample Rate; 30000|20000" },
      { "test_opt0", "Test option #0; false|true" },
      { "test_opt1", "Test option #1; 0" },
      { "test_opt2", "Test option #2; 0|1|foo|3" },
      { NULL, NULL },
   };

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);

   bool no_content = true;
   cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

   if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
      log_cb = logging.log;
   else
      log_cb = fallback_log;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_reset(void)
{
	return;
}

static void audio_callback(void)
{
	static unsigned phase;

	for (unsigned i = 0; i < 30000 / 60; i++, phase++)
	{
		int16_t val = 0x800 * sinf(2.0f * M_PI * phase * 300.0f / 30000.0f);
		audio_cb(val, val);
	}

	phase %= 100;
}

void retro_run(void)
{
	audio_callback();

	return;
}

/* No save-state support */
size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data_, size_t size)
{
   return true;
}

bool retro_unserialize(const void *data_, size_t size)
{
   return true;
}

bool retro_load_game(const struct retro_game_info *info)
{
    const char *cmd[] = {"loadfile", info->path, NULL};
    mpv_command(mpv, cmd);

	return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
	return false;
}

void retro_unload_game(void)
{
	return;
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   return 0;
}

void retro_cheat_reset(void)
{
	return;
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
	return;
}
