/* ---------- Public API Implementation ---------- */
#if HAVE_UP60P_HEADER
up60p_error up60p_init(const char *app_support_dir, up60p_log_callback log_cb) {
    (void)app_support_dir; // currently unused; we rely on HOME / XDG.
    
#ifdef UP60P_LIBRARY_MODE
    global_log_cb = log_cb; // Store the callback
#endif

    // Initialize paths safely
    init_paths();
    
    // Set defaults
    set_defaults();        // also sets DEF = S
    
    // Create config directories (may fail silently, that's OK)
    ensure_conf_dirs();    // creates ~/.config/gptPro, default preset, active_preset

    // Load active preset into S so "defaults" match the current preset
    // If this fails, we'll just use defaults
    char name[64];
    active_preset_name(name, sizeof(name));
    if (name[0] != '\0') {
        load_preset_file(name, true);  // quiet mode - don't print errors
    }

    return UP60P_OK;
}

void up60p_default_options(up60p_options *out_opts) {
    if (!out_opts) return;
    up60p_options_from_settings(out_opts, &S);
}

up60p_error up60p_process_path(const char *input_path,
                               const up60p_options *opts)
{
    if (!input_path || !opts) return UP60P_ERR_INVALID_OPTIONS;

    const char *ffmpeg = get_bundled_ffmpeg_path();
    if (!ffmpeg) return UP60P_ERR_FFMPEG_NOT_FOUND;

    struct stat st;
    if (stat(input_path, &st) != 0) return UP60P_ERR_IO;

    // Apply options into S, then reuse existing pipeline
    settings_from_up60p_options(&S, opts);

    if (S_ISDIR(st.st_mode)) {
        process_directory(input_path, ffmpeg);
    } else {
        process_file(input_path, ffmpeg, false);
    }

    return UP60P_OK;
}

void up60p_shutdown(void) {
    // Nothing to clean up yet – reserved for future resources.
}
#endif

#ifndef UP60P_LIBRARY_MODE
int main(int argc, char **argv) {
    set_defaults();
    init_paths();

    // LOAD DEFAULT PRESET BEFORE ANY PROCESSING:
    ensure_conf_dirs();
    char ap[128];
    active_preset_name(ap, sizeof(ap));
    load_preset_file(ap, true);

    const char *ffmpeg = get_bundled_ffmpeg_path();
    if (!ffmpeg) {
        printf("Error: FFmpeg not found.\n");
        return 1;
    }

    if (argc > 1 && argv[1][0] != '-') {
        struct stat st;
        if (stat(argv[1], &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                process_directory(argv[1], ffmpeg);
            } else {
                process_file(argv[1], ffmpeg, false);
            }
            return 0;
        }
    }

    return interactive_mode(argv[0], ffmpeg);
}
#endif
