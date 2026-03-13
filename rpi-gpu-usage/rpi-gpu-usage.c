/*
 * rpi-gpu-usage - Monitor VideoCore VII GPU block utilisation on
 *                 Raspberry Pi 5/500.
 *
 * Copyright (c) 2025, Raspberry Pi Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <ctype.h>
#include <curses.h>
#include <dirent.h>
#include <glob.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS 256
#define MAX_COMM    64

/* Engine indices for the three we display */
#define ENG_RENDER 0
#define ENG_TFU    1
#define ENG_BIN    2
#define N_ENGINES  3

struct client {
    int id;
    int pid;
    char comm[MAX_COMM];
    long long eng_ns[N_ENGINES];
    double cpu_secs;
};

static char stats_path[512];
static long clk_tck;
static volatile sig_atomic_t quit;

static void handle_signal(int sig)
{
    (void)sig;
    quit = 1;
}

static int find_gpu_stats(void)
{
    glob_t g;
    if (glob("/sys/class/drm/card*/device/gpu_stats", 0, NULL, &g) ||
        g.gl_pathc == 0) {
        fprintf(stderr, "Error: no V3D gpu_stats found\n");
        globfree(&g);
        return -1;
    }
    snprintf(stats_path, sizeof(stats_path), "%s", g.gl_pathv[0]);
    globfree(&g);
    return 0;
}

/* Read the bin queue timestamp from gpu_stats (used as the time base) */
static long long read_gpu_timestamp(void)
{
    FILE *f = fopen(stats_path, "r");
    if (!f)
        return 0;

    char line[256];
    long long ts = 0;
    /* skip header */
    if (fgets(line, sizeof(line), f)) {
        while (fgets(line, sizeof(line), f)) {
            char queue[64];
            long long t;
            if (sscanf(line, "%63s %lld", queue, &t) == 2 &&
                strcmp(queue, "bin") == 0) {
                ts = t;
                break;
            }
        }
    }
    fclose(f);
    return ts;
}

static double read_proc_cpu(int pid)
{
    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return 0.0;

    char buf[2048];
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return 0.0;
    }
    fclose(f);

    /* Skip past comm (in parentheses), then parse utime+stime */
    char *p = strrchr(buf, ')');
    if (!p)
        return 0.0;
    p++;

    long long utime = 0, stime = 0;
    int field = 2;
    while (*p && field < 15) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (field == 13)
            utime = strtoll(p, NULL, 10);
        else if (field == 14)
            stime = strtoll(p, NULL, 10);
        while (*p && *p != ' ') p++;
        field++;
    }
    return (double)(utime + stime) / clk_tck;
}

/* Map fdinfo engine name to our index, or -1 */
static int engine_idx(const char *name)
{
    if (strcmp(name, "render") == 0) return ENG_RENDER;
    if (strcmp(name, "tfu") == 0)    return ENG_TFU;
    if (strcmp(name, "bin") == 0)    return ENG_BIN;
    return -1;
}

static int read_clients(struct client *clients)
{
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir)
        return 0;

    int n = 0;
    struct dirent *pde;
    while ((pde = readdir(proc_dir)) != NULL) {
        if (!isdigit((unsigned char)pde->d_name[0]))
            continue;
        int pid = atoi(pde->d_name);

        char fdinfo_path[128];
        snprintf(fdinfo_path, sizeof(fdinfo_path), "/proc/%d/fdinfo", pid);
        DIR *fdinfo_dir = opendir(fdinfo_path);
        if (!fdinfo_dir)
            continue;

        char comm[MAX_COMM] = "";
        int comm_read = 0;
        struct dirent *fde;

        while ((fde = readdir(fdinfo_dir)) != NULL) {
            if (fde->d_name[0] == '.')
                continue;

            char fpath[256];
            snprintf(fpath, sizeof(fpath), "%s/%s", fdinfo_path,
                     fde->d_name);
            FILE *f = fopen(fpath, "r");
            if (!f)
                continue;

            char line[256];
            int is_v3d = 0, cid = -1;
            long long eng[N_ENGINES] = {0};

            while (fgets(line, sizeof(line), f)) {
                if (strstr(line, "drm-driver:\tv3d")) {
                    is_v3d = 1;
                } else if (strncmp(line, "drm-client-id:", 14) == 0) {
                    cid = atoi(line + 14);
                } else if (strncmp(line, "drm-engine-", 11) == 0) {
                    char *colon = strchr(line + 11, ':');
                    if (colon) {
                        *colon = '\0';
                        int ei = engine_idx(line + 11);
                        if (ei >= 0)
                            eng[ei] = strtoll(colon + 1, NULL, 10);
                    }
                }
            }
            fclose(f);

            if (!is_v3d || cid < 0)
                continue;
            if (!eng[0] && !eng[1] && !eng[2])
                continue;

            if (!comm_read) {
                char cpath[128];
                snprintf(cpath, sizeof(cpath), "/proc/%d/comm", pid);
                FILE *cf = fopen(cpath, "r");
                if (cf) {
                    if (fgets(comm, sizeof(comm), cf)) {
                        char *nl = strchr(comm, '\n');
                        if (nl) *nl = '\0';
                    }
                    fclose(cf);
                }
                if (!comm[0])
                    snprintf(comm, sizeof(comm), "%d", pid);
                comm_read = 1;
            }

            /* Deduplicate: keep lowest PID per client id */
            int found = -1;
            for (int i = 0; i < n; i++) {
                if (clients[i].id == cid) {
                    found = i;
                    break;
                }
            }
            if (found >= 0) {
                if (pid < clients[found].pid) {
                    clients[found].pid = pid;
                    snprintf(clients[found].comm, MAX_COMM, "%s", comm);
                    memcpy(clients[found].eng_ns, eng, sizeof(eng));
                }
                continue;
            }

            if (n >= MAX_CLIENTS)
                continue;

            clients[n].id = cid;
            clients[n].pid = pid;
            snprintf(clients[n].comm, MAX_COMM, "%s", comm);
            memcpy(clients[n].eng_ns, eng, sizeof(eng));
            clients[n].cpu_secs = read_proc_cpu(pid);
            n++;
        }
        closedir(fdinfo_dir);
    }
    closedir(proc_dir);
    return n;
}

static struct client *find_by_id(struct client *list, int n, int id)
{
    for (int i = 0; i < n; i++) {
        if (list[i].id == id)
            return &list[i];
    }
    return NULL;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options]\n\n"
            "Monitor VideoCore VII GPU block utilisation on "
            "Raspberry Pi 5/500.\n\n"
            "  --csv   Output in CSV format (pipe to file)\n"
            "  --help  Show this help message\n",
            prog);
}

int main(int argc, char *argv[])
{
    int csv = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0)
            csv = 1;
        else if (strcmp(argv[i], "--help") == 0 ||
                 strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    clk_tck = sysconf(_SC_CLK_TCK);
    if (clk_tck <= 0)
        clk_tck = 100;

    if (find_gpu_stats() < 0)
        return 1;

    struct sigaction sa = { .sa_handler = handle_signal };
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (!csv) {
        initscr();
        cbreak();
        noecho();
        nodelay(stdscr, TRUE);
        curs_set(0);
    } else {
        printf("timestamp,client_id,pid,process,"
               "render%%,tfu%%,bin%%,cpu%%\n");
    }

    struct client prev[MAX_CLIENTS], curr[MAX_CLIENTS];
    int nprev, ncurr;
    long long prev_ts, curr_ts;
    double prev_wall, curr_wall;

    prev_ts = read_gpu_timestamp();
    nprev = read_clients(prev);
    prev_wall = ({struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
                  t.tv_sec + t.tv_nsec / 1e9;});

    sleep(1);

    while (!quit) {
        curr_ts = read_gpu_timestamp();
        ncurr = read_clients(curr);
        curr_wall = ({struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
                      t.tv_sec + t.tv_nsec / 1e9;});

        long long dt_ns = curr_ts - prev_ts;
        double wall_dt = curr_wall - prev_wall;

        if (!csv) {
            clear();
            mvprintw(0, 0, "GPU Utilisation\n\n");
            printw("%6s  %-8s %-16s %8s %8s %8s %8s\n",
                   "Client", "PID", "Process",
                   "render", "tfu", "bin", "CPU");
            printw("----------------------------------------------"
                   "------------------------\n");
        }

        for (int i = 0; i < ncurr; i++) {
            struct client *c = &curr[i];
            struct client *p = find_by_id(prev, nprev, c->id);

            double pcts[N_ENGINES];
            for (int e = 0; e < N_ENGINES; e++) {
                long long d = c->eng_ns[e] - (p ? p->eng_ns[e] : 0);
                pcts[e] = (dt_ns > 0 && d > 0)
                          ? (double)d / dt_ns * 100.0 : 0.0;
            }

            double cpu_d = c->cpu_secs - (p ? p->cpu_secs : 0.0);
            double cpct = wall_dt > 0 ? cpu_d / wall_dt * 100.0 : 0.0;

            if (csv)
                printf("%.3f,%d,%d,%s,%.1f,%.1f,%.1f,%.1f\n",
                       curr_wall, c->id, c->pid, c->comm,
                       pcts[ENG_RENDER], pcts[ENG_TFU], pcts[ENG_BIN],
                       cpct);
            else
                printw("%6d  %-8d %-16s"
                       " %7.1f%% %7.1f%% %7.1f%% %7.1f%%\n",
                       c->id, c->pid, c->comm,
                       pcts[ENG_RENDER], pcts[ENG_TFU], pcts[ENG_BIN],
                       cpct);
        }

        if (csv)
            fflush(stdout);
        else
            refresh();

        memcpy(prev, curr, ncurr * sizeof(*curr));
        nprev = ncurr;
        prev_ts = curr_ts;
        prev_wall = curr_wall;

        if (!csv) {
            /* Poll for 'q' during the 1s sleep */
            for (int ms = 0; ms < 1000 && !quit; ms += 100) {
                int ch = getch();
                if (ch == 'q' || ch == 'Q')
                    quit = 1;
                else
                    napms(100);
            }
        } else {
            sleep(1);
        }
    }

    if (!csv)
        endwin();

    return 0;
}
