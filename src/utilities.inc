/* ---------- Utilities ---------- */
static void sanitize_path(char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    size_t len = strlen(p);
    while (len > 0 && isspace((unsigned char)p[len-1])) p[--len] = '\0';
    if (len > 2 && ((p[0] == '"' && p[len-1] == '"') || (p[0] == '\'' && p[0] == p[len-1]))) {
        memmove(p, p+1, len-2); p[len-2] = '\0'; len -= 2;
    }
    char *src = p, *dst = p;
    while (*src) {
        if (*src == '\\' && src[1] == ' ') { *dst++ = ' '; src += 2; } else *dst++ = *src++;
    } *dst = '\0';
}

static void init_paths(void) {
    char xdg[PATH_MAX];
    const char *env = getenv("XDG_CONFIG_HOME");
    if (env && *env) {
        snprintf(xdg, sizeof(xdg), "%s", env);
    } else {
        const char *home = getenv("HOME");
        if (home && *home) {
            snprintf(xdg, sizeof(xdg), "%s/.config", home);
        } else {
            // Fallback to /tmp if HOME is not set (shouldn't happen in normal macOS app)
            snprintf(xdg, sizeof(xdg), "/tmp");
        }
    }
    snprintf(GPTPRO_PRESET_DIR, sizeof(GPTPRO_PRESET_DIR), "%s/gptPro/presets", xdg);
    snprintf(GPTPRO_ACTIVE_FILE, sizeof(GPTPRO_ACTIVE_FILE), "%s/gptPro/active_preset", xdg);
}

static void mkdir_p(const char *path) {
    char tmp[PATH_MAX]; snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) { if (*p == '/') { *p = 0; mkdir(tmp, 0775); *p = '/'; } }
    mkdir(tmp, 0775);
}

static const char* get_bundled_ffmpeg_path(void) {
    char exe_path_raw[PATH_MAX]; uint32_t size = sizeof(exe_path_raw);
    if (_NSGetExecutablePath(exe_path_raw, &size) != 0) return NULL;
    char *exe_dir = dirname(exe_path_raw);
    char app_bundle_ffcore[PATH_MAX]; snprintf(app_bundle_ffcore, sizeof(app_bundle_ffcore), "%s/../../Contents/Resources/lib/ffcore", exe_dir);
    char app_bundle_ffmpeg[PATH_MAX]; snprintf(app_bundle_ffmpeg, sizeof(app_bundle_ffmpeg), "%s/../../Contents/Resources/lib/ffmpeg", exe_dir);
    char dev_ffcore[PATH_MAX]; snprintf(dev_ffcore, sizeof(dev_ffcore), "%s/ffcore", exe_dir);
    char dev_ffmpeg[PATH_MAX]; snprintf(dev_ffmpeg, sizeof(dev_ffmpeg), "%s/ffmpeg", exe_dir);
    if (access(app_bundle_ffcore, X_OK) == 0) { strncpy(FFMPEG_PATH_BUF, app_bundle_ffcore, sizeof(FFMPEG_PATH_BUF)); return FFMPEG_PATH_BUF; }
    if (access(app_bundle_ffmpeg, X_OK) == 0) { strncpy(FFMPEG_PATH_BUF, app_bundle_ffmpeg, sizeof(FFMPEG_PATH_BUF)); return FFMPEG_PATH_BUF; }
    if (access(dev_ffcore, X_OK) == 0) { strncpy(FFMPEG_PATH_BUF, dev_ffcore, sizeof(FFMPEG_PATH_BUF)); return FFMPEG_PATH_BUF; }
    if (access(dev_ffmpeg, X_OK) == 0) { strncpy(FFMPEG_PATH_BUF, dev_ffmpeg, sizeof(FFMPEG_PATH_BUF)); return FFMPEG_PATH_BUF; }
    return NULL;
}

static void set_defaults(void) {
    memset(&S, 0, sizeof(S));
    strcpy(S.codec, "h264"); strcpy(S.fps, "60"); strcpy(S.scale_factor, "2");
    strcpy(S.scaler, "lanczos"); strcpy(S.ai_backend, "sr"); strcpy(S.ai_model_type, "espcn"); strcpy(S.dnn_backend, "tensorflow");
    
    strcpy(S.denoiser, "bm3d"); strcpy(S.denoise_strength, "2.5");
    strcpy(S.deblock_mode, "strong");
    S.dering_active = 0; strcpy(S.dering_strength, "0.5");

    strcpy(S.sharpen_method, "cas"); strcpy(S.sharpen_strength, "0.25");
    strcpy(S.usm_radius, "5"); strcpy(S.usm_amount, "1.0"); strcpy(S.usm_threshold, "0.03");

    strcpy(S.deband_method, "deband"); strcpy(S.deband_strength, "0.015");

    strcpy(S.grain_strength, "1.0");
    
    // Second set defaults (initially same as first set)
    strcpy(S.denoiser_2, "bm3d"); strcpy(S.denoise_strength_2, "2.5");
    strcpy(S.deblock_mode_2, "strong");
    S.dering_active_2 = 0; strcpy(S.dering_strength_2, "0.5");

    strcpy(S.sharpen_method_2, "cas"); strcpy(S.sharpen_strength_2, "0.25");
    strcpy(S.usm_radius_2, "5"); strcpy(S.usm_amount_2, "1.0"); strcpy(S.usm_threshold_2, "0.03");

    strcpy(S.deband_method_2, "deband"); strcpy(S.deband_strength_2, "0.015");

    strcpy(S.grain_strength_2, "1.0");
    S.use_denoise_2 = 0;
    S.use_deblock_2 = 0;
    S.use_dering_2 = 0;
    S.use_sharpen_2 = 0;
    S.use_deband_2 = 0;
    S.use_grain_2 = 0;
    strcpy(S.mi_mode, "mci");
    strcpy(S.eq_contrast, "1.03"); strcpy(S.eq_brightness, "0.005"); strcpy(S.eq_saturation, "1.06");
    strcpy(S.x265_params, "aq-mode=3:psy-rd=2.0:deblock=-2:-2");
    strcpy(S.audio_bitrate, "192k"); strcpy(S.movflags, "+faststart");
    strcpy(S.hwaccel, "none"); strcpy(S.encoder, "auto");
    DEF = S;
}

static void reset_to_factory(void) { S = DEF; }

static void save_preset_file(const char *name) {
    if (!name || !*name || strcmp(name, "factory") == 0) return;
    char file[PATH_MAX]; snprintf(file, sizeof(file), "%s/%s.preset", GPTPRO_PRESET_DIR, name);
    FILE *fp = fopen(file, "w"); if (!fp) return;
    fprintf(fp, "codec=\"%s\"\ncrf=\"%s\"\npreset=\"%s\"\nfps=\"%s\"\nscale_factor=\"%s\"\n", S.codec, S.crf, S.preset, S.fps, S.scale_factor);
    fprintf(fp, "scaler=\"%s\"\nai_backend=\"%s\"\nai_model=\"%s\"\nai_model_type=\"%s\"\ndnn_backend=\"%s\"\n", S.scaler, S.ai_backend, S.ai_model, S.ai_model_type, S.dnn_backend);
    
    fprintf(fp, "denoiser=\"%s\"\ndenoise_strength=\"%s\"\n", S.denoiser, S.denoise_strength);
    fprintf(fp, "deblock_mode=\"%s\"\ndeblock_thresh=\"%s\"\ndering_active=\"%d\"\ndering_strength=\"%s\"\n", S.deblock_mode, S.deblock_thresh, S.dering_active, S.dering_strength);
    
    fprintf(fp, "sharpen_method=\"%s\"\nsharpen_strength=\"%s\"\n", S.sharpen_method, S.sharpen_strength);
    fprintf(fp, "usm_radius=\"%s\"\nusm_amount=\"%s\"\nusm_threshold=\"%s\"\n", S.usm_radius, S.usm_amount, S.usm_threshold);

    fprintf(fp, "deband_method=\"%s\"\ndeband_strength=\"%s\"\n", S.deband_method, S.deband_strength);

    fprintf(fp, "grain_strength=\"%s\"\n", S.grain_strength);
    
    // Second set
    fprintf(fp, "denoiser_2=\"%s\"\ndenoise_strength_2=\"%s\"\n", S.denoiser_2, S.denoise_strength_2);
    fprintf(fp, "deblock_mode_2=\"%s\"\ndeblock_thresh_2=\"%s\"\ndering_active_2=\"%d\"\ndering_strength_2=\"%s\"\n", S.deblock_mode_2, S.deblock_thresh_2, S.dering_active_2, S.dering_strength_2);
    
    fprintf(fp, "sharpen_method_2=\"%s\"\nsharpen_strength_2=\"%s\"\n", S.sharpen_method_2, S.sharpen_strength_2);
    fprintf(fp, "usm_radius_2=\"%s\"\nusm_amount_2=\"%s\"\nusm_threshold_2=\"%s\"\n", S.usm_radius_2, S.usm_amount_2, S.usm_threshold_2);

    fprintf(fp, "deband_method_2=\"%s\"\ndeband_strength_2=\"%s\"\n", S.deband_method_2, S.deband_strength_2);

    fprintf(fp, "grain_strength_2=\"%s\"\n", S.grain_strength_2);
    fprintf(fp, "use_denoise_2=\"%d\"\nuse_deblock_2=\"%d\"\nuse_dering_2=\"%d\"\n", S.use_denoise_2, S.use_deblock_2, S.use_dering_2);
    fprintf(fp, "use_sharpen_2=\"%d\"\nuse_deband_2=\"%d\"\nuse_grain_2=\"%d\"\n", S.use_sharpen_2, S.use_deband_2, S.use_grain_2);
    fprintf(fp, "mi_mode=\"%s\"\neq_contrast=\"%s\"\neq_brightness=\"%s\"\neq_saturation=\"%s\"\n", S.mi_mode, S.eq_contrast, S.eq_brightness, S.eq_saturation);
    fprintf(fp, "x265_params=\"%s\"\n", S.x265_params);
    fprintf(fp, "outdir=\"%s\"\naudio_bitrate=\"%s\"\nmovflags=\"%s\"\nthreads=\"%s\"\n", S.outdir, S.audio_bitrate, S.movflags, S.threads);
    fprintf(fp, "use10=\"%d\"\nhwaccel=\"%s\"\nencoder=\"%s\"\n", S.use10, S.hwaccel, S.encoder);
    
    fprintf(fp, "no_deblock=\"%d\"\nno_denoise=\"%d\"\nno_decimate=\"%d\"\nno_interpolate=\"%d\"\n", S.no_deblock, S.no_denoise, S.no_decimate, S.no_interpolate);
    fprintf(fp, "no_sharpen=\"%d\"\nno_deband=\"%d\"\nno_eq=\"%d\"\nno_grain=\"%d\"\n", S.no_sharpen, S.no_deband, S.no_eq, S.no_grain);
    
    fclose(fp);
    printf("Saved preset: %s\n", name);
}

static void load_preset_file(const char *name, bool quiet) {
    if (strcmp(name, "factory") == 0) { S = DEF; return; }
    char file[PATH_MAX]; snprintf(file, sizeof(file), "%s/%s.preset", GPTPRO_PRESET_DIR, name);
    FILE *fp = fopen(file, "r"); if (!fp) { if(!quiet) printf("Preset not found: %s\n", name); return; }
    char line[2048], key[64], val[1024];
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%63[^=]=\"%1023[^\"]\"", key, val) == 2) {
            if (!strcmp(key, "codec")) strcpy(S.codec, val); else if (!strcmp(key, "crf")) strcpy(S.crf, val);
            else if (!strcmp(key, "preset")) strcpy(S.preset, val); else if (!strcmp(key, "fps")) strcpy(S.fps, val);
            else if (!strcmp(key, "scale_factor")) strcpy(S.scale_factor, val); else if (!strcmp(key, "scaler")) strcpy(S.scaler, val);
            else if (!strcmp(key, "ai_backend")) strcpy(S.ai_backend, val); else if (!strcmp(key, "ai_model")) strcpy(S.ai_model, val);
            else if (!strcmp(key, "ai_model_type")) strcpy(S.ai_model_type, val); else if (!strcmp(key, "dnn_backend")) strcpy(S.dnn_backend, val);
            else if (!strcmp(key, "denoiser")) strcpy(S.denoiser, val); else if (!strcmp(key, "denoise_strength")) strcpy(S.denoise_strength, val);
            else if (!strcmp(key, "deblock_mode")) strcpy(S.deblock_mode, val); else if (!strcmp(key, "deblock_thresh")) strcpy(S.deblock_thresh, val);
            else if (!strcmp(key, "dering_active")) S.dering_active = atoi(val); else if (!strcmp(key, "dering_strength")) strcpy(S.dering_strength, val);
            else if (!strcmp(key, "sharpen_method")) strcpy(S.sharpen_method, val); else if (!strcmp(key, "sharpen_strength")) strcpy(S.sharpen_strength, val);
            else if (!strcmp(key, "usm_radius")) strcpy(S.usm_radius, val); else if (!strcmp(key, "usm_amount")) strcpy(S.usm_amount, val); else if (!strcmp(key, "usm_threshold")) strcpy(S.usm_threshold, val);
            else if (!strcmp(key, "deband_method")) strcpy(S.deband_method, val); else if (!strcmp(key, "deband_strength")) strcpy(S.deband_strength, val);
            else if (!strcmp(key, "grain_strength")) strcpy(S.grain_strength, val);
            else if (!strcmp(key, "denoiser_2")) strcpy(S.denoiser_2, val); else if (!strcmp(key, "denoise_strength_2")) strcpy(S.denoise_strength_2, val);
            else if (!strcmp(key, "deblock_mode_2")) strcpy(S.deblock_mode_2, val); else if (!strcmp(key, "deblock_thresh_2")) strcpy(S.deblock_thresh_2, val);
            else if (!strcmp(key, "dering_active_2")) S.dering_active_2 = atoi(val); else if (!strcmp(key, "dering_strength_2")) strcpy(S.dering_strength_2, val);
            else if (!strcmp(key, "sharpen_method_2")) strcpy(S.sharpen_method_2, val); else if (!strcmp(key, "sharpen_strength_2")) strcpy(S.sharpen_strength_2, val);
            else if (!strcmp(key, "usm_radius_2")) strcpy(S.usm_radius_2, val); else if (!strcmp(key, "usm_amount_2")) strcpy(S.usm_amount_2, val); else if (!strcmp(key, "usm_threshold_2")) strcpy(S.usm_threshold_2, val);
            else if (!strcmp(key, "deband_method_2")) strcpy(S.deband_method_2, val); else if (!strcmp(key, "deband_strength_2")) strcpy(S.deband_strength_2, val);
            else if (!strcmp(key, "grain_strength_2")) strcpy(S.grain_strength_2, val);
            else if (!strcmp(key, "use_denoise_2")) S.use_denoise_2 = atoi(val);
            else if (!strcmp(key, "use_deblock_2")) S.use_deblock_2 = atoi(val);
            else if (!strcmp(key, "use_dering_2")) S.use_dering_2 = atoi(val);
            else if (!strcmp(key, "use_sharpen_2")) S.use_sharpen_2 = atoi(val);
            else if (!strcmp(key, "use_deband_2")) S.use_deband_2 = atoi(val);
            else if (!strcmp(key, "use_grain_2")) S.use_grain_2 = atoi(val);
            else if (!strcmp(key, "mi_mode")) strcpy(S.mi_mode, val);
            else if (!strcmp(key, "eq_contrast")) strcpy(S.eq_contrast, val); else if (!strcmp(key, "eq_brightness")) strcpy(S.eq_brightness, val); else if (!strcmp(key, "eq_saturation")) strcpy(S.eq_saturation, val);
            else if (!strcmp(key, "x265_params")) strcpy(S.x265_params, val);
            else if (!strcmp(key, "outdir")) strcpy(S.outdir, val); else if (!strcmp(key, "audio_bitrate")) strcpy(S.audio_bitrate, val);
            else if (!strcmp(key, "movflags")) strcpy(S.movflags, val); else if (!strcmp(key, "threads")) strcpy(S.threads, val); else if (!strcmp(key, "hwaccel")) strcpy(S.hwaccel, val);
            else if (!strcmp(key, "encoder")) strcpy(S.encoder, val); else if (!strcmp(key, "use10")) S.use10 = atoi(val);
            else if (!strcmp(key, "no_deblock")) S.no_deblock = atoi(val); else if (!strcmp(key, "no_denoise")) S.no_denoise = atoi(val);
            else if (!strcmp(key, "no_decimate")) S.no_decimate = atoi(val); else if (!strcmp(key, "no_interpolate")) S.no_interpolate = atoi(val);
            else if (!strcmp(key, "no_sharpen")) S.no_sharpen = atoi(val); else if (!strcmp(key, "no_deband")) S.no_deband = atoi(val);
            else if (!strcmp(key, "no_eq")) S.no_eq = atoi(val); else if (!strcmp(key, "no_grain")) S.no_grain = atoi(val);
        }
    } fclose(fp);
}

static void ensure_conf_dirs(void) {
    mkdir_p(GPTPRO_PRESET_DIR);
    char def[PATH_MAX]; snprintf(def, sizeof(def), "%s/default.preset", GPTPRO_PRESET_DIR);
    struct stat st;
    if (stat(def, &st) != 0) { S = DEF; save_preset_file("default"); }
    if (stat(GPTPRO_ACTIVE_FILE, &st) != 0) {
        FILE *fp = fopen(GPTPRO_ACTIVE_FILE, "w"); if(fp) { fprintf(fp, "default\n"); fclose(fp); }
    }
}

static void active_preset_name(char *out, size_t outsz) {
    FILE *fp = fopen(GPTPRO_ACTIVE_FILE, "r");
    if (!fp || !fgets(out, outsz, fp)) strncpy(out, "default", outsz);
    else { size_t n = strlen(out); while(n > 0 && isspace(out[n-1])) out[--n] = 0; }
    if (fp) fclose(fp);
}

static void set_active_preset(const char *name) {
    FILE *fp = fopen(GPTPRO_ACTIVE_FILE, "w"); if (fp) { fprintf(fp, "%s\n", name); fclose(fp); }
}

static void list_presets(char ***names, int *count) {
    *names=NULL; *count=0; int cap=8;
    char **arr = malloc(sizeof(char*)*cap);
    arr[*count] = strdup("factory"); (*count)++;
    DIR *d = opendir(GPTPRO_PRESET_DIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0]=='.') continue;
            size_t L = strlen(e->d_name);
            if (L>7 && !strcmp(e->d_name+L-7,".preset")) {
                char base[PATH_MAX]; snprintf(base, sizeof(base), "%s", e->d_name);
                base[L-7]='\0';
                if (strcmp(base,"factory")) {
                    if (*count==cap) { cap*=2; arr=realloc(arr,sizeof(char*)*cap); }
                    arr[*count]=strdup(base); (*count)++;
                }
            }
        } closedir(d);
    } *names=arr;
}
