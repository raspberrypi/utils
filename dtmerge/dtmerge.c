/*
Copyright (c) 2016 Raspberry Pi (Trading) Ltd.
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
#include <libfdt.h>

#include "dtoverlay.h"

static void usage(void)
{
    printf("Usage:\n");
    printf("    dtmerge [<options] <base dtb> <merged dtb> - [param=value] ...\n");
    printf("        to apply a parameter to the base dtb, without an overlay (like dtparam)\n");
    printf("    dtmerge [<options] <base dtb> <merged dtb> <overlay dtb> [param=value] ...\n");
    printf("        to apply an overlay, optionally with parameters (like dtoverlay)\n");
    printf("  where <options> is any of:\n");
    printf("    -d      Enable debug output\n");
    printf("    -h      Show this help message\n");
    exit(1);
}

static int apply_override(DTBLOB_T *base_dtb, DTBLOB_T *overlay_dtb, char *override)
{
    char *param_name = override;
    char *param_value = param_name + strcspn(param_name, "=");
    const void *override_data;
    int data_len;
    int err;

    if (*param_value == '=')
    {
        *(param_value++) = '\0';
    }
    else
    {
        /* This isn't a well-formed parameter assignment, but it can be
        treated as an assignment of true. */
        param_value = "true";
    }

    override_data = dtoverlay_find_override(overlay_dtb, param_name,
                        &data_len);
    if (override_data)
    {
        err = dtoverlay_apply_override(overlay_dtb, param_name,
                        override_data, data_len,
                        param_value);
    }
    else
    {
        override_data = dtoverlay_find_override(base_dtb, param_name, &data_len);
        if (override_data)
        {
            err = dtoverlay_apply_override(base_dtb, param_name,
                            override_data, data_len,
                            param_value);
        }
        else
        {
            printf("* unknown param '%s'\n", param_name);
            err = data_len;
        }
    }

    return err;
}

int main(int argc, char **argv)
{
    const char *base_file;
    const char *merged_file;
    const char *overlay_file;
    const char *compatible;
    char *overlay_dir;
    char *p;
    DTBLOB_T *base_dtb;
    DTBLOB_T *overlay_dtb;
    int err = 0;
    int argn = 1;
    int max_dtb_size = 200000;
    int compatible_len;

    while ((argn < argc) && (argv[argn][0] == '-'))
    {
        const char *arg = argv[argn++];
        if ((strcmp(arg, "-d") == 0) ||
            (strcmp(arg, "--debug") == 0))
            dtoverlay_enable_debug(1);
        else if ((strcmp(arg, "-h") == 0) ||
                 (strcmp(arg, "--help") == 0))
            usage();
        else
        {
            printf("* Unknown option '%s'\n", arg);
            usage();
        }
    }

    if (argc < (argn + 3))
    {
        usage();
    }

    base_file = argv[argn++];
    merged_file = argv[argn++];
    overlay_file = argv[argn++];

    base_dtb = dtoverlay_load_dtb(base_file, max_dtb_size);
    if (!base_dtb)
    {
        printf("* failed to load '%s'\n", base_file);
        return -1;
    }

    if (strnlen(overlay_file, DTOVERLAY_MAX_PATH) == DTOVERLAY_MAX_PATH)
    {
        printf("* overlay filename too long\n");
        return -1;
    }

    overlay_dir = strdup(overlay_file);
    p = strrchr(overlay_dir, '/');
    if (p)
        *p = 0;
    else
        overlay_dir = ".";

    compatible = dtoverlay_get_property(base_dtb,
                                        dtoverlay_find_node(base_dtb, "/", 1),
                                        "compatible", &compatible_len);
    dtoverlay_init_map(overlay_dir, compatible, compatible_len);

    if (!dtoverlay_get_alias(base_dtb, "i2c"))
    {
        err = dtoverlay_set_synonym(base_dtb, "i2c_arm", "i2c0");
        err = dtoverlay_set_synonym(base_dtb, "i2c_vc", "i2c1");
        err = dtoverlay_set_synonym(base_dtb, "i2c_baudrate", "i2c0_baudrate");
        err = dtoverlay_set_synonym(base_dtb, "i2c_arm_baudrate", "i2c0_baudrate");
        err = dtoverlay_set_synonym(base_dtb, "i2c_vc_baudrate", "i2c1_baudrate");
    };

    if (strcmp(overlay_file, "-") == 0)
    {
        overlay_dtb = base_dtb;
    }
    else
    {
        char new_file[DTOVERLAY_MAX_PATH];
        char *overlay_name;
        const char *new_name;
        char *p;

        strcpy(new_file, overlay_file);
        overlay_name = strrchr(new_file, '/');
        if (overlay_name)
            overlay_name++;
        else
            overlay_name = new_file;
        p = strrchr(overlay_name, '.');
        if (p)
            *p = 0;
        new_name = dtoverlay_remap_overlay(overlay_name);
        if (new_name)
        {
            char *overrides = NULL;
            int len = strlen(overlay_name);
            int new_len = strcspn(new_name, ",");
            if (new_name[new_len] && new_name[new_len + 1])
            {
                /* There are parameters */
                overrides = strdup(new_name + new_len + 1);
            }
            if (new_len != len || memcmp(overlay_name, new_name, len))
            {
                dtoverlay_debug("mapped overlay '%s' to '%.*s'",
                                overlay_name, new_len, new_name);
                memcpy(overlay_name, new_name, new_len);
            }

            strcpy(overlay_name + new_len, ".dtbo");

            overlay_dtb = dtoverlay_load_dtb(new_file, max_dtb_size);
            if (overlay_dtb)
                err = dtoverlay_fixup_overlay(base_dtb, overlay_dtb);
            else
                err = -1;

            while (!err && overrides)
            {
                char *override = overrides;
                char *end;
                len = strcspn(override, ",");
                end = override + len;
                if (*end == ',' && *(end + 1))
                    overrides = end + 1;
                else
                    overrides = NULL;
                *end = '\0';
                err = apply_override(base_dtb, overlay_dtb, override);
            }
        }
        else
        {
            overlay_dtb = NULL;
            err = -2;
        }
    }

    while (!err && (argn < argc))
    {
        err = apply_override(base_dtb, overlay_dtb, argv[argn++]);
    }

    if (!err && (overlay_dtb != base_dtb))
    {
        err = dtoverlay_merge_overlay(base_dtb, overlay_dtb);

        dtoverlay_free_dtb(overlay_dtb);
    }

    if (!err)
    {
        dtoverlay_pack_dtb(base_dtb);
        err = dtoverlay_save_dtb(base_dtb, merged_file);
    }

    dtoverlay_free_dtb(base_dtb);

    return err;
}
