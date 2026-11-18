/*
Copyright (c) 2016-2023 Raspberry Pi Ltd.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mount.h>

#include <libfdt.h>

#include "dtoverlay.h"
#include "utils.h"

#define CFG_DIR "/sys/kernel/config"
/* Where to mount configfs if it isn't at CFG_DIR (relative to WORK_DIR) */
#define CFG_MOUNT_SUBDIR "configfs"
#define DT_SUBDIR "/device-tree"
#define WORK_DIR "/run/dtoverlays"
#define OVERLAY_SRC_SUBDIR "overlays"
#define README_FILE "README"
#define DT_OVERLAYS_SUBDIR "overlays"
#define DTOVERLAY_PATH_MAX 128
#define DIR_MODE 0755
#define LOCK_FILE "lock"
#define LOCK_POLL_MS 50
#define LOCK_TIMEOUT_MS 30000
/* Set in the environment of the dtoverlay-pre/post hooks */
#define HOOK_ENV "DTOVERLAY_IN_HOOK"


enum {
    OPT_ADD,
    OPT_REMOVE,
    OPT_REMOVE_FROM,
    OPT_LIST,
    OPT_LIST_ALL,
    OPT_HELP
};

static const char *boot_dirs[] =
{
#ifdef FORCE_BOOT_DIR
    FORCE_BOOT_DIR,
#else
    "/boot/firmware",
    "/boot",
    "/flash",
#ifdef OTHER_BOOT_DIR
    OTHER_BOOT_DIR,
#endif
#endif
    NULL /* Terminator */
};

typedef struct state_struct
{
    int count;
    struct dirent **namelist;
} STATE_T;

static int dtoverlay_add(STATE_T *state, const char *overlay,
                         int argc, const char **argv);
static int dtoverlay_remove(STATE_T *state, const char *overlay, int and_later);
static int dtoverlay_list(STATE_T *state);
static int dtoverlay_list_all(STATE_T *state);
static void usage(void);
static void root_check(void);
static int check_work_dir(const char *dir, int create);
static void lock_state(int exclusive);
static void defer_signals(int defer);

static void overlay_help(const char *overlay, const char **params);

static int apply_overlay(const char *overlay_file, const char *overlay);
static int save_dtb_atomic(DTBLOB_T *dtb, const char *filename);
static void discard_overlay_file(const char *filename);
static int overlay_applied(const char *overlay_dir);

static STATE_T *read_state(const char *dir);
static void free_state(STATE_T *state);

const char *cmd_name;

const char *work_dir = WORK_DIR;
const char *overlay_src_dir;
const char *dt_overlays_dir;
const char *error_file = NULL;
const char *platform_string;
int platform_string_len;
int dry_run = 0;
static int lock_fd = -1;

char cell_source_loc[DTOVERLAY_MAX_PATH];
int cell_source_loc_len;

int main(int argc, const char **argv)
{
    int argn = 1;
    int opt = OPT_ADD;
    int is_dtparam;
    const char *overlay = NULL;
    const char **params = NULL;
    int ret = 0;
    STATE_T *state = NULL;
    const char *cfg_dir;
    int have_work_dir = 1;
    int modifying;
    int in_hook;

    cmd_name = argv[0];
    if (strrchr(cmd_name, '/'))
        cmd_name = strrchr(cmd_name, '/') + 1;
    is_dtparam = (strcmp(cmd_name, "dtparam") == 0);

    while ((argn < argc) && (argv[argn][0] == '-'))
    {
        const char *arg = argv[argn++];
        if (strcmp(arg, "-r") == 0)
        {
            if (opt != OPT_ADD)
                usage();
            opt = OPT_REMOVE;
        }
        else if (strcmp(arg, "-R") == 0)
        {
            if (opt != OPT_ADD)
                usage();
            opt = OPT_REMOVE_FROM;
        }
        else if (strcmp(arg, "-D") == 0)
        {
            if (opt != OPT_ADD)
                usage();
            dry_run = 1;
            work_dir = ".";
        }
        else if ((strcmp(arg, "-l") == 0) ||
                 (strcmp(arg, "--list") == 0))
        {
            if (opt != OPT_ADD)
                usage();
            opt = OPT_LIST;
        }
        else if ((strcmp(arg, "-a") == 0) ||
                 (strcmp(arg, "--listall") == 0) ||
                 (strcmp(arg, "--all") == 0))
        {
            if (opt != OPT_ADD)
                usage();
            opt = OPT_LIST_ALL;
        }
        else if (strcmp(arg, "-d") == 0)
        {
            if (argn == argc)
                usage();
            overlay_src_dir = argv[argn++];
        }
        else if (strcmp(arg, "-p") == 0)
        {
            if (argn == argc)
                usage();
            platform_string = argv[argn++];
            platform_string_len = strlen(platform_string) + 1;
        }
        else if (strcmp(arg, "-v") == 0)
        {
            opt_verbose = 1;
        }
        else if (strcmp(arg, "-h") == 0)
        {
            opt = OPT_HELP;
        }
        else
        {
            fprintf(stderr, "* unknown option '%s'\n", arg);
            usage();
        }
    }

    if ((opt == OPT_ADD) || (opt == OPT_REMOVE) ||
        (opt == OPT_REMOVE_FROM) || (opt == OPT_HELP))
    {
        if ((argn == argc) &&
            ((!is_dtparam &&
              ((opt == OPT_ADD) || (opt == OPT_HELP))) ||
             (is_dtparam && (opt == OPT_HELP))))
            usage();
        if (is_dtparam && (opt == OPT_ADD) && (argn == argc))
            opt = OPT_HELP;
        if (is_dtparam &&
            ((opt == OPT_ADD) || (opt == OPT_HELP)))
            overlay = "dtparam";
        else if (argn < argc)
            overlay = argv[argn++];
    }

    if ((opt == OPT_HELP) && (argn < argc))
    {
        params = &argv[argn];
        argn = argc;
    }

    if ((opt != OPT_ADD) && (argn != argc))
        usage();

    dtoverlay_enable_debug(opt_verbose);

    modifying = !dry_run && ((opt == OPT_ADD) || (opt == OPT_REMOVE) ||
                             (opt == OPT_REMOVE_FROM));
    in_hook = (getenv(HOOK_ENV) != NULL);

    /* The hooks run while the state is locked, so any attempt to change it
       from a hook would deadlock (and risks infinite recursion) */
    if (modifying && in_hook)
        fatal_error("%s can't add or remove overlays from a dtoverlay-pre or "
                    "dtoverlay-post hook", cmd_name);

    if (!overlay_src_dir)
    {
        /* Find the overlays and README */
        const char *os_prefix;
        int i;

        /* Check for an os_prefix value */
        os_prefix = read_file_as_string("/proc/device-tree/chosen/os_prefix",
                                        NULL);
        if (!os_prefix)
            os_prefix = "";

        for (i = 0; boot_dirs[i]; i++)
        {
            overlay_src_dir = sprintf_dup("%s/%s" OVERLAY_SRC_SUBDIR,
                                          boot_dirs[i], os_prefix);
            if (dir_exists(overlay_src_dir))
                break;
            free_string(overlay_src_dir);
            overlay_src_dir = NULL;
        }

        if (!overlay_src_dir)
            fatal_error("Failed to find overlays directory");
    }

    if (opt == OPT_HELP)
    {
        overlay_help(overlay, params);
        goto orderly_exit;
    }

    if (modifying)
        root_check();

    if (!dry_run)
    {
        /* Only root may create the state directory. Others can still list
           the active overlays if it exists. */
        have_work_dir = check_work_dir(work_dir, getuid() == 0);

        /* Serialise against other instances, holding the lock until exit.
           A hook runs while its parent holds the lock exclusively, so the
           state can't change under it and it must not wait for the lock. */
        if (have_work_dir && !in_hook)
            lock_state(modifying);
    }

    error_file = sprintf_dup("%s/%s", work_dir, "error.dtb");

    cfg_dir = CFG_DIR DT_SUBDIR;
    if (!dry_run && !dir_exists(cfg_dir))
    {
        /* configfs isn't mounted in the usual place, so mount it in the
           state directory, which is on a tmpfs */
        const char *mount_dir = sprintf_dup("%s/%s", work_dir,
                                            CFG_MOUNT_SUBDIR);

        root_check();

        /* Only one instance may mount configfs. Listing only holds a shared
           lock, so upgrade it (by now the root user has a lock file). */
        if (!modifying && !in_hook)
            lock_state(1);

        cfg_dir = sprintf_dup("%s%s", mount_dir, DT_SUBDIR);
        if (!dir_exists(cfg_dir))
        {
            if ((mkdir(mount_dir, DIR_MODE) != 0) && (errno != EEXIST))
                fatal_error("Failed to create '%s' - %s", mount_dir,
                            strerror(errno));
            if (mount("configfs", mount_dir, "configfs",
                      MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0)
                fatal_error("Failed to mount configfs on '%s' - %s",
                            mount_dir, strerror(errno));
            if (!dir_exists(cfg_dir))
            {
                umount(mount_dir);
                fatal_error("configfs has no '%s' folder - incompatible kernel",
                            DT_SUBDIR + 1);
            }
        }
    }

    if (!platform_string)
        platform_string = read_file_as_string("/proc/device-tree/compatible",
                                              &platform_string_len);

    if (platform_string)
        dtoverlay_init_map(overlay_src_dir, platform_string, platform_string_len);

    if (!dry_run)
    {
        dt_overlays_dir = sprintf_dup("%s/%s", cfg_dir, DT_OVERLAYS_SUBDIR);
        if (!dir_exists(dt_overlays_dir))
            fatal_error("configfs overlays folder not found - incompatible kernel");

        state = read_state(have_work_dir ? work_dir : NULL);
        if (!state)
            fatal_error("Failed to read state");
    }

    switch (opt)
    {
    case OPT_ADD:
    case OPT_REMOVE:
    case OPT_REMOVE_FROM:
        if (!dry_run)
        {
            setenv(HOOK_ENV, "1", 1);
            run_cmd("which dtoverlay-pre >/dev/null 2>&1 && dtoverlay-pre");
        }
        break;
    default:
        break;
    }

    /* Don't let a signal leave configfs and the state directory out of
       step - act on it once they agree again */
    if (modifying)
        defer_signals(1);

    switch (opt)
    {
    case OPT_ADD:
        ret = dtoverlay_add(state, overlay, argc - argn, argv + argn);
        break;
    case OPT_REMOVE:
        ret = dtoverlay_remove(state, overlay, 0);
        break;
    case OPT_REMOVE_FROM:
        ret = dtoverlay_remove(state, overlay, 1);
        break;
    case OPT_LIST:
        ret = dtoverlay_list(state);
        break;
    case OPT_LIST_ALL:
        ret = dtoverlay_list_all(state);
        break;
    default:
        ret = 1;
        break;
    }

    /* Any pending signal is delivered here, and the hook doesn't run */
    if (modifying)
        defer_signals(0);

    switch (opt)
    {
    case OPT_ADD:
    case OPT_REMOVE:
    case OPT_REMOVE_FROM:
        if (!dry_run)
            run_cmd("which dtoverlay-post >/dev/null 2>&1 && dtoverlay-post");
        break;
    default:
        break;
    }

  orderly_exit:
    if (state)
        free_state(state);
    free_strings();

    if ((ret == 0) && error_file)
        unlink(error_file);

    return ret;
}

static int dtparam_callback(int override_type, const char *override_value,
                            DTBLOB_T *dtb, int node_off,
                            const char *prop_name, int target_phandle,
                            int target_off, int target_size,
                            void *callback_state)
{
    STRING_VEC_T *used_props = callback_state;
    char prop_id[80];
    int err;

    err = dtoverlay_override_one_target(override_type,
                                        override_value,
                                        dtb, node_off,
                                        prop_name, target_phandle,
                                        target_off, target_size,
                                        callback_state);

    if ((err == 0) && (target_phandle != 0))
    {
        if (snprintf(prop_id, sizeof(prop_id), "%08x%s", target_phandle,
                     prop_name) < 0)
            err = FDT_ERR_INTERNAL;
        else if (string_vec_find(used_props, prop_id, 0) < 0)
            string_vec_add(used_props, prop_id, 0);
    }

    return err;
}

// Returns 0 on success, -ve for fatal errors and +ve for non-fatal errors
static int dtparam_apply(DTBLOB_T *dtb, const char *override_name,
                         const char *override_data, int data_len,
                         const char *override_value, STRING_VEC_T *used_props)
{
    void *data;
    int err;

    /* Copy the override data in case it moves */
    data = malloc(data_len);
    if (data)
    {
        memcpy(data, override_data, data_len);
        err = dtoverlay_foreach_override_target(dtb, override_name,
                                                data, data_len,
                                                override_value,
                                                dtparam_callback,
                                                used_props);
        free(data);
    }
    else
    {
        dtoverlay_error("out of memory");
        err = NON_FATAL(FDT_ERR_NOSPACE);
    }

    return err;
}

static void intra_fragment_merged_callback(DTBLOB_T *dtb, int fragment_off,
                                           int target_off)
{
    // look for all fixups that refer to the indicated fragment, and rebase them
    // onto the target
    char fragment_path[DTOVERLAY_MAX_PATH];
    char target_path[DTOVERLAY_MAX_PATH];
    char *src_path;
    char *dst_path;
    DTBLOB_T clone_dtb;
    DTBLOB_T *src_dtb;
    int fragment_path_len;
    int target_path_len;
    int fixups_off;
    int fixup_off;
    int src_off;
    int dst_off;

    fdt_get_path(dtb->fdt, fragment_off, fragment_path, sizeof(fragment_path));
    fragment_path_len = strlen(fragment_path);
    fragment_path[fragment_path_len++] = ':';
    fragment_path[fragment_path_len] = '\0';

    fdt_get_path(dtb->fdt, target_off, target_path, sizeof(target_path));
    target_path_len = strlen(target_path);
    target_path[target_path_len++] = ':';
    target_path[target_path_len] = '\0';

    fixups_off = fdt_path_offset(dtb->fdt, "/__fixups__");
    if (fixups_off < 0)
        goto try_local_fixups;

    for (fixup_off = fdt_first_property_offset(dtb->fdt, fixups_off);
         fixup_off >= 0;
         fixup_off = fdt_next_property_offset(dtb->fdt, fixup_off))
    {
        const char *fixups_stringlist;
        const char *symbol_name;
        char *new_list = NULL;
        int list_len;
        int new_len;
        int err;
        int i;

        fixups_stringlist = fdt_getprop_by_offset(dtb->fdt, fixup_off,
                                                  &symbol_name, &list_len);
        for (i = 0; i < 2; i++)
        {
            new_len = dtoverlay_stringlist_replace(fixups_stringlist, list_len,
                                                   fragment_path, fragment_path_len,
                                                   target_path, target_path_len,
                                                   new_list);
            if (new_len < 0)
                break;
            if (!new_list)
                new_list = malloc(new_len);
        }

        if (!new_list)
            continue;

        err = fdt_setprop(dtb->fdt, fixups_off, symbol_name, new_list, new_len);
        free(new_list);
        if (err < 0)
            break;
    }

try_local_fixups:
    // Strip off the trailing ':'s
    fragment_path[--fragment_path_len] = '\0';
    target_path[--target_path_len] = '\0';
    src_path = sprintf_dup("/__local_fixups__%s", fragment_path);
    dst_path = sprintf_dup("/__local_fixups__%s", target_path);
    src_dtb = dtb;
    src_off = fdt_path_offset(src_dtb->fdt, src_path);
    if (src_off < 0) // No phandles in the fragment payload
        return;

    dst_off = dtoverlay_create_node(dtb, dst_path, 0);
    if (dst_off < 0)
        fatal_error("failed to copy local fixups");
    if (dst_off < src_off)
    {
        // Make a copy of the src, so that it doesn't move
        int overlay_size = fdt_totalsize(dtb->fdt);
        void *overlay_copy = malloc(overlay_size);
        if (!overlay_copy)
            fatal_error("out of memory");
        memcpy(overlay_copy, dtb->fdt, overlay_size);
        memcpy(&clone_dtb, dtb, sizeof(DTBLOB_T));
        clone_dtb.fdt = overlay_copy;
        src_dtb = &clone_dtb;
        src_off = fdt_path_offset(src_dtb->fdt, src_path);
    }
    if (dtoverlay_merge_fragment(dtb, dst_off, src_dtb, src_off, 1))
        fatal_error("failed to copy local fixups");
    if (src_dtb != dtb)
        free(src_dtb->fdt);
}

static void cell_changed_callback(DTBLOB_T *dtb, int node_off, const char *prop_name,
                                  int target_off, int cell_data_offset)
{
    char cell_target_loc[DTOVERLAY_MAX_PATH];
    const char *symbol;
    int path_len;

    sprintf(cell_source_loc + cell_source_loc_len, "%d", cell_data_offset); // XXX

    fdt_get_path(dtb->fdt, node_off, cell_target_loc,
                 sizeof(cell_target_loc));
    path_len = strlen(cell_target_loc);
    sprintf(cell_target_loc + path_len, ":%s:%d", prop_name, target_off); // XXX

    dtoverlay_delete_fixup(dtb, cell_target_loc);
    symbol = dtoverlay_find_fixup(dtb, cell_source_loc);
    if (symbol)
        dtoverlay_add_fixup(dtb, symbol, cell_target_loc);
}

// Returns 0 on success, -ve for fatal errors and +ve for non-fatal errors
static int apply_override(DTBLOB_T *dtb, const char *override_name,
                          const char *override_data, int data_len,
                          const char *override_value)
{
    cell_source_loc_len = sprintf(cell_source_loc, "/__overrides__:%s:", override_name);

    return dtoverlay_foreach_override_target(dtb, override_name,
                                             override_data, data_len,
                                             override_value,
                                             dtoverlay_override_one_target,
                                             NULL);
}

static int apply_parameter(DTBLOB_T *overlay_dtb, const char *arg, int is_dtparam,
                           STRING_VEC_T *used_props, char **param_string)
{
    const char *param_val = strchr(arg, '=');
    const char *param, *override;
    char *p = NULL;
    int override_len;
    int err = 0;

    if (param_val)
    {
        int len = (param_val - arg);
        p = sprintf_dup("%.*s", len, arg);
        param = p;
        param_val++;
    }
    else
    {
        /* Use the default parameter value - true */
        param = arg;
        param_val = "true";
    }

    override = dtoverlay_find_override(overlay_dtb, param, &override_len);

    if (!override)
        return error("Unknown parameter '%s'", param);

    if (is_dtparam)
        err = dtparam_apply(overlay_dtb, param,
                            override, override_len,
                            param_val, used_props);
    else
        err = apply_override(overlay_dtb, param,
                             override, override_len,
                             param_val);
    if (err != 0)
        return error("Failed to set %s=%s", param, param_val);

    *param_string = sprintf_dup("%s %s=%s",
                                *param_string ? *param_string : "",
                                param, param_val);

    free_string(p);

    return 0;
}

static int dtoverlay_add(STATE_T *state, const char *overlay,
                         int argc, const char **argv)
{
    const char *overlay_name;
    const char *overlay_file;
    char *overrides = NULL;
    char *param_string = NULL;
    int is_dtparam;
    DTBLOB_T *base_dtb = NULL;
    DTBLOB_T *overlay_dtb;
    STRING_VEC_T used_props;
    int err;
    int len;
    int i;

    len = strlen(overlay) - 5;
    is_dtparam = (strcmp(overlay, "dtparam") == 0);
    if (is_dtparam)
    {
        /* Convert /proc/device-tree to a .dtb and load it */
        overlay_file = sprintf_dup("%s/%s", work_dir, "base.dtb");
        if (run_cmd("dtc -I fs -O dtb -o '%s' /proc/device-tree 1>/dev/null 2>&1",
                    overlay_file) != 0)
            return error("Failed to read active DTB");
    }
    else if ((len > 0) && (strcmp(overlay + len, ".dtbo") == 0))
    {
        const char *p;
        overlay_file = overlay;
        p = strrchr(overlay, '/');
        if (p)
        {
            overlay = p + 1;
            len = strlen(overlay) - 5;
        }

        overlay = sprintf_dup("%.*s", len, overlay);
    }
    else
    {
        /* Allow for comma-separated parameters */
        const char *in_params = strchr(overlay, ',');
        const char *remapped;
        int name_len;

        if (in_params && in_params[1])
        {
            int len = in_params - overlay;
            overlay = sprintf_dup("%.*s", len, overlay);
            if (!overlay)
                return error("Out of memory");
            in_params++;
        }
        else
        {
            in_params = "";
        }

        remapped = dtoverlay_remap_overlay(overlay);
        if (!remapped)
            return error("Failed to load '%s'", overlay);
        if (strcmp(overlay, remapped))
            dtoverlay_debug("mapped overlay '%s' to '%s'", overlay, remapped);
        overlay = remapped;
        name_len = strcspn(overlay, ",");
        if (overlay[name_len] && overlay[name_len + 1])
        {
            /* There are parameters */
            overrides = sprintf_dup("%s,%s", overlay + name_len + 1, in_params);
        }
        else
        {
            overrides = strdup(in_params);
        }
        overlay = sprintf_dup("%.*s", name_len, overlay);
        overlay_file = sprintf_dup("%s/%s.dtbo", overlay_src_dir, overlay);
    }

    if (dry_run)
        overlay_name = "dry_run";
    else
        overlay_name = sprintf_dup("%d_%s", state->count, overlay);
    dtoverlay_debug("loading file '%s'", overlay_file);
    overlay_dtb = dtoverlay_load_dtb(overlay_file, DTOVERLAY_PADDING(4096));
    if (!overlay_dtb)
        return error("Failed to read '%s'", overlay_file);

    if (is_dtparam)
    {
        base_dtb = overlay_dtb;
        string_vec_init(&used_props);
    }

    dtoverlay_set_cell_changed_callback(&cell_changed_callback);

    /* Apply any parameters that came from the overlay map, along with
       any supplied on the command line separated by commas */
    while (overrides && *overrides)
    {
        int override_len = strcspn(overrides, ",");
        int last = !overrides[override_len];
        overrides[override_len] = '\0';
        err = apply_parameter(overlay_dtb, overrides, is_dtparam,
                              &used_props, &param_string);
        if (err)
            return err;
        if (last)
            break;
        overrides += override_len + 1;
    }

    /* Then any others supplied on the command line */
    for (i = 0; i < argc; i++)
    {
        err = apply_parameter(overlay_dtb, argv[i], is_dtparam,
                              &used_props, &param_string);
        if (err)
            return err;
    }

    dtoverlay_set_cell_changed_callback(NULL);

    /* Apply any intra-overlay fragments, filtering the symbols */
    dtoverlay_set_intra_fragment_merged_callback(intra_fragment_merged_callback);
    err = dtoverlay_merge_overlay(NULL, overlay_dtb);
    if (err != 0)
        return error("Failed to apply intra-overlay fragments");
    dtoverlay_set_intra_fragment_merged_callback(NULL);

    if (is_dtparam)
    {
        /* Build an overlay DTB */
        overlay_dtb = dtoverlay_create_dtb(2048 + 256 * used_props.num_strings);

        for (i = 0; i < used_props.num_strings; i++)
        {
            int phandle, node_off, prop_len;
            const char *str, *prop_name;
            const void *prop_data;

            str = used_props.strings[i];
            sscanf(str, "%8x", &phandle);
            prop_name = str + 8;
            node_off = dtoverlay_find_phandle(base_dtb, phandle);

            prop_data = dtoverlay_get_property(base_dtb, node_off,
                                               prop_name, &prop_len);
            err = dtoverlay_create_prop_fragment(overlay_dtb, i, phandle,
                                                 prop_name, prop_data, prop_len);
        }

        dtoverlay_free_dtb(base_dtb);
    }

    if (param_string)
        dtoverlay_dtb_set_trailer(overlay_dtb, param_string,
                                  strlen(param_string) + 1);

    /* Create a filename with the sequence number */
    overlay_file = sprintf_dup("%s/%s.dtbo", work_dir, overlay_name);

    /* then write the overlay to the file */
    dtoverlay_pack_dtb(overlay_dtb);
    err = save_dtb_atomic(overlay_dtb, overlay_file);
    dtoverlay_free_dtb(overlay_dtb);
    if (err)
        return error("Failed to write '%s'", overlay_file);

    if (!dry_run && !apply_overlay(overlay_file, overlay_name))
    {
        discard_overlay_file(overlay_file);
        return 1;
    }

    return 0;
}

static int dtoverlay_remove(STATE_T *state, const char *overlay, int and_later)
{
    const char *overlay_dir;
    const char *dir_name = NULL;
    char *end;
    int overlay_len;
    int count = state->count;
    int failed = 0;
    int rmpos;
    int i;

    if (chdir(work_dir) != 0)
        fatal_error("Failed to chdir to '%s'", work_dir);

    if (overlay)
    {
        overlay_len = strlen(overlay);

        rmpos = strtoul(overlay, &end, 10);
        if (end && (*end == '\0'))
        {
            if (rmpos >= count)
                return error("Overlay index (%d) too large", rmpos);
            dir_name = state->namelist[rmpos]->d_name;
        }
        /* Locate the most recent reference to the overlay */
        else for (rmpos = count - 1; rmpos >= 0; rmpos--)
        {
            const char *left, *right;
            dir_name = state->namelist[rmpos]->d_name;
            left = strchr(dir_name, '_');
            if (!left)
                return error("Internal error");
            left++;
            right = strchr(left, '.');
            if (!right)
                return error("Internal error");
            if (((right - left) == overlay_len) &&
                (memcmp(overlay, left, overlay_len) == 0))
                break;
            dir_name = NULL;
        }

        if (rmpos < 0)
            return error("Overlay '%s' is not loaded", overlay);
    }
    else
    {
        if (!count)
            return error("No overlays loaded");
        rmpos = and_later ? 0 : (count - 1);
        dir_name = state->namelist[rmpos]->d_name;
    }

    if (rmpos < count)
    {
        /* Unload it and all subsequent overlays in reverse order */
        for (i = count - 1; i >= rmpos; i--)
        {
            const char *left, *right;
            left = state->namelist[i]->d_name;
            right = strrchr(left, '.');
            if (!right)
                return error("Internal error");

            overlay_dir = sprintf_dup("%s/%.*s", dt_overlays_dir,
                                      right - left, left);
            if (rmdir(overlay_dir) != 0)
                return error("Failed to remove directory '%s'", overlay_dir);

            free_string(overlay_dir);
        }

        /* Replay the sequence, deleting files for the specified overlay,
           and renumbering and reloading all other overlays. */
        for (i = rmpos, state->count = rmpos; i < count; i++)
        {
            const char *left, *right;
            const char *filename = state->namelist[i]->d_name;

            left = strchr(filename, '_');
            if (!left)
                return error("Internal error");
            left++;
            right = strchr(left, '.');
            if (!right)
                return error("Internal error");

            if (and_later || (i == rmpos))
            {
                /* This one is being deleted */
                unlink(filename);
            }
            else
            {
                /* Keep this one - renumber and reload */
                int len = right - left;
                char *new_name = sprintf_dup("%d_%.*s", state->count,
                                             len, left);
                char *new_file = sprintf_dup("%s.dtbo", new_name);
                int ret = 0;

                if ((len == 7) && (memcmp(left, "dtparam", 7) == 0))
                {
                    /* Regenerate the overlay in case multiple overlays target
                       different parts of the same property. */

                    DTBLOB_T *dtb;
                    char *params;
                    const char **paramv;
                    int paramc;
                    int j;
                    char *p;

                    /* Extract the parameters */
                    dtb = dtoverlay_load_dtb(filename, 0);
                    unlink(filename);

                    if (!dtb)
                    {
                        error("Failed to re-apply dtparam");
                        failed++;
                        continue;
                    }

                    params = (char *)dtoverlay_dtb_trailer(dtb);
                    if (!params)
                    {
                        error("Failed to re-apply dtparam");
                        dtoverlay_free_dtb(dtb);
                        failed++;
                        continue;
                    }

                    /* Count and NUL-separate the params */
                    p = params;
                    paramc = 0;
                    while (*p)
                    {
                        int paramlen;
                        *(p++) = '\0';
                        paramlen = strcspn(p, " ");
                        paramc++;
                        p += paramlen;
                    }

                    paramv = malloc((paramc + 1) * sizeof(const char *));
                    if (!paramv)
                    {
                        error("out of memory re-applying dtparam");
                        dtoverlay_free_dtb(dtb);
                        failed++;
                        continue;
                    }

                    for (j = 0, p = params + 1; j < paramc; j++)
                    {
                        paramv[j] = p;
                        p += strlen(p) + 1;
                    }
                    paramv[j] = NULL;

                    /* Create the new overlay. If it can't be applied, the
                       file is discarded. */
                    ret = dtoverlay_add(state, "dtparam", paramc, paramv);
                    free(paramv);
                    dtoverlay_free_dtb(dtb);
                }
                else
                {
                    rename(filename, new_file);
                    ret = !apply_overlay(new_file, new_name);
                    if (ret != 0)
                        discard_overlay_file(new_file);
                }
                free_string(new_file);
                free_string(new_name);
                if (ret != 0)
                {
                    /* Its file has gone, so the next overlay takes its
                       number */
                    error("Failed to re-apply '%.*s' - it is no longer loaded",
                          len, left);
                    failed++;
                    continue;
                }
                state->count++;
            }
        }
    }

    return failed ? 1 : 0;
}

static int dtoverlay_list(STATE_T *state)
{
    if (state->count == 0)
    {
        printf("No overlays loaded\n");
    }
    else
    {
        int i;
        printf("Overlays (in load order):\n");
        for (i = 0; i < state->count; i++)
        {
            const char *name, *left, *right;
            const char *saved_overlay;
            DTBLOB_T *dtb;
            name = state->namelist[i]->d_name;
            left = strchr(name, '_');
            if (!left)
                return error("Internal error");
            left++;
            right = strchr(left, '.');
            if (!right)
                return error("Internal error");

            saved_overlay = sprintf_dup("%s/%s", work_dir, name);
            dtb = dtoverlay_load_dtb(saved_overlay, 0);
            free_string(saved_overlay);

            if (!dtb)
                printf("%d:  %.*s <unreadable>\n", i, (int)(right - left), left);
            else if (dtoverlay_dtb_trailer(dtb))
                printf("%d:  %.*s %.*s\n", i, (int)(right - left), left,
                       dtoverlay_dtb_trailer_len(dtb),
                       (char *)dtoverlay_dtb_trailer(dtb));
            else
                printf("%d:  %.*s\n", i, (int)(right - left), left);

            if (dtb)
                dtoverlay_free_dtb(dtb);
        }
    }

    return 0;
}

static int dtoverlay_list_all(STATE_T *state)
{
    int i;
    DIR *dh;
    struct dirent *de;
    STRING_VEC_T strings;

    string_vec_init(&strings);

    /* Enumerate .dtbo files in the /boot/overlays directory */
    dh = opendir(overlay_src_dir);
    while ((de = readdir(dh)) != NULL)
    {
        int len = strlen(de->d_name) - 5;
        if ((len >= 0) && strcmp(de->d_name + len, ".dtbo") == 0)
        {
            char *str = string_vec_add(&strings, de->d_name, len + 2);
            str[len] = '\0';
            str[len + 1] = ' ';
        }
    }
    closedir(dh);

    /* Merge in active overlays, marking them */
    for (i = 0; i < state->count; i++)
    {
        const char *left, *right;
        char *str;
        int len, idx;

        left = strchr(state->namelist[i]->d_name, '_');
        if (!left)
            return error("Internal error");
        left++;
        right = strchr(left, '.');
        if (!right)
            return error("Internal error");

        len = right - left;
        if ((len == 7) && (memcmp(left, "dtparam", 7) == 0))
            continue;
        idx = string_vec_find(&strings, left, len);
        if (idx >= 0)
        {
            str = strings.strings[idx];
            len = strlen(str);
        }
        else
        {
            str = string_vec_add(&strings, left, len + 2);
            str[len] = '\0';
        }
        str[len + 1] = '*';
    }

    if (strings.num_strings == 0)
    {
        printf("No overlays found\n");
    }
    else
    {
        /* Sort */
        string_vec_sort(&strings);

        /* Display */
        printf("All overlays (* = loaded):\n");

        for (i = 0; i < strings.num_strings; i++)
        {
            const char *str = strings.strings[i];
            printf("%c %s\n", str[strlen(str)+1], str);
        }
    }

    string_vec_uninit(&strings);

    return 0;
}

static void usage(void)
{
    printf("Usage:\n");
    if (strcmp(cmd_name, "dtparam") == 0)
    {
        printf("  %s                Display help on all parameters\n", cmd_name);
        printf("  %s <param>=<val>...\n", cmd_name);
        printf("  %*s                Add an overlay (with parameters)\n", (int)strlen(cmd_name), "");
        printf("  %s -D             Dry-run (prepare overlay, but don't apply -\n",
               cmd_name);
        printf("  %*s                save it as dry-run.dtbo)\n", (int)strlen(cmd_name), "");
        printf("  %s -r [<idx>]     Remove an overlay (by index, or the last)\n", cmd_name);
        printf("  %s -R [<idx>]     Remove from an overlay (by index, or all)\n",
               cmd_name);
        printf("  %s -l             List active overlays/dtparams\n", cmd_name);
        printf("  %s -a             List all overlays/dtparams (marking the active)\n", cmd_name);
        printf("  %s -h             Show this usage message\n", cmd_name);
        printf("  %s -h <param>...  Display help on the listed parameters\n", cmd_name);
    }
    else
    {
        printf("  %s <overlay> [<param>=<val>...]\n", cmd_name);
        printf("  %*s                Add an overlay (with parameters)\n", (int)strlen(cmd_name), "");
        printf("  %s -D             Dry-run (prepare overlay, but don't apply -\n",
               cmd_name);
        printf("  %*s                save it as dry-run.dtbo)\n", (int)strlen(cmd_name), "");
        printf("  %s -r [<overlay>] Remove an overlay (by name, index or the last)\n", cmd_name);
        printf("  %s -R [<overlay>] Remove from an overlay (by name, index or all)\n",
               cmd_name);
        printf("  %s -l             List active overlays/params\n", cmd_name);
        printf("  %s -a             List all overlays (marking the active)\n", cmd_name);
        printf("  %s -h             Show this usage message\n", cmd_name);
        printf("  %s -h <overlay>   Display help on an overlay\n", cmd_name);
        printf("  %s -h <overlay> <param>..  Or its parameters\n", cmd_name);
        printf("    where <overlay> is the name of an overlay or 'dtparam' for dtparams\n");
    }
    printf("Options applicable to most variants:\n");
    printf("    -d <dir>        Specify an alternate location for the overlays\n");
    printf("                    (defaults to /boot/overlays or /flash/overlays)\n");
    printf("    -p <string>     Force a compatible string for the platform\n");
    printf("    -v              Verbose operation\n");
    printf("\n");
    printf("Adding or removing overlays and parameters requires root privileges.\n");

    exit(1);
}

static void root_check(void)
{
    if (getuid() != 0)
        fatal_error("Must be run as root - try 'sudo %s ...'", cmd_name);
}

/* Returns 1 if the directory exists (creating it if requested), 0 if it
   doesn't. Exits if it exists but can't be trusted. */
static int check_work_dir(const char *dir, int create)
{
    struct stat st;

    if (create && (mkdir(dir, DIR_MODE) != 0) && (errno != EEXIST))
        fatal_error("Failed to create '%s' - %s", dir, strerror(errno));

    if (lstat(dir, &st) != 0)
    {
        if (errno == ENOENT)
            return 0;
        fatal_error("Failed to stat '%s' - %s", dir, strerror(errno));
    }

    /* The contents are replayed into the kernel, so they must only be
       writable by root */
    if (!S_ISDIR(st.st_mode) || (st.st_uid != 0) ||
        (st.st_mode & (S_IWGRP | S_IWOTH)))
        fatal_error("'%s' is not a directory owned and only writable by root",
                    dir);

    return 1;
}

/* Take a shared or exclusive lock on the state. It is released when the
   process exits, however that happens. */
static void lock_state(int exclusive)
{
    int op = (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB;
    int waited_ms = 0;

    if (lock_fd < 0)
    {
        const char *lock_path = sprintf_dup("%s/%s", work_dir, LOCK_FILE);

        if (getuid() == 0)
            lock_fd = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
                           0644);
        else
            lock_fd = open(lock_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

        if (lock_fd < 0)
        {
            /* Root has never modified the state, so there is nothing to
               protect */
            if ((errno == ENOENT) && (getuid() != 0))
                return;
            fatal_error("Failed to open '%s' - %s", lock_path,
                        strerror(errno));
        }
        free_string(lock_path);
    }

    while (flock(lock_fd, op) != 0)
    {
        if (errno == EINTR)
            continue;
        if (errno != EWOULDBLOCK)
            fatal_error("Failed to lock '%s' - %s", work_dir, strerror(errno));
        if (waited_ms == 0)
            dtoverlay_debug("waiting for another instance to finish");
        if (waited_ms >= LOCK_TIMEOUT_MS)
            fatal_error("Timed out waiting for another dtoverlay or dtparam "
                        "to finish");
        usleep(LOCK_POLL_MS * 1000);
        waited_ms += LOCK_POLL_MS;
    }
}

/* Block (or unblock) the signals that would normally terminate the process
   when sent by a user or a terminal. Unblocking delivers any that arrived
   in the meantime. SIGKILL can't be blocked. Child processes (dtc, when a
   dtparam is regenerated) inherit the blocked set. */
static void defer_signals(int defer)
{
    static sigset_t old_set;
    sigset_t set;

    if (defer)
    {
        sigemptyset(&set);
        sigaddset(&set, SIGHUP);
        sigaddset(&set, SIGINT);
        sigaddset(&set, SIGQUIT);
        sigaddset(&set, SIGTERM);
        sigaddset(&set, SIGPIPE);
        sigprocmask(SIG_BLOCK, &set, &old_set);
    }
    else
    {
        sigprocmask(SIG_SETMASK, &old_set, NULL);
    }
}

static void overlay_help(const char *overlay, const char **params)
{
    OVERLAY_HELP_STATE_T *state;
    const char *readme_path = sprintf_dup("%s/%s", overlay_src_dir,
                                          README_FILE);

    state = overlay_help_open(readme_path);
    free_string(readme_path);

    if (state)
    {
        if (strcmp(overlay, "dtparam") == 0)
            overlay = "<The base DTB>";

        if (overlay_help_find(state, overlay))
        {
            if (params && overlay_help_find_field(state, "Params"))
            {
                int in_param = 0;

                while (1)
                {
                    const char *line = overlay_help_field_data(state);
                    if (!line)
                        break;
                    if (line[0] == '\0')
                        continue;
                    if (line[0] != ' ')
                    {
                        /* This is a parameter name */
                        size_t param_len = strcspn(line, " ");
                        const char **p = params;
                        const char **q = p;
                        in_param = 0;
                        while (*p)
                        {
                            if ((param_len == strlen(*p)) &&
                                (memcmp(line, *p, param_len) == 0))
                                in_param = 1;
                            else
                                *(q++) = *p;
                            p++;
                        }
                        *(q++) = 0;
                    }
                    if (in_param)
                        printf("%s\n", line);
                }
                /* This only shows the first unknown parameter, but
                 * that is enough. */
                if (*params)
                    fatal_error("Unknown parameter '%s'", *params);
            }
            else
            {
                printf("Name:   %s\n\n", overlay);
                overlay_help_print_field(state, "Info", "Info:", 8, 0);
                overlay_help_print_field(state, "Load", "Usage:", 8, 0);
                overlay_help_print_field(state, "Params", "Params:", 8, 0);
            }
        }
        else
        {
            fatal_error("No help found for overlay '%s'", overlay);
        }
        overlay_help_close(state);
    }
    else
    {
        fatal_error("Help file not found");
    }
}

static int apply_overlay(const char *overlay_file, const char *overlay)
{
    const char *overlay_dir = sprintf_dup("%s/%s", dt_overlays_dir, overlay);
    int ret = 0;
    if (dir_exists(overlay_dir))
    {
        error("Overlay '%s' is already loaded", overlay);
    }
    else if (mkdir(overlay_dir, DIR_MODE) == 0)
    {
        DTBLOB_T *dtb = dtoverlay_load_dtb(overlay_file, 0);
        if (!dtb)
        {
            error("Failed to apply overlay '%s' (load)", overlay);
        }
        else
        {
            const char *dest_file = sprintf_dup("%s/dtbo", overlay_dir);

            /* then write the overlay to the file */
            if (dtoverlay_save_dtb(dtb, dest_file) != 0)
                error("Failed to apply overlay '%s' (save)", overlay);
            else if (!overlay_applied(overlay_dir))
                error("Failed to apply overlay '%s' (kernel)", overlay);
            else
                ret = 1;

            free_string(dest_file);
            dtoverlay_free_dtb(dtb);
        }

        if (!ret)
            rmdir(overlay_dir);
    }
    else
    {
        error("Failed to create overlay directory");
    }

    return ret;
}

/* Write a DTB so that a reader never sees a partial file, even if this
   process is killed. The state lives on a tmpfs that doesn't survive a
   power cut, so there is no need to fsync. The file must be in work_dir;
   the temporary name is hidden from seq_filter. */
static int save_dtb_atomic(DTBLOB_T *dtb, const char *filename)
{
    const char *tmp_file = sprintf_dup("%s/.new.dtbo", work_dir);
    int err;

    err = dtoverlay_save_dtb(dtb, tmp_file);
    if (!err && (rename(tmp_file, filename) != 0))
        err = -1;
    if (err)
        unlink(tmp_file);
    free_string(tmp_file);

    return err;
}

/* Remove the file of an overlay that failed to apply from the sequence.
   The first failure is kept as error.dtb for diagnosis. */
static void discard_overlay_file(const char *filename)
{
    if (error_file)
    {
        rename(filename, error_file);
        free_string(error_file);
        /* Don't delete it on exit */
        error_file = NULL;
    }
    else
    {
        unlink(filename);
    }
}

static int overlay_applied(const char *overlay_dir)
{
    char status[7] = { '\0' };
    const char *status_path = sprintf_dup("%s/status", overlay_dir);
    FILE *fp = fopen(status_path, "r");
    int bytes = 0;
    if (fp)
    {
        bytes = fread(status, 1, sizeof(status), fp);
        fclose(fp);
    }
    free_string(status_path);
    return (bytes == sizeof(status)) &&
        (memcmp(status, "applied", sizeof(status)) == 0);
}

int seq_filter(const struct dirent *de)
{
    int num;
    return (sscanf(de->d_name, "%d_", &num) == 1);
}

int seq_compare(const struct dirent **de1, const struct dirent **de2)
{
    int num1 = atoi((*de1)->d_name);
    int num2 = atoi((*de2)->d_name);
    if (num1 < num2)
        return -1;
    else if (num1 == num2)
        return 0;
    else
        return 1;
}

static STATE_T *read_state(const char *dir)
{
    STATE_T *state = malloc(sizeof(STATE_T));
    int i;

    if (state)
    {
        state->count = 0;
        state->namelist = NULL;
        if (dir)
        {
            state->count = scandir(dir, &state->namelist, seq_filter,
                                   seq_compare);
            if (state->count < 0)
            {
                free(state);
                return NULL;
            }
        }

        for (i = 0; i < state->count; i++)
        {
            int num = atoi(state->namelist[i]->d_name);
            if (i != num)
                error("Overlay sequence error");
        }
    }
    return state;
}

static void free_state(STATE_T *state)
{
    int i;
    for (i = 0; i < state->count; i++)
    {
        free(state->namelist[i]);
    }
    free(state->namelist);
    free(state);
}
