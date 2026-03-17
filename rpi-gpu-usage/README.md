
# rpi-gpu-usage

rpi-gpu-usage is a simple tool for showing the per-process usage of the V3D
GPU on Raspberry Pi 4 and 5.  It works by parsing the /proc/\*/fdinfo/\* 
information to find the processes that have drm stats information like:

```
/proc/2390/fdinfo/23:drm-driver:              v3d
/proc/2390/fdinfo/23:drm-client-id:           14
/proc/2390/fdinfo/23:drm-engine-bin:          25951428711 ns
/proc/2390/fdinfo/23:drm-engine-render:       1400777565939 ns
/proc/2390/fdinfo/23:drm-engine-tfu:          26324241848 ns
/proc/2390/fdinfo/23:drm-engine-csd:          0 ns
/proc/2390/fdinfo/23:drm-engine-cache_clean:  0 ns
/proc/2390/fdinfo/23:drm-engine-cpu:          0 ns
/proc/2390/fdinfo/23:drm-total-memory:        74864 KiB
/proc/2390/fdinfo/23:drm-shared-memory:       31600 KiB
/proc/2390/fdinfo/23:drm-active-memory:       0
/proc/2390/fdinfo/23:drm-resident-memory:     43264 KiB
/proc/2390/fdinfo/23:drm-purgeable-memory:    0
```

The application then outputs total GPU usage for the renderer (shaders)
tfu (texture format unit) and binning blocks.  It also reads the total CPU
usage for that process.

```
GPU Utilisation

Client  PID      Process            render      tfu      bin      CPU
----------------------------------------------------------------------
    14  2215     labwc               13.2%     0.0%     0.3%    18.7%
    38  10668    chromium             0.0%     0.0%     0.0%     0.0%
```

**Command line options**

 * --csv   Output in CSV format. This can just be cat'ed to a file for analysis


**Build Instructions**

Install the prerequisites with "sudo apt install cmake libncurses-dev" - you need at least version 3.10 of cmake. Run the following commands, either here or in the top-level directory to build and install everything:

 - *cmake .*
 - *make*
 - *sudo make install*

