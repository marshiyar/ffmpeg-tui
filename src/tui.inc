/* ---------- TUI System ---------- */
typedef struct { struct termios orig; int fd; bool ok; } TermCtx;

static TermCtx term_enter_raw(int fd) {
    TermCtx t = { .fd = fd, .ok=false };
    if (!isatty(fd)) return t;
    if (tcgetattr(fd, &t.orig)==-1) return t;
    struct termios raw = t.orig;
    raw.c_lflag &= ~(ICANON|ECHO); raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSAFLUSH, &raw)==-1) return t;
    t.ok=true; return t;
}
static void term_leave_raw(TermCtx *t) { if (t->ok) tcsetattr(t->fd, TCSAFLUSH, &t->orig); }

static void play_ui_sound(const char *sound_name) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "afplay /System/Library/Sounds/%s.aiff > /dev/null 2>&1 &", sound_name);
    system(cmd);
}

static int utf8_codepoint_len(unsigned char c) {
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static void menu_format_cell(const char *src, int cellw, char *dst, size_t dstsz) {
    size_t j = 0;
    int visual = 0;
    for (size_t i = 0; src[i] && j + 1 < dstsz && visual < cellw; ) {
        if (src[i] == '\x1b' && src[i+1] == '[') {
            if (j + 2 >= dstsz) break;
            dst[j++] = src[i++];
            dst[j++] = src[i++];
            while (src[i] && j + 1 < dstsz) {
                dst[j++] = src[i];
                if ((src[i] >= '@' && src[i] <= '~')) { i++; break; }
                i++;
            }
            continue;
        }
        int cp = utf8_codepoint_len((unsigned char)src[i]);
        if (j + (size_t)cp >= dstsz) break;
        for (int k = 0; k < cp && src[i]; k++) dst[j++] = src[i++];
        visual++;
    }
    while (visual < cellw && j + 1 < dstsz) { dst[j++] = ' '; visual++; }
    if (j + 4 < dstsz) { dst[j++] = '\x1b'; dst[j++] = '['; dst[j++] = '0'; dst[j++] = 'm'; }
    dst[j] = '\0';
}

static int ar_menu_choose(const char *prompt, const char **items, int n, int start_index) {
    int rfd = open("/dev/tty", O_RDONLY);
    int wfd = STDERR_FILENO;
    if (rfd < 0 || !isatty(wfd)) {
        dprintf(wfd, "%s\n", prompt);
        for(int i=0; i<n; i++) dprintf(wfd, "%d. %s\n", i+1, items[i]);
        return 0;
    }
    TermCtx ctx = term_enter_raw(rfd);
    int selected = (start_index >= 0 && start_index < n) ? start_index : 0;
    int width = 64; bool done = false, cancelled = false; char k[8];
    dprintf(wfd, "\033[?25l");

    while (!done) {
        dprintf(wfd, "\033[2J\033[H");
        dprintf(wfd, " ┌"); for (int i=0; i<width-2; i++) dprintf(wfd,"─"); dprintf(wfd, "┐\n");
        char prompt_cell[1024];
        menu_format_cell(prompt, width-4, prompt_cell, sizeof(prompt_cell));
        dprintf(wfd, " │ " C_BOLD "%s" C_RESET " │\n", prompt_cell);
        dprintf(wfd, " ├"); for (int i=0; i<width-2; i++) dprintf(wfd,"─"); dprintf(wfd, "┤\n");

        for (int i = 0; i < n; i++) {
            char key[4];
            char item_cell[1024];
            if (i < 9) snprintf(key, 4, "%d", i+1); else if (i == 9) strcpy(key, "0"); else snprintf(key, 4, "%c", 'A' + (i-10));
            menu_format_cell(items[i], width-8, item_cell, sizeof(item_cell));
            if (i == selected) dprintf(wfd, " │ " C_CYAN "> %s. %s" C_RESET " │\n", key, item_cell);
            else dprintf(wfd, " │   %s. %s │\n", key, item_cell);
        }
        dprintf(wfd, " └"); for (int i=0; i<width-2; i++) dprintf(wfd,"─"); dprintf(wfd, "┘\n");

        memset(k, 0, sizeof(k));
        if (read(rfd, k, 1) <= 0) break;
        if (k[0] == 0x1b) {
            if (read(rfd, k+1, 1) > 0 && k[1] == '[') {
                read(rfd, k+2, 1);
                if (k[2] == 'A') { selected = (selected - 1 + n) % n; play_ui_sound("Tink"); }
                if (k[2] == 'B') { selected = (selected + 1) % n; play_ui_sound("Tink"); }
            } else { cancelled = true; break; }
        } else if (k[0] == '\n' || k[0] == '\r') { play_ui_sound("Hero"); done = true; }
        else {
            int idx = -1;
            if (k[0] >= '1' && k[0] <= '9') idx = k[0] - '1'; else if (k[0] == '0') idx = 9;
            else if (k[0] >= 'a' && k[0] <= 'z') idx = k[0] - 'a' + 10; else if (k[0] >= 'A' && k[0] <= 'Z') idx = k[0] - 'A' + 10;
            if (idx >= 0 && idx < n) { selected = idx; play_ui_sound("Hero"); done = true; }
        }
    }
    dprintf(wfd, "\033[?25h"); term_leave_raw(&ctx); close(rfd);
    return cancelled ? -1 : selected;
}

static void prompt_edit(const char *name, char *buf, size_t sz) {
    fprintf(stderr, "Enter value for %s [current: %s]: ", name, buf);
    char line[1024];
    if (fgets(line, sizeof(line), stdin)) {
        size_t n = strlen(line); while(n > 0 && isspace(line[n-1])) line[--n] = 0;
        if (n > 0) snprintf(buf, sz, "%s", line);
    }
}

static void cycle_string(char *current, const char **options, int count) {
    int idx = 0;
    for (int i = 0; i < count; i++) {
        if (!strcmp(current, options[i])) { idx = i; break; }
    }
    strcpy(current, options[(idx + 1) % count]);
}

static void submenu_edit_group(const char *title, const char **keys, char **vals, size_t *sizes, int n) {
    int cursor = 0;
    for (;;) {
        char **items = malloc((n+1)*sizeof(char*));
        for(int i=0; i<n; i++) { items[i] = malloc(256); snprintf(items[i], 256, "%s = '%s'", keys[i], vals[i]); }
        items[n] = strdup("← Back");
        int sel = ar_menu_choose(title, (const char**)items, n+1, cursor);
        for(int i=0; i<=n; i++) free(items[i]); free(items);
        if (sel < 0 || sel == n) break;
        cursor = sel; prompt_edit(keys[sel], vals[sel], sizes[sel]);
    }
}

static void submenu_toggle_group(const char *title, const char **keys, char **vals, int n) {
    int cursor = 0;
    for (;;) {
        char **items = malloc((n+1)*sizeof(char*));
        for(int i=0; i<n; i++) {
            items[i] = malloc(256); bool on = !strcmp(vals[i], "1");
            snprintf(items[i], 256, "%s %s", on ? C_GREEN "[ON] " C_RESET : "[OFF]", keys[i]);
        }
        items[n] = strdup("← Back");
        int sel = ar_menu_choose(title, (const char**)items, n+1, cursor);
        for(int i=0; i<=n; i++) free(items[i]); free(items);
        if (sel < 0 || sel == n) break;
        cursor = sel; if (!strcmp(vals[sel], "1")) strcpy(vals[sel], "0"); else strcpy(vals[sel], "1");
    }
}

static void settings_main_menu(void) {
    ensure_conf_dirs(); char current[128]; active_preset_name(current, sizeof(current));
    load_preset_file(current, true); int cursor = 0;
    for(;;) {
        const char *opts[] = { "Codec & Rate", "Frame / Scale", "Filters (Denoise/Deblock)", "Color / EQ", "Toggles", "Hardware", "I/O", "Load Preset", "Save Preset", "Reset Factory", "Exit & Save" };
        char head[256]; snprintf(head, sizeof(head), "Settings — Active: %s", current);
        int sel = ar_menu_choose(head, opts, ARR_LEN(opts), cursor);
        if (sel < 0) return; cursor = sel;
        
        if(sel==0){
            int sub_c = 0;
            for(;;) {
                char *items[5];
                items[0] = malloc(256); snprintf(items[0],256,"Codec: " C_CYAN "%s" C_RESET, S.codec);
                items[1] = malloc(256); snprintf(items[1],256,"CRF: %s", S.crf);
                items[2] = malloc(256); snprintf(items[2],256,"Preset: " C_CYAN "%s" C_RESET, S.preset);
                items[3] = malloc(256); snprintf(items[3],256,"x265 Params: %s", S.x265_params);
                items[4] = strdup("← Back");
                int sidx = ar_menu_choose("Codec & Rate", (const char**)items, 5, sub_c);
                for(int i=0;i<5;i++) free(items[i]);
                if(sidx<0 || sidx==4) break; sub_c=sidx;
                if(sidx==0) { const char *opt[]={"h264","hevc"}; cycle_string(S.codec, opt, 2); }
                else if(sidx==1) prompt_edit("CRF (0-51)", S.crf, sizeof(S.crf));
                else if(sidx==2) { const char *opt[]={"veryfast","faster","medium","slow","slower","veryslow"}; cycle_string(S.preset, opt, 6); }
                else if(sidx==3) prompt_edit("x265 Params", S.x265_params, sizeof(S.x265_params));
            }
        }
        else if(sel==1){
            int sub_c = 0;
            for(;;) {
                char *items[5];
                items[0] = malloc(256); snprintf(items[0],256,"FPS: %s (source=Lock)", S.fps);
                items[1] = malloc(256); snprintf(items[1],256,"Scale Factor: %s", S.scale_factor);
                items[2] = malloc(256); snprintf(items[2],256,"Scaler: " C_CYAN "%s" C_RESET, S.scaler);
                items[3] = malloc(256); snprintf(items[3],256,"Interpolation: " C_CYAN "%s" C_RESET, S.mi_mode);
                items[4] = strdup("← Back");
                int sidx = ar_menu_choose("Frame & Scale", (const char**)items, 5, sub_c);
                for(int i=0;i<5;i++) free(items[i]);
                if(sidx<0 || sidx==4) break; sub_c=sidx;
                if(sidx==0) prompt_edit("FPS (1-240 or 'source')", S.fps, sizeof(S.fps));
                else if(sidx==1) prompt_edit("Scale Factor (0.1-10)", S.scale_factor, sizeof(S.scale_factor));
                else if(sidx==2) { const char *opt[]={"lanczos","zscale","hw"}; cycle_string(S.scaler, opt, 3); }
                else if(sidx==3) { const char *opt[]={"mci","blend"}; cycle_string(S.mi_mode, opt, 2); }
            }
        }
        else if(sel==2){
            int sub_c = 0;
            for(;;) {
                char *items[40]; int k=0;
                // First set
                items[k++] = malloc(256); snprintf(items[k-1],256,"Denoiser: " C_CYAN "%s" C_RESET, S.denoiser);
                items[k++] = malloc(256); snprintf(items[k-1],256,"Denoise Strength: %s", S.denoise_strength);
                items[k++] = malloc(256); snprintf(items[k-1],256,"Deblock Mode: " C_CYAN "%s" C_RESET, S.deblock_mode);
                items[k++] = malloc(256); snprintf(items[k-1],256,"Dering Active: " C_CYAN "%s" C_RESET, S.dering_active ? "YES" : "NO");
                items[k++] = malloc(256); snprintf(items[k-1],256,"Dering Strength: %s", S.dering_strength);
                items[k++] = malloc(256); snprintf(items[k-1],256,"Sharpen Method: " C_CYAN "%s" C_RESET, S.sharpen_method);
                
                if(!strcmp(S.sharpen_method, "unsharp")) {
                    items[k++] = malloc(256); snprintf(items[k-1],256,"  Radius: %s", S.usm_radius);
                    items[k++] = malloc(256); snprintf(items[k-1],256,"  Amount: %s", S.usm_amount);
                    items[k++] = malloc(256); snprintf(items[k-1],256,"  Threshold: %s", S.usm_threshold);
                } else {
                    items[k++] = malloc(256); snprintf(items[k-1],256,"  CAS Strength: %s", S.sharpen_strength);
                }

                items[k++] = malloc(256); snprintf(items[k-1],256,"Deband Method: " C_CYAN "%s" C_RESET, S.deband_method);
                items[k++] = malloc(256); snprintf(items[k-1],256,"  Strength: %s", S.deband_strength);
                items[k++] = malloc(256); snprintf(items[k-1],256,"Grain Strength: %s", S.grain_strength);
                
                // Save count of first set
                int first_set_count = k;
                
                // Second set toggles and settings (using _2 fields)
                items[k++] = malloc(256); snprintf(items[k-1],256,"[%s] Use Denoiser (2)", S.use_denoise_2 ? "ON" : "OFF");
                items[k++] = malloc(256); snprintf(items[k-1],256,"Denoiser (2): " C_CYAN "%s" C_RESET, S.denoiser_2);
                items[k++] = malloc(256); snprintf(items[k-1],256,"Denoise Strength (2): %s", S.denoise_strength_2);
                items[k++] = malloc(256); snprintf(items[k-1],256,"[%s] Use Deblock (2)", S.use_deblock_2 ? "ON" : "OFF");
                items[k++] = malloc(256); snprintf(items[k-1],256,"Deblock Mode (2): " C_CYAN "%s" C_RESET, S.deblock_mode_2);
                items[k++] = malloc(256); snprintf(items[k-1],256,"[%s] Use Dering (2)", S.use_dering_2 ? "ON" : "OFF");
                items[k++] = malloc(256); snprintf(items[k-1],256,"Dering Active (2): " C_CYAN "%s" C_RESET, S.dering_active_2 ? "YES" : "NO");
                items[k++] = malloc(256); snprintf(items[k-1],256,"Dering Strength (2): %s", S.dering_strength_2);
                items[k++] = malloc(256); snprintf(items[k-1],256,"[%s] Use Sharpen (2)", S.use_sharpen_2 ? "ON" : "OFF");
                items[k++] = malloc(256); snprintf(items[k-1],256,"Sharpen Method (2): " C_CYAN "%s" C_RESET, S.sharpen_method_2);
                
                if(!strcmp(S.sharpen_method_2, "unsharp")) {
                    items[k++] = malloc(256); snprintf(items[k-1],256,"  Radius (2): %s", S.usm_radius_2);
                    items[k++] = malloc(256); snprintf(items[k-1],256,"  Amount (2): %s", S.usm_amount_2);
                    items[k++] = malloc(256); snprintf(items[k-1],256,"  Threshold (2): %s", S.usm_threshold_2);
                } else {
                    items[k++] = malloc(256); snprintf(items[k-1],256,"  CAS Strength (2): %s", S.sharpen_strength_2);
                }

                items[k++] = malloc(256); snprintf(items[k-1],256,"[%s] Use Deband (2)", S.use_deband_2 ? "ON" : "OFF");
                items[k++] = malloc(256); snprintf(items[k-1],256,"Deband Method (2): " C_CYAN "%s" C_RESET, S.deband_method_2);
                items[k++] = malloc(256); snprintf(items[k-1],256,"  Strength (2): %s", S.deband_strength_2);
                items[k++] = malloc(256); snprintf(items[k-1],256,"[%s] Use Grain (2)", S.use_grain_2 ? "ON" : "OFF");
                items[k++] = malloc(256); snprintf(items[k-1],256,"Grain Strength (2): %s", S.grain_strength_2);
                
                items[k++] = strdup("← Back");
                
                int sidx = ar_menu_choose("Filters (Adv)", (const char**)items, k, sub_c);
                for(int i=0;i<k;i++) free(items[i]);
                if(sidx<0 || sidx==k-1) break; sub_c=sidx;
                
                // Map selection to actual field
                int actual_idx = sidx;
                bool is_second_set = (sidx >= first_set_count && sidx < k-1);
                if(is_second_set) {
                    actual_idx = sidx - first_set_count;
                }
                
                // First set handling
                if(!is_second_set) {
                    if(actual_idx==0) { const char *opt[]={"bm3d","nlmeans","hqdn3d","atadenoise"}; cycle_string(S.denoiser, opt, 4); }
                    else if(actual_idx==1) prompt_edit("Denoise Strength (0-20 or 'auto')", S.denoise_strength, sizeof(S.denoise_strength));
                    else if(actual_idx==2) { const char *opt[]={"weak","strong"}; cycle_string(S.deblock_mode, opt, 2); }
                    else if(actual_idx==3) S.dering_active = !S.dering_active;
                    else if(actual_idx==4) prompt_edit("Dering Strength (0-10)", S.dering_strength, sizeof(S.dering_strength));
                    else if(actual_idx==5) { const char *opt[]={"cas","unsharp"}; cycle_string(S.sharpen_method, opt, 2); }
                    else {
                        const char *sharpen_method = S.sharpen_method;
                        bool unsharp = !strcmp(sharpen_method, "unsharp");
                        int offset = 6;
                        if(unsharp) {
                            if(actual_idx==offset) prompt_edit("USM Radius (3-23)", S.usm_radius, 16);
                            else if(actual_idx==offset+1) prompt_edit("USM Amount (-2.0-5.0)", S.usm_amount, 16);
                            else if(actual_idx==offset+2) prompt_edit("USM Threshold (0-255)", S.usm_threshold, 16);
                            offset += 3;
                        } else {
                            if(actual_idx==offset) prompt_edit("CAS Strength (0.0-1.0)", S.sharpen_strength, 32);
                            offset += 1;
                        }
                        
                        if(actual_idx==offset) {
                            const char *opt[]={"deband","gradfun"};
                            cycle_string(S.deband_method, opt, 2);
                        }
                        else {
                            int doff = offset + 1;
                            if(actual_idx==doff) prompt_edit("Deband Strength (0.0-0.5)", S.deband_strength, 32);
                            doff+=1;
                            if(actual_idx==doff) prompt_edit("Grain Strength (0-100)", S.grain_strength, 16);
                        }
                    }
                }
                // Second set handling
                else {
                    // actual_idx 0: Use Denoiser (2) toggle
                    if(actual_idx==0) S.use_denoise_2 = !S.use_denoise_2;
                    // actual_idx 1: Denoiser (2) mode
                    else if(actual_idx==1) { const char *opt[]={"bm3d","nlmeans","hqdn3d","atadenoise"}; cycle_string(S.denoiser_2, opt, 4); }
                    // actual_idx 2: Denoise Strength (2)
                    else if(actual_idx==2) prompt_edit("Denoise Strength (2) (0-20 or 'auto')", S.denoise_strength_2, sizeof(S.denoise_strength_2));
                    // actual_idx 3: Use Deblock (2) toggle
                    else if(actual_idx==3) S.use_deblock_2 = !S.use_deblock_2;
                    // actual_idx 4: Deblock Mode (2)
                    else if(actual_idx==4) { const char *opt[]={"weak","strong"}; cycle_string(S.deblock_mode_2, opt, 2); }
                    // actual_idx 5: Use Dering (2) toggle
                    else if(actual_idx==5) S.use_dering_2 = !S.use_dering_2;
                    // actual_idx 6: Dering Active (2)
                    else if(actual_idx==6) S.dering_active_2 = !S.dering_active_2;
                    // actual_idx 7: Dering Strength (2)
                    else if(actual_idx==7) prompt_edit("Dering Strength (2) (0-10)", S.dering_strength_2, sizeof(S.dering_strength_2));
                    // actual_idx 8: Use Sharpen (2) toggle
                    else if(actual_idx==8) S.use_sharpen_2 = !S.use_sharpen_2;
                    // actual_idx 9: Sharpen Method (2)
                    else if(actual_idx==9) { const char *opt[]={"cas","unsharp"}; cycle_string(S.sharpen_method_2, opt, 2); }
                    // actual_idx 10+: Sharpen params (2)
                    else {
                        const char *sharpen_method = S.sharpen_method_2;
                        bool unsharp = !strcmp(sharpen_method, "unsharp");
                        int offset = 10;
                        if(unsharp) {
                            if(actual_idx==offset) prompt_edit("USM Radius (2) (3-23)", S.usm_radius_2, 16);
                            else if(actual_idx==offset+1) prompt_edit("USM Amount (2) (-2.0-5.0)", S.usm_amount_2, 16);
                            else if(actual_idx==offset+2) prompt_edit("USM Threshold (2) (0-255)", S.usm_threshold_2, 16);
                            offset += 3;
                        } else {
                            if(actual_idx==offset) prompt_edit("CAS Strength (2) (0.0-1.0)", S.sharpen_strength_2, 32);
                            offset += 1;
                        }
                        
                        // actual_idx offset: Use Deband (2) toggle
                        if(actual_idx==offset) S.use_deband_2 = !S.use_deband_2;
                        // actual_idx offset+1: Deband method (2)
                        else if(actual_idx==offset+1) {
                            const char *opt[]={"deband","gradfun"};
                            cycle_string(S.deband_method_2, opt, 2);
                        }
                        // actual_idx offset+2+: Deband params (2)
                        else {
                            int doff = offset + 2;
                            if(actual_idx==doff) prompt_edit("Deband Strength (2) (0.0-0.5)", S.deband_strength_2, 32);
                            doff+=1;
                            // actual_idx doff: Use Grain (2) toggle
                            if(actual_idx==doff) S.use_grain_2 = !S.use_grain_2;
                            // actual_idx doff+1: Grain Strength (2)
                            else if(actual_idx==doff+1) prompt_edit("Grain Strength (2) (0-100)", S.grain_strength_2, 16);
                        }
                    }
                }
            }
        }
        else if(sel==3){
            const char *k[]={"contrast (1.0=norm)","brightness","saturation (1.0=norm)"};
            char *v[]={S.eq_contrast,S.eq_brightness,S.eq_saturation};
            size_t s[]={16,16,16}; submenu_edit_group("Color", k, v, s, 3);
        }
        else if(sel==4){
            const char *k[]={"no_deblock","no_denoise","no_decimate","no_interpolate","no_sharpen","no_deband","no_eq","no_grain"};
            char b[8][16]; char *v[8]; for(int i=0;i<8;i++) v[i]=b[i];
            snprintf(b[0],16,"%d",S.no_deblock); snprintf(b[1],16,"%d",S.no_denoise); snprintf(b[2],16,"%d",S.no_decimate); snprintf(b[3],16,"%d",S.no_interpolate);
            snprintf(b[4],16,"%d",S.no_sharpen); snprintf(b[5],16,"%d",S.no_deband); snprintf(b[6],16,"%d",S.no_eq); snprintf(b[7],16,"%d",S.no_grain);
            submenu_toggle_group("Toggles", k, v, 8);
            S.no_deblock=atoi(b[0]); S.no_denoise=atoi(b[1]); S.no_decimate=atoi(b[2]); S.no_interpolate=atoi(b[3]);
            S.no_sharpen=atoi(b[4]); S.no_deband=atoi(b[5]); S.no_eq=atoi(b[6]); S.no_grain=atoi(b[7]);
        }
        else if(sel==5){
            int sub_c=0;
            for(;;) {
                char *items[5];
                items[0] = malloc(256); snprintf(items[0],256,"HW Accel: " C_CYAN "%s" C_RESET, S.hwaccel);
                items[1] = malloc(256); snprintf(items[1],256,"Encoder: " C_CYAN "%s" C_RESET, S.encoder);
                items[2] = malloc(256); snprintf(items[2],256,"10-Bit Output: " C_CYAN "%s" C_RESET, S.use10 ? "Yes" : "No");
                items[3] = malloc(256); snprintf(items[3],256,"Threads: %s", S.threads);
                items[4] = strdup("← Back");
                int sidx = ar_menu_choose("Hardware", (const char**)items, 5, sub_c);
                for(int i=0;i<5;i++) free(items[i]);
                if(sidx<0 || sidx==4) break; sub_c=sidx;
                if(sidx==0) { const char *opt[]={"none","cuda","qsv","vaapi"}; cycle_string(S.hwaccel, opt, 4); }
                else if(sidx==1) { const char *opt[]={"auto","cpu","nvenc","qsv","vaapi"}; cycle_string(S.encoder, opt, 5); }
                else if(sidx==2) S.use10 = !S.use10;
                else if(sidx==3) prompt_edit("Threads (0=Auto)", S.threads, sizeof(S.threads));
            }
        }
        else if(sel==6){
            int sub_c=0;
            for(;;) {
                char *items[4];
                items[0] = malloc(256); snprintf(items[0],256,"Output Dir: %s", S.outdir);
                items[1] = malloc(256); snprintf(items[1],256,"Audio Bitrate: %s", S.audio_bitrate);
                items[2] = malloc(256); snprintf(items[2],256,"Movflags: " C_CYAN "%s" C_RESET, S.movflags);
                items[3] = strdup("← Back");
                int sidx = ar_menu_choose("I/O", (const char**)items, 4, sub_c);
                for(int i=0;i<4;i++) free(items[i]);
                if(sidx<0 || sidx==3) break; sub_c=sidx;
                if(sidx==0) prompt_edit("Output Dir", S.outdir, sizeof(S.outdir));
                else if(sidx==1) prompt_edit("Audio Bitrate (e.g. 192k)", S.audio_bitrate, sizeof(S.audio_bitrate));
                else if(sidx==2) { if(!strcmp(S.movflags, "+faststart")) strcpy(S.movflags, ""); else strcpy(S.movflags, "+faststart"); }
            }
        }
        else if(sel==7){
            char **names; int count; list_presets(&names, &count);
            int pidx = ar_menu_choose("Load", (const char**)names, count, 0);
            if (pidx >= 0) { load_preset_file(names[pidx], false); snprintf(current, sizeof(current), "%s", names[pidx]); set_active_preset(current); }
            for(int i=0;i<count;i++) free(names[i]); free(names);
        } else if(sel==8){
            char name[256]=""; prompt_edit("name", name, sizeof(name));
            if (*name && strcmp(name,"factory")) { save_preset_file(name); snprintf(current, sizeof(current), "%s", name); set_active_preset(current); }
        } else if(sel==9) reset_to_factory();
        else if(sel==10) { save_preset_file(current); return; }
    }
}
