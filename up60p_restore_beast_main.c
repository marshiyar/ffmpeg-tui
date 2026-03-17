#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <mach-o/dyld.h>

#if defined(__has_include)
#  if __has_include("upscaler/up60p.h")
#    define HAVE_UP60P_HEADER 1
#    include "upscaler/up60p.h"
#  else
#    define HAVE_UP60P_HEADER 0
#  endif
#else
#  define HAVE_UP60P_HEADER 0
#endif

#if !HAVE_UP60P_HEADER && defined(UP60P_LIBRARY_MODE)
#  error "UP60P_LIBRARY_MODE requires upscaler/up60p.h"
#endif

/* ---------- Macros & Globals ---------- */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define ARR_LEN(a) ((int)(sizeof(a)/sizeof((a)[0])))

#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_CYAN    "\033[36m"

static const char *SCRIPT_NAME = "up60p_restore_beast";
static char FFMPEG_PATH_BUF[PATH_MAX];
int DRY_RUN = 0;

/* ---------- Help Text ---------- */
static const char *HELP_TEXT =
"\n"
"USAGE:\n"
"  ./%s <input> [options]\n"
"  ./%s --settings\n"
"\n"
"CODEC / RATE CONTROL:\n"
"  --hevc                   Use HEVC/H.265 (default: H.264)\n"
"  --crf <0-51>             Constant Rate Factor (default 16)\n"
"  --preset <name>          Encoder preset (default slow)\n"
"  --10bit                  Output yuv420p10le (or p010le for HW)\n"
"  --x265-params <str>      Pass args to x265 (e.g. 'aq-mode=3:psy-rd=2.0')\n"
"\n"
"FRAME / SCALE:\n"
"  --fps <1-240|source>     Target FPS (default: 60). Use 'source' to lock FPS.\n"
"  --scale <0.1-10>         Upscale factor (default: 2).\n"
"  --mi-mode <mci|blend>    Interpolation method (default: mci)\n"
"\n"
"AI UPSCALING:\n"
"  --scaler <ai|lanczos|zscale|hw> Select upscaler (default: ai)\n"
"  --ai-backend <sr|dnn>    AI filter choice (default: sr).\n"
"  --ai-model <file>        Path to model (.pb/.model). Required for --scaler ai.\n"
"  --dnn-backend <name>     native|tensorflow|openvino\n"
"\n"
"FILTERS (Denoise/Deblock/Sharpen):\n"
"  --denoiser <bm3d|nlmeans|hqdn3d|atadenoise> (default: bm3d)\n"
"  --denoise-strength <f|auto>  Sigma value or 'auto' (default: 2.5)\n"
"  --dering                 Enable ringing artifact removal\n"
"  --sharpen-method <cas|unsharp>\n"
"  --usm-radius <3-23>      Unsharp Mask Radius (default: 5)\n"
"  --deband-method <deband|gradfun>\n"
"\n"
"COLOR / I/O:\n"
"  --movflags <flags>       MOV container flags (default: +faststart)\n"
"\n"
"SETTINGS MODE:\n"
"  --settings               Launch interactive menu.\n";

static const char *MANUAL_TEXT = "Refer to interactive settings for full documentation.\n";

/* ---------- Settings Struct ---------- */
typedef struct {
    /* Core */
    char codec[8]; char crf[16]; char preset[32];
    char fps[16]; // "60", "source"
    char scale_factor[16];
    
    /* Scaler */
    char scaler[16]; char ai_backend[16]; char ai_model[PATH_MAX];
    char ai_model_type[16]; char dnn_backend[32];

    /* Filters - First Set */
    char denoiser[16]; char denoise_strength[16];
    char deblock_mode[16]; char deblock_thresh[64];
    int  dering_active; char dering_strength[16];

    char sharpen_method[16]; char sharpen_strength[32];
    char usm_radius[16]; char usm_amount[16]; char usm_threshold[16];

    char deband_method[16]; // deband, gradfun
    char deband_strength[32];

    char grain_strength[16];
    
    /* Filters - Second Set */
    char denoiser_2[16]; char denoise_strength_2[16];
    char deblock_mode_2[16]; char deblock_thresh_2[64];
    int  dering_active_2; char dering_strength_2[16];

    char sharpen_method_2[16]; char sharpen_strength_2[32];
    char usm_radius_2[16]; char usm_amount_2[16]; char usm_threshold_2[16];

    char deband_method_2[16];
    char deband_strength_2[32];

    char grain_strength_2[16];
    
    /* Individual toggles for second set */
    int use_denoise_2;
    int use_deblock_2;
    int use_dering_2;
    int use_sharpen_2;
    int use_deband_2;
    int use_grain_2;

    char mi_mode[16];

    char eq_contrast[16]; char eq_brightness[16]; char eq_saturation[16];

    char x265_params[256];

    /* I/O */
    char outdir[PATH_MAX]; char audio_bitrate[32]; char threads[16];
    char movflags[32];
    int  use10;

    /* Toggles */
    int no_deblock, no_denoise, no_decimate, no_interpolate;
    int no_sharpen, no_deband, no_eq, no_grain;

    /* HW */
    char hwaccel[16]; char encoder[16];
} Settings;

static Settings DEF;
static Settings S;

static char GPTPRO_PRESET_DIR[PATH_MAX];
static char GPTPRO_ACTIVE_FILE[PATH_MAX];

#ifdef UP60P_LIBRARY_MODE
// Optional callback for logging (can be NULL if not needed)
void (*global_log_cb)(const char *) = NULL;

// Helper function for logging (uses callback if available, otherwise printf)
void log_message(const char *format, ...) {
    if (global_log_cb) {
        char buffer[4096];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        global_log_cb(buffer);
    } else {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
}
#endif

/* ---------- Bridge Helpers: Settings <-> up60p_options ---------- */
#if HAVE_UP60P_HEADER
static void up60p_options_from_settings(up60p_options *dst, const Settings *src) {
    if (!dst || !src) return;
    memset(dst, 0, sizeof(*dst));

    /* Core */
    snprintf(dst->codec,        sizeof(dst->codec),        "%s", src->codec);
    snprintf(dst->crf,          sizeof(dst->crf),          "%s", src->crf);
    snprintf(dst->preset,       sizeof(dst->preset),       "%s", src->preset);
    snprintf(dst->fps,          sizeof(dst->fps),          "%s", src->fps);
    snprintf(dst->scale_factor, sizeof(dst->scale_factor), "%s", src->scale_factor);

    /* Scaler / AI */
    snprintf(dst->scaler,       sizeof(dst->scaler),       "%s", src->scaler);
    snprintf(dst->ai_backend,   sizeof(dst->ai_backend),   "%s", src->ai_backend);
    snprintf(dst->ai_model,     sizeof(dst->ai_model),     "%s", src->ai_model);
    snprintf(dst->ai_model_type,sizeof(dst->ai_model_type),"%s", src->ai_model_type);
    snprintf(dst->dnn_backend,  sizeof(dst->dnn_backend),  "%s", src->dnn_backend);

    /* Filters – First Set */
    snprintf(dst->denoiser,         sizeof(dst->denoiser),         "%s", src->denoiser);
    snprintf(dst->denoise_strength, sizeof(dst->denoise_strength), "%s", src->denoise_strength);
    snprintf(dst->deblock_mode,     sizeof(dst->deblock_mode),     "%s", src->deblock_mode);
    snprintf(dst->deblock_thresh,   sizeof(dst->deblock_thresh),   "%s", src->deblock_thresh);
    dst->dering_active = src->dering_active;
    snprintf(dst->dering_strength,  sizeof(dst->dering_strength),  "%s", src->dering_strength);

    snprintf(dst->sharpen_method,   sizeof(dst->sharpen_method),   "%s", src->sharpen_method);
    snprintf(dst->sharpen_strength, sizeof(dst->sharpen_strength), "%s", src->sharpen_strength);
    snprintf(dst->usm_radius,       sizeof(dst->usm_radius),       "%s", src->usm_radius);
    snprintf(dst->usm_amount,       sizeof(dst->usm_amount),       "%s", src->usm_amount);
    snprintf(dst->usm_threshold,    sizeof(dst->usm_threshold),    "%s", src->usm_threshold);

    snprintf(dst->deband_method,    sizeof(dst->deband_method),    "%s", src->deband_method);
    snprintf(dst->deband_strength,  sizeof(dst->deband_strength),  "%s", src->deband_strength);
    snprintf(dst->grain_strength,   sizeof(dst->grain_strength),   "%s", src->grain_strength);

    /* Filters – Second Set */
    snprintf(dst->denoiser_2,         sizeof(dst->denoiser_2),         "%s", src->denoiser_2);
    snprintf(dst->denoise_strength_2, sizeof(dst->denoise_strength_2), "%s", src->denoise_strength_2);
    snprintf(dst->deblock_mode_2,     sizeof(dst->deblock_mode_2),     "%s", src->deblock_mode_2);
    snprintf(dst->deblock_thresh_2,   sizeof(dst->deblock_thresh_2),   "%s", src->deblock_thresh_2);
    dst->dering_active_2 = src->dering_active_2;
    snprintf(dst->dering_strength_2,  sizeof(dst->dering_strength_2),  "%s", src->dering_strength_2);

    snprintf(dst->sharpen_method_2,   sizeof(dst->sharpen_method_2),   "%s", src->sharpen_method_2);
    snprintf(dst->sharpen_strength_2, sizeof(dst->sharpen_strength_2), "%s", src->sharpen_strength_2);
    snprintf(dst->usm_radius_2,       sizeof(dst->usm_radius_2),       "%s", src->usm_radius_2);
    snprintf(dst->usm_amount_2,       sizeof(dst->usm_amount_2),       "%s", src->usm_amount_2);
    snprintf(dst->usm_threshold_2,    sizeof(dst->usm_threshold_2),    "%s", src->usm_threshold_2);

    snprintf(dst->deband_method_2,    sizeof(dst->deband_method_2),    "%s", src->deband_method_2);
    snprintf(dst->deband_strength_2,  sizeof(dst->deband_strength_2),  "%s", src->deband_strength_2);
    snprintf(dst->grain_strength_2,   sizeof(dst->grain_strength_2),   "%s", src->grain_strength_2);

    dst->use_denoise_2 = src->use_denoise_2;
    dst->use_deblock_2 = src->use_deblock_2;
    dst->use_dering_2  = src->use_dering_2;
    dst->use_sharpen_2 = src->use_sharpen_2;
    dst->use_deband_2  = src->use_deband_2;
    dst->use_grain_2   = src->use_grain_2;

    snprintf(dst->mi_mode, sizeof(dst->mi_mode), "%s", src->mi_mode);

    snprintf(dst->eq_contrast,   sizeof(dst->eq_contrast),   "%s", src->eq_contrast);
    snprintf(dst->eq_brightness, sizeof(dst->eq_brightness), "%s", src->eq_brightness);
    snprintf(dst->eq_saturation, sizeof(dst->eq_saturation), "%s", src->eq_saturation);

    snprintf(dst->x265_params,   sizeof(dst->x265_params),   "%s", src->x265_params);

    snprintf(dst->outdir,        sizeof(dst->outdir),        "%s", src->outdir);
    snprintf(dst->audio_bitrate, sizeof(dst->audio_bitrate), "%s", src->audio_bitrate);
    snprintf(dst->threads,       sizeof(dst->threads),       "%s", src->threads);
    snprintf(dst->movflags,      sizeof(dst->movflags),      "%s", src->movflags);

    dst->use10    = src->use10;

    dst->no_deblock     = src->no_deblock;
    dst->no_denoise     = src->no_denoise;
    dst->no_decimate    = src->no_decimate;
    dst->no_interpolate = src->no_interpolate;
    dst->no_sharpen     = src->no_sharpen;
    dst->no_deband      = src->no_deband;
    dst->no_eq          = src->no_eq;
    dst->no_grain       = src->no_grain;

    snprintf(dst->hwaccel, sizeof(dst->hwaccel), "%s", src->hwaccel);
    snprintf(dst->encoder, sizeof(dst->encoder), "%s", src->encoder);
}

static void settings_from_up60p_options(Settings *dst, const up60p_options *src) {
    if (!dst || !src) return;

    *dst = DEF; /* start from factory defaults */

    /* Core */
    snprintf(dst->codec,        sizeof(dst->codec),        "%s", src->codec);
    snprintf(dst->crf,          sizeof(dst->crf),          "%s", src->crf);
    snprintf(dst->preset,       sizeof(dst->preset),       "%s", src->preset);
    snprintf(dst->fps,          sizeof(dst->fps),          "%s", src->fps);
    snprintf(dst->scale_factor, sizeof(dst->scale_factor), "%s", src->scale_factor);

    /* Scaler / AI */
    snprintf(dst->scaler,       sizeof(dst->scaler),       "%s", src->scaler);

    /* Filters – First Set */
    snprintf(dst->denoiser,         sizeof(dst->denoiser),         "%s", src->denoiser);
    snprintf(dst->denoise_strength, sizeof(dst->denoise_strength), "%s", src->denoise_strength);
    snprintf(dst->deblock_mode,     sizeof(dst->deblock_mode),     "%s", src->deblock_mode);
    snprintf(dst->deblock_thresh,   sizeof(dst->deblock_thresh),   "%s", src->deblock_thresh);
    dst->dering_active = src->dering_active;
    snprintf(dst->dering_strength,  sizeof(dst->dering_strength),  "%s", src->dering_strength);

    snprintf(dst->sharpen_method,   sizeof(dst->sharpen_method),   "%s", src->sharpen_method);
    snprintf(dst->sharpen_strength, sizeof(dst->sharpen_strength), "%s", src->sharpen_strength);
    snprintf(dst->usm_radius,       sizeof(dst->usm_radius),       "%s", src->usm_radius);
    snprintf(dst->usm_amount,       sizeof(dst->usm_amount),       "%s", src->usm_amount);
    snprintf(dst->usm_threshold,    sizeof(dst->usm_threshold),    "%s", src->usm_threshold);

    snprintf(dst->deband_method,    sizeof(dst->deband_method),    "%s", src->deband_method);
    snprintf(dst->deband_strength,  sizeof(dst->deband_strength),  "%s", src->deband_strength);
    snprintf(dst->grain_strength,   sizeof(dst->grain_strength),   "%s", src->grain_strength);

    /* Filters – Second Set */
    snprintf(dst->denoiser_2,         sizeof(dst->denoiser_2),         "%s", src->denoiser_2);
    snprintf(dst->denoise_strength_2, sizeof(dst->denoise_strength_2), "%s", src->denoise_strength_2);
    snprintf(dst->deblock_mode_2,     sizeof(dst->deblock_mode_2),     "%s", src->deblock_mode_2);
    snprintf(dst->deblock_thresh_2,   sizeof(dst->deblock_thresh_2),   "%s", src->deblock_thresh_2);
    dst->dering_active_2 = src->dering_active_2;
    snprintf(dst->dering_strength_2,  sizeof(dst->dering_strength_2),  "%s", src->dering_strength_2);

    snprintf(dst->sharpen_method_2,   sizeof(dst->sharpen_method_2),   "%s", src->sharpen_method_2);
    snprintf(dst->sharpen_strength_2, sizeof(dst->sharpen_strength_2), "%s", src->sharpen_strength_2);
    snprintf(dst->usm_radius_2,       sizeof(dst->usm_radius_2),       "%s", src->usm_radius_2);
    snprintf(dst->usm_amount_2,       sizeof(dst->usm_amount_2),       "%s", src->usm_amount_2);
    snprintf(dst->usm_threshold_2,    sizeof(dst->usm_threshold_2),    "%s", src->usm_threshold_2);

    snprintf(dst->deband_method_2,    sizeof(dst->deband_method_2),    "%s", src->deband_method_2);
    snprintf(dst->deband_strength_2,  sizeof(dst->deband_strength_2),  "%s", src->deband_strength_2);
    snprintf(dst->grain_strength_2,   sizeof(dst->grain_strength_2),   "%s", src->grain_strength_2);

    dst->use_denoise_2 = src->use_denoise_2;
    dst->use_deblock_2 = src->use_deblock_2;
    dst->use_dering_2  = src->use_dering_2;
    dst->use_sharpen_2 = src->use_sharpen_2;
    dst->use_deband_2  = src->use_deband_2;
    dst->use_grain_2   = src->use_grain_2;

    snprintf(dst->mi_mode, sizeof(dst->mi_mode), "%s", src->mi_mode);

    snprintf(dst->eq_contrast,   sizeof(dst->eq_contrast),   "%s", src->eq_contrast);
    snprintf(dst->eq_brightness, sizeof(dst->eq_brightness), "%s", src->eq_brightness);
    snprintf(dst->eq_saturation, sizeof(dst->eq_saturation), "%s", src->eq_saturation);

    snprintf(dst->x265_params,   sizeof(dst->x265_params),   "%s", src->x265_params);

    snprintf(dst->outdir,        sizeof(dst->outdir),        "%s", src->outdir);
    snprintf(dst->audio_bitrate, sizeof(dst->audio_bitrate), "%s", src->audio_bitrate);
    snprintf(dst->threads,       sizeof(dst->threads),       "%s", src->threads);
    snprintf(dst->movflags,      sizeof(dst->movflags),      "%s", src->movflags);

    dst->use10    = src->use10;

    dst->no_deblock     = src->no_deblock;
    dst->no_denoise     = src->no_denoise;
    dst->no_decimate    = src->no_decimate;
    dst->no_interpolate = src->no_interpolate;
    dst->no_sharpen     = src->no_sharpen;
    dst->no_deband      = src->no_deband;
    dst->no_eq          = src->no_eq;
    dst->no_grain       = src->no_grain;

    snprintf(dst->hwaccel, sizeof(dst->hwaccel), "%s", src->hwaccel);
    snprintf(dst->encoder, sizeof(dst->encoder), "%s", src->encoder);
}
#endif

/* ---------- Prototypes ---------- */
static void process_file(const char *in, const char *ffmpeg, bool batch);
static void process_directory(const char *dir, const char *ffmpeg);
static int ar_menu_choose(const char *prompt, const char **items, int n, int start_index);
static void set_defaults(void);
static void reset_to_factory(void);


/* ---------- Utilities ---------- */
#include "src/utilities.inc"

/* ---------- TUI System ---------- */
#include "src/tui.inc"

/* ---------- CLI Parsing ---------- */
#include "src/cli.inc"

/* ---------- Processing Logic ---------- */
#include "src/processing.inc"

/* ---------- Public API Implementation ---------- */
#include "src/public_api.inc"
