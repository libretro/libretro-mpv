#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>

#include <mpv/client.h>
#include <mpv/opengl_cb.h>

#include "libretro.h"
#include "version.h"

static struct retro_hw_render_callback hw_render;

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

/* Keep track of the number of events in mpv queue */
static unsigned int event_waiting = 0;

/* Save the current playback time for context changes */
static int64_t *playback_time = 0;
static char *filepath = NULL;

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
	(void)level;
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
}

static void on_mpv_events(void *mpv)
{
	event_waiting++;
}

void retro_init(void)
{
	return;
}

void retro_deinit(void)
{
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
	info->library_version  = LIBRETRO_MPV_VERSION;
	info->need_fullpath    = true;	/* Allow MPV to load the file on its own */
	info->valid_extensions = "mkv|avi|f4v|f4f|3gp|ogm|flv|mp4|mp3|flac|ogg|m4a|webm|3g2|mov|wmv|mpg|mpeg|vob|asf|divx|m2p|m2ts|ps|ts|mxf|wma|wav";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
	float sampling_rate = 48000.0f;

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
		.base_width   = 256,
		.base_height  = 144,
		.max_width    = 1920,
		.max_height   = 1080,
		.aspect_ratio = -1,
	};
}

void retro_set_environment(retro_environment_t cb)
{
	environ_cb = cb;

	static const struct retro_variable vars[] = {
		{ "test_samplerate", "Sample Rate; 48000|30000|20000" },
		{ "test_opt0", "Test option #0; false|true" },
		{ "test_opt1", "Test option #1; 0" },
		{ "test_opt2", "Test option #2; 0|1|foo|3" },
		{ NULL, NULL },
	};

	cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);

	if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
		log_cb = logging.log;
	else
		log_cb = fallback_log;
}

static void *get_proc_address_mpv(void *fn_ctx, const char *name)
{
	return (void *) hw_render.get_proc_address(name);
}

static void context_reset(void)
{
	const char *cmd[] = {"loadfile", filepath, NULL};

	mpv = mpv_create();

	if(!mpv)
	{
		log_cb(RETRO_LOG_ERROR, "failed creating context\n");
		return;
	}

	if(mpv_initialize(mpv) < 0)
	{
		log_cb(RETRO_LOG_ERROR, "mpv init failed\n");
		return;
	}

    // When normal mpv events are available.
    mpv_set_wakeup_callback(mpv, on_mpv_events, NULL);

	if(mpv_request_log_messages(mpv, "info") < 0)
	{
		log_cb(RETRO_LOG_ERROR, "mpv logging failed\n");
		return;
	}

	// The OpenGL API is somewhat separate from the normal mpv API. This only
	// returns NULL if no OpenGL support is compiled.
	mpv_gl = mpv_get_sub_api(mpv, MPV_SUB_API_OPENGL_CB);

	if(!mpv_gl)
	{
		log_cb(RETRO_LOG_ERROR, "failed to create mpv GL API handle\n");
		return;
	}

	if(mpv_opengl_cb_init_gl(mpv_gl, NULL, get_proc_address_mpv, NULL) < 0)
		log_cb(RETRO_LOG_ERROR, "failed to initialize mpv GL context\n");

	// Actually using the opengl_cb state has to be explicitly requested.
	// Otherwise, mpv will create a separate platform window.
	if(mpv_set_option_string(mpv, "vo", "opengl-cb") < 0)
	{
		log_cb(RETRO_LOG_ERROR, "failed to set VO");
		return;
	}

	if(mpv_set_option_string(mpv, "hwdec", "auto") < 0)
	{
		log_cb(RETRO_LOG_ERROR, "failed to enable hwdec");
		return;
	}

	if(mpv_command(mpv, cmd) != 0)
	{
		log_cb(RETRO_LOG_ERROR, "failed to issue mpv_command\n");
		return;
	}

	/* Keep trying until mpv accepts the property.
	 * This is done to seek to the point in the file after the previous context
	 * was destroyed. If now context was destroyed previously, the file seeks
	 * to 0.
	 *
	 * This also seems to fix some black screen issues.
	 */
	while(mpv_set_property(mpv, "playback-time", MPV_FORMAT_INT64, &playback_time) < 0)
	{}

	log_cb(RETRO_LOG_INFO, "Context reset.\n");
}

static void context_destroy(void)
{
	mpv_get_property(mpv, "playback-time", MPV_FORMAT_INT64, &playback_time);
	mpv_opengl_cb_uninit_gl(mpv_gl);
	mpv_terminate_destroy(mpv);
	log_cb(RETRO_LOG_INFO, "Context destroyed.\n");
}

#ifdef HAVE_OPENGLES
static bool retro_init_hw_context(void)
{
#if defined(HAVE_OPENGLES_3_1)
	hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES_VERSION;
	hw_render.version_major = 3;
	hw_render.version_minor = 1;
#elif defined(HAVE_OPENGLES3)
	hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES3;
#else
	hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES2;
#endif
	hw_render.context_reset = context_reset;
	hw_render.context_destroy = context_destroy;
	hw_render.depth = true;
	hw_render.bottom_left_origin = true;

	if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
		return false;

	return true;
}
#else
static bool retro_init_hw_context(void)
{
	hw_render.context_type = RETRO_HW_CONTEXT_OPENGL;
	hw_render.context_reset = context_reset;
	hw_render.context_destroy = context_destroy;

	if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
		return false;

	return true;
}
#endif

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

#if 0
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
#endif

static void retropad_update_input(void)
{
	input_poll_cb();

	if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
			RETRO_DEVICE_ID_JOYPAD_LEFT))
		mpv_command_string(mpv, "seek -5");

	if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
			RETRO_DEVICE_ID_JOYPAD_RIGHT))
		mpv_command_string(mpv, "seek 5");

	if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
			RETRO_DEVICE_ID_JOYPAD_UP))
		mpv_command_string(mpv, "seek 60");

	if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
			RETRO_DEVICE_ID_JOYPAD_DOWN))
		mpv_command_string(mpv, "seek -60");

	if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
			RETRO_DEVICE_ID_JOYPAD_L))
		mpv_command_string(mpv, "cycle audio");

	if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
			RETRO_DEVICE_ID_JOYPAD_R))
		mpv_command_string(mpv, "cycle sub");

	if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
			RETRO_DEVICE_ID_JOYPAD_A))
		mpv_command_string(mpv, "cycle pause");

	if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0,
			RETRO_DEVICE_ID_JOYPAD_X))
		mpv_command_string(mpv, "show-progress");
}

void retro_run(void)
{
	/* We only need to update the base video size once, and we do it here since
	 * the input file is not processed during the first
	 * retro_get_system_av_info() call.
	 */
	static bool updated_video_dimensions = false;
	static int64_t width, height;

	if(updated_video_dimensions == false)
	{
		mpv_get_property(mpv, "width", MPV_FORMAT_INT64, &width);
		mpv_get_property(mpv, "height", MPV_FORMAT_INT64, &height);

		struct retro_game_geometry geometry = {
			.base_width   = width,
			.base_height  = height,
			/* max_width and max_height are ignored */
			.max_width    = width,
			.max_height   = height,
			/* Aspect ratio calculated automatically from base dimensions */
			.aspect_ratio = -1,
		};

		environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
		updated_video_dimensions = true;
	}

	/* Print out logs */
	if(event_waiting > 0)
	{
		while(1)
		{
			mpv_event *mp_event = mpv_wait_event(mpv, 0);
			if(mp_event->event_id == MPV_EVENT_NONE)
				break;

			log_cb(RETRO_LOG_INFO, "mpv: ");
			if(mp_event->event_id == MPV_EVENT_LOG_MESSAGE)
			{
				struct mpv_event_log_message *msg =
					(struct mpv_event_log_message *)mp_event->data;
				log_cb(RETRO_LOG_INFO, "[%s] %s: %s",
						msg->prefix, msg->level, msg->text);
			}
			else
				log_cb(RETRO_LOG_INFO, "%s\n", mpv_event_name(mp_event->event_id));
		}

		event_waiting = 0;
	}

	retropad_update_input();
	/* TODO: Implement an audio callback feature in to libmpv */
	//audio_callback();

	mpv_opengl_cb_draw(mpv_gl, hw_render.get_current_framebuffer(), width, height);
	video_cb(RETRO_HW_FRAME_BUFFER_VALID, width, height, 0);
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
	/* Supported on most systems. */
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	struct retro_input_descriptor desc[] = {
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,  "Pause/Play" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,  "Show Progress" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Seek -5 seconds" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Seek +60 seconds" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Seek -60 seconds" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Seek +5 seconds" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "Cycle Audio Track" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "Cycle Subtitle Track" },

		{ 0 },
	};

	/* Copy the file path to a global variable as we need it in context_reset()
	 * where mpv is initialised.
	 */
	filepath = strdup(info->path);

	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

	/* Not bothered if this fails. Assuming the default is selected anyway. */
	if(!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
	{
		log_cb(RETRO_LOG_ERROR, "XRGB8888 is not supported.\n");

		/* Try RGB565 */
		fmt = RETRO_PIXEL_FORMAT_RGB565;
		environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
	}

	if(!retro_init_hw_context())
	{
		log_cb(RETRO_LOG_ERROR, "HW Context could not be initialized\n");
		return false;
	}

	return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info,
		size_t num)
{
	return false;
}

void retro_unload_game(void)
{
	free(filepath);
	filepath = NULL;

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
