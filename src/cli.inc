/* ---------- CLI Parsing ---------- */
static int parse_command_line(char *command_line, char ***argv_out) {
    int argc = 0; int max_args = 64;
    char **argv = malloc(sizeof(char*) * max_args);
    char *p = command_line;
    bool in_q = false, in_dq = false;
    while (*p) {
        while (*p && isspace((unsigned char)*p) && !in_q && !in_dq) p++;
        if (!*p) break;
        char *start = p;
        while (*p) {
            if (*p == '\\' && p[1]) { memmove(p, p+1, strlen(p)); p++; }
            else if (*p == '\'') { in_q = !in_q; memmove(p, p+1, strlen(p)); }
            else if (*p == '"') { in_dq = !in_dq; memmove(p, p+1, strlen(p)); }
            else if (isspace((unsigned char)*p) && !in_q && !in_dq) break;
            else p++;
        }
        if (*p) *p++ = 0;
        argv[argc++] = strdup(start);
    }
    argv[argc] = NULL; *argv_out = argv; return argc;
}

static int process_cli_args(int argc, char **argv, const char *ffmpeg_path) {
    char input_path[PATH_MAX]="";
    int opt, long_idx;
    // FIX: Added missing CLI options to match HELP_TEXT
    struct option long_opts[] = {
        {"codec",1,0,1}, {"crf",1,0,'c'}, {"preset",1,0,'p'}, {"fps",1,0,'f'}, {"scale",1,0,'s'},
        {"scaler",1,0,2}, {"denoiser",1,0,3}, {"x265",1,0,5}, {"help",0,0,'h'},
        {"manual",0,0,'m'}, {"outdir",1,0,'o'}, {"no-deblock",0,0,10}, {"no-denoise",0,0,11}, {"dry-run",0,0,12},
        {"dering",0,0,13}, {"usm-radius",1,0,14}, {"usm-amount",1,0,15}, {"usm-threshold",1,0,16},
        /* Added flags */
        {"hevc",0,0,20}, {"10bit",0,0,21}, {"mi-mode",1,0,22}, {"ai-backend",1,0,23},
        {"ai-model",1,0,24}, {"dnn-backend",1,0,25}, {"denoise-strength",1,0,26},
        {"sharpen-method",1,0,27}, {"deband-method",1,0,28},
        {0,0,0,0}
    };
    optind = 1;
    while ((opt = getopt_long(argc, argv, "i:o:c:p:f:s:hm", long_opts, &long_idx)) != -1) {
        switch(opt) {
            case 'i': strncpy(input_path, optarg, PATH_MAX); break;
            case 'o': strncpy(S.outdir, optarg, PATH_MAX); break;
            case 'c': strncpy(S.crf, optarg, 16); break;
            case 'p': strncpy(S.preset, optarg, 32); break;
            case 'f': strncpy(S.fps, optarg, 16); break;
            case 's': strncpy(S.scale_factor, optarg, 16); break;
            case 1: strncpy(S.codec, optarg, 8); break;
            case 2: strncpy(S.scaler, optarg, 16); break;
            case 3: strncpy(S.denoiser, optarg, 16); break;
            case 5: strncpy(S.x265_params, optarg, 256); break;
            case 10: S.no_deblock = 1; break;
            case 11: S.no_denoise = 1; break;
            case 12: DRY_RUN = 1; break;
            case 13: S.dering_active = 1; break;
            case 14: strncpy(S.usm_radius, optarg, 16); strcpy(S.sharpen_method, "unsharp"); break;
            case 15: strncpy(S.usm_amount, optarg, 16); break;
            case 16: strncpy(S.usm_threshold, optarg, 16); break;
            /* Implementation of added flags */
            case 20: strcpy(S.codec, "hevc"); break;
            case 21: S.use10 = 1; break;
            case 22: strncpy(S.mi_mode, optarg, 16); break;
            case 23: strncpy(S.ai_backend, optarg, 16); break;
            case 24: strncpy(S.ai_model, optarg, PATH_MAX); break;
            case 25: strncpy(S.dnn_backend, optarg, 32); break;
            case 26: strncpy(S.denoise_strength, optarg, 16); break;
            case 27: strncpy(S.sharpen_method, optarg, 16); break;
            case 28: strncpy(S.deband_method, optarg, 16); break;
            
            case 'h': printf("%s", HELP_TEXT); return 0;
            case 'm': printf("%s", MANUAL_TEXT); return 0;
        }
    }
    if (optind < argc && !*input_path) strncpy(input_path, argv[optind], PATH_MAX);
    if (*input_path) process_file(input_path, ffmpeg_path, false);
    return 0;
}
