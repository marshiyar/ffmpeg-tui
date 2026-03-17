/* ---------- Processing Logic ---------- */
typedef struct { char *buf; size_t len, cap; } SB;
static void sb_append(SB *s, const char *str) {
    if (!s->buf) { s->cap=1024; s->buf=malloc(s->cap); s->len=0; }
    size_t l = strlen(str);
    if (s->len + l + 1 >= s->cap) { s->cap = (s->cap + l)*2; s->buf=realloc(s->buf, s->cap); }
    strcpy(s->buf + s->len, str); s->len += l;
}
static void sb_fmt(SB *s, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char tmp[2048]; vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap); sb_append(s, tmp);
}

static void process_file(const char *in, const char *ffmpeg, bool batch) {
    (void)batch; char outdir[PATH_MAX], base[PATH_MAX], out[PATH_MAX];
    {
        char t[PATH_MAX]; strcpy(t, in); char *b = basename(t); strcpy(base, b);
        char *dot = strrchr(base, '.'); if(dot) *dot=0;
        if (*S.outdir) strcpy(outdir, S.outdir); else { strcpy(t, in); strcpy(outdir, dirname(t)); }
    }
    mkdir_p(outdir); snprintf(out, sizeof(out), "%s/%s_[restored].mp4", outdir, base);

    SB vf = {0};
    
    /* 1. Input Format & Decimation */
    sb_append(&vf, "format=yuv444p16le,");
    
    if (!S.no_decimate) sb_append(&vf, "mpdecimate=hi=64*12,setpts=PTS,");

    /* 2. Pre-Processing (Deblock / Denoise) */
    if (!S.no_deblock) {
        if (*S.deblock_thresh) sb_fmt(&vf, "deblock=filter=%s:block=8:%s,", S.deblock_mode, S.deblock_thresh);
        else sb_fmt(&vf, "deblock=filter=%s:block=8,", S.deblock_mode);
    }
    
    if (S.dering_active) sb_append(&vf, "hqdn3d=4:3:6:4,");

    if (!S.no_denoise) {
        if (!strcmp(S.denoiser, "bm3d")) {
            if (!strcmp(S.denoise_strength, "auto")) sb_append(&vf, "bm3d=estim=final:planes=1,");
            else sb_fmt(&vf, "bm3d=sigma=%s:estim=basic:planes=1,", S.denoise_strength);
        }
        else if (!strcmp(S.denoiser, "hqdn3d")) sb_fmt(&vf, "hqdn3d=s=%s:p=%s:r=5,", S.denoise_strength, S.denoise_strength);
        else if (!strcmp(S.denoiser, "nlmeans")) sb_fmt(&vf, "nlmeans=s=%s:p=7:r=5,", S.denoise_strength);
        else sb_append(&vf, "atadenoise=s=9:0a=0.02:0b=0.04,");
    }

    /* 3. Interpolation (FPS Lock check) */
    if (!S.no_interpolate) {
        if (!strcmp(S.fps, "source") || !strcmp(S.fps, "lock")) {
            sb_fmt(&vf, "minterpolate=mi_mode=%s:mc_mode=aobmc:me_mode=bidir:vsbmc=1,", S.mi_mode);
        } else {
            sb_fmt(&vf, "minterpolate=fps=%s:mi_mode=%s:mc_mode=aobmc:me_mode=bidir:vsbmc=1,", S.fps, S.mi_mode);
        }
    }

    /* 4. Scaling */
    if (!strcmp(S.scaler, "zscale")) sb_fmt(&vf, "zscale=w=iw*%s:h=ih*%s:filter=lanczos:dither=error_diffusion,", S.scale_factor, S.scale_factor);
    else if (!strcmp(S.scaler, "ai")) {
        if (!strcmp(S.ai_backend, "sr")) { sb_fmt(&vf, "sr=dnn_backend=%s:model='%s'", S.dnn_backend, S.ai_model); if (!strcmp(S.ai_model_type, "srcnn")) sb_fmt(&vf, ":scale_factor=%s", S.scale_factor); sb_append(&vf, ","); }
        else sb_fmt(&vf, "dnn_processing=dnn_backend=%s:model='%s':input=x:output=y,", S.dnn_backend, S.ai_model);
    } else if (!strcmp(S.scaler, "hw")) {
        if (!strcmp(S.hwaccel,"cuda")) sb_fmt(&vf, "scale_npp=iw*%s:ih*%s,", S.scale_factor, S.scale_factor);
        else sb_fmt(&vf, "scale=iw*%s:ih*%s:flags=lanczos,", S.scale_factor, S.scale_factor);
    } else sb_fmt(&vf, "scale=iw*%s:ih*%s:flags=lanczos+accurate_rnd,", S.scale_factor, S.scale_factor);

    /* 5. Post-Processing (Sharpen/Deband/Color) */
    if (!S.no_sharpen) {
        if (!strcmp(S.sharpen_method, "unsharp")) {
            sb_fmt(&vf, "unsharp=%s:%s:%s,", S.usm_radius, S.usm_radius, S.usm_amount);
        }
        else sb_fmt(&vf, "cas=strength=%s,", S.sharpen_strength);
    }
    
    if (!S.no_deband) {
        if (!strcmp(S.deband_method, "gradfun")) sb_fmt(&vf, "gradfun=%s,", S.deband_strength);
        else sb_fmt(&vf, "deband=1thr=%s:b=1,", S.deband_strength);
    }

    if (!S.no_eq) {
        sb_fmt(&vf, "eq=contrast=%s:brightness=%s:saturation=%s,", S.eq_contrast, S.eq_brightness, S.eq_saturation);
    }
    
    /* Second Set of Filters (individual toggles) */
    if (S.use_deblock_2 && !S.no_deblock) {
        if (*S.deblock_thresh_2) sb_fmt(&vf, "deblock=filter=%s:block=8:%s,", S.deblock_mode_2, S.deblock_thresh_2);
        else sb_fmt(&vf, "deblock=filter=%s:block=8,", S.deblock_mode_2);
    }
    
    if (S.use_dering_2 && S.dering_active_2) sb_append(&vf, "hqdn3d=4:3:6:4,");

    if (S.use_denoise_2 && !S.no_denoise) {
        if (!strcmp(S.denoiser_2, "bm3d")) {
            if (!strcmp(S.denoise_strength_2, "auto")) sb_append(&vf, "bm3d=estim=final:planes=1,");
            else sb_fmt(&vf, "bm3d=sigma=%s:estim=basic:planes=1,", S.denoise_strength_2);
        }
        else if (!strcmp(S.denoiser_2, "hqdn3d")) sb_fmt(&vf, "hqdn3d=s=%s:p=%s:r=5,", S.denoise_strength_2, S.denoise_strength_2);
        else if (!strcmp(S.denoiser_2, "nlmeans")) sb_fmt(&vf, "nlmeans=s=%s:p=7:r=5,", S.denoise_strength_2);
        else sb_append(&vf, "atadenoise=s=9:0a=0.02:0b=0.04,");
    }

    if (S.use_sharpen_2 && !S.no_sharpen) {
        if (!strcmp(S.sharpen_method_2, "unsharp")) {
            sb_fmt(&vf, "unsharp=%s:%s:%s,", S.usm_radius_2, S.usm_radius_2, S.usm_amount_2);
        }
        else sb_fmt(&vf, "cas=strength=%s,", S.sharpen_strength_2);
    }
    
    if (S.use_deband_2 && !S.no_deband) {
        if (!strcmp(S.deband_method_2, "gradfun")) sb_fmt(&vf, "gradfun=%s,", S.deband_strength_2);
        else sb_fmt(&vf, "deband=1thr=%s:b=1,", S.deband_strength_2);
    }
    
    sb_append(&vf, "limiter=min=16:max=235:planes=0+1+2,setsar=1");
    if (!S.no_grain) {
        if (S.use_grain_2) sb_fmt(&vf, ",noise=alls=%s:allf=t", S.grain_strength_2);
        else sb_fmt(&vf, ",noise=alls=%s:allf=t", S.grain_strength);
    }

    /* 6. Final Format */
    const char *pix = S.use10 ? "yuv420p10le" : "yuv420p";
    if (S.use10 && (!strcmp(S.encoder,"nvenc") || !strcmp(S.encoder,"hevc_nvenc"))) pix="p010le";
    
    sb_fmt(&vf, ",format=%s", pix);

    char *args[128]; int a=0;
    args[a++] = (char*)ffmpeg; args[a++] = "-hide_banner"; args[a++] = "-loglevel"; args[a++] = "error"; args[a++] = "-stats"; args[a++] = "-y";
    if (strcmp(S.hwaccel,"none")) { args[a++] = "-hwaccel"; args[a++] = S.hwaccel; }
    args[a++] = "-i"; args[a++] = (char*)in;

    args[a++] = "-vf"; args[a++] = vf.buf;
    
    char *cod = "libx264";
    if (!strcmp(S.codec, "hevc")) {
        if (!strcmp(S.encoder, "nvenc")) cod = "hevc_nvenc"; else if (!strcmp(S.encoder, "qsv")) cod = "hevc_qsv"; else if (!strcmp(S.encoder, "vaapi")) cod = "hevc_vaapi"; else cod = "libx265";
    } else { if (!strcmp(S.encoder, "nvenc")) cod = "h264_nvenc"; else if (!strcmp(S.encoder, "qsv")) cod = "h264_qsv"; else if (!strcmp(S.encoder, "vaapi")) cod = "h264_vaapi"; }

    args[a++] = "-c:v"; args[a++] = cod;
    
    if (strstr(cod, "hevc") || strstr(cod, "265")) { args[a++] = "-tag:v"; args[a++] = "hvc1"; }
    args[a++] = "-pix_fmt"; args[a++] = (char*)pix;

    if (!strstr(cod, "vaapi")) { args[a++] = "-preset"; args[a++] = S.preset; args[a++] = "-crf"; args[a++] = S.crf; }
    if (!strcmp(cod, "libx265") && *S.x265_params) { args[a++] = "-x265-params"; args[a++] = S.x265_params; }

    args[a++] = "-c:a"; args[a++] = "aac"; args[a++] = "-b:a"; args[a++] = S.audio_bitrate;
    if (*S.movflags) { args[a++] = "-movflags"; args[a++] = S.movflags; }
    args[a++] = out;

    args[a] = NULL;

#ifdef UP60P_LIBRARY_MODE
    // In library mode, we can optionally use callbacks for logging
    // For now, we'll just use printf (output goes to console)
    log_message("Processing: %s\n", in);
    
    if (DRY_RUN) {
        if (global_log_cb) {
            char cmd_buf[8192];
            int pos = 0;
            pos += snprintf(cmd_buf + pos, sizeof(cmd_buf) - pos, "CMD: ");
            for(int i=0; args[i]; i++) {
                pos += snprintf(cmd_buf + pos, sizeof(cmd_buf) - pos, "%s ", args[i]);
            }
            pos += snprintf(cmd_buf + pos, sizeof(cmd_buf) - pos, "\n");
            global_log_cb(cmd_buf);
        }
    } else {
        // Create pipes for stdout and stderr
        int stdout_pipe[2], stderr_pipe[2];
        if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
            if (global_log_cb) global_log_cb("Error: Failed to create pipes\n");
            free(vf.buf);
            return;
        }
        
        pid_t pid = fork();
        if (pid == 0) {
            // Child process: redirect stdout/stderr to pipes
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stdout_pipe[1]);
            close(stderr_pipe[1]);
            execvp(args[0], args);
            _exit(1);
        } else {
            // Parent process: read from pipes and send to callback
            close(stdout_pipe[1]);
            close(stderr_pipe[1]);
            
            char buffer[4096];
            fd_set readfds;
            int maxfd = (stdout_pipe[0] > stderr_pipe[0]) ? stdout_pipe[0] : stderr_pipe[0];
            ssize_t result;
            
            while (1) {
                FD_ZERO(&readfds);
                FD_SET(stdout_pipe[0], &readfds);
                FD_SET(stderr_pipe[0], &readfds);
                
                struct timeval timeout = {0, 100000}; // 100ms timeout
                int select_result = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
                
                if (select_result > 0) {
                    if (FD_ISSET(stdout_pipe[0], &readfds)) {
                        ssize_t n = read(stdout_pipe[0], buffer, sizeof(buffer) - 1);
                        if (n > 0) {
                            buffer[n] = 0;
                            if (global_log_cb) global_log_cb(buffer);
                        } else if (n == 0) {
                            close(stdout_pipe[0]);
                            FD_CLR(stdout_pipe[0], &readfds);
                        }
                    }
                    if (FD_ISSET(stderr_pipe[0], &readfds)) {
                        ssize_t n = read(stderr_pipe[0], buffer, sizeof(buffer) - 1);
                        if (n > 0) {
                            buffer[n] = 0;
                            if (global_log_cb) global_log_cb(buffer);
                        } else if (n == 0) {
                            close(stderr_pipe[0]);
                            FD_CLR(stderr_pipe[0], &readfds);
                        }
                    }
                } else if (select_result == 0) {
                    // Timeout - check if process is still running
                    int status;
                    if (waitpid(pid, &status, WNOHANG) != 0) {
                        // Process finished
                        break;
                    }
                }
            }
            
            // Read any remaining data
            while ((result = read(stdout_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[result] = 0;
                if (global_log_cb) global_log_cb(buffer);
            }
            while ((result = read(stderr_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[result] = 0;
                if (global_log_cb) global_log_cb(buffer);
            }
            
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            waitpid(pid, NULL, 0);
            
            if (global_log_cb) global_log_cb("Done.\n");
        }
    }
#else
    printf(C_BOLD "Processing: %s\n" C_RESET, in);
    if (DRY_RUN) { printf(C_YELLOW "CMD: "); for(int i=0; args[i]; i++) printf("%s ", args[i]); printf("\n" C_RESET); }
    else { pid_t pid = fork(); if (pid == 0) { execvp(args[0], args); _exit(1); } waitpid(pid, NULL, 0); printf(C_GREEN "Done.\n" C_RESET); }
#endif
    free(vf.buf);
}


static void process_directory(const char *dir, const char *ffmpeg) {
    DIR *d = opendir(dir); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[PATH_MAX]; snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) process_directory(path, ffmpeg);
            else if (strstr(path, ".mp4") || strstr(path, ".mkv") || strstr(path, ".mov")) process_file(path, ffmpeg, true);
        }
    } closedir(d);
}

static int interactive_mode(const char *self_path, const char *ffmpeg_path) {
    (void)self_path; char line[PATH_MAX];
    printf("\n" C_BOLD "up60p_restore_beast v4.9 COMPLETE" C_RESET "\n"); // FIX: Version match
    ensure_conf_dirs(); char ap[128]; active_preset_name(ap, sizeof(ap)); load_preset_file(ap, true);

    for (;;) {
        printf("\n────────────────────────────────────────────────────────────────\n");
        printf("Drag video/folder here, 'settings', or 'q':\n" C_CYAN "> " C_RESET);
        if (!fgets(line, sizeof(line), stdin)) break;
        sanitize_path(line);
        if (!strcmp(line, "q")) break;
        if (!strcmp(line, "settings")) { settings_main_menu(); continue; }
        
        char **p_argv; char *lc = strdup(line); int p_argc = parse_command_line(lc, &p_argv);
        if (p_argc > 1 || (p_argc == 1 && p_argv[0][0] == '-')) {
            char *t_argv[64]; t_argv[0] = (char*)SCRIPT_NAME;
            for(int i=0; i<p_argc; i++) t_argv[i+1] = p_argv[i];
            process_cli_args(p_argc+1, t_argv, ffmpeg_path);
        } else {
            struct stat st;
            if (stat(line, &st) == 0) {
                if (S_ISDIR(st.st_mode)) process_directory(line, ffmpeg_path);
                else process_file(line, ffmpeg_path, false);
            } else printf(C_RED "Invalid path or command.\n" C_RESET);
        }
        for(int i=0; i<p_argc; i++) free(p_argv[i]); free(p_argv); free(lc);
    }
    return 0;
}
