/*
 * Tells the Windows program launcher how to set up Direct3D on this
 * computer.
 *
 *   raye-wine-gpu          one line per Vulkan device, then a verdict
 *   raye-wine-gpu --env    shell assignments for the launcher
 *
 * DXVK and VKD3D-Proton need a Vulkan 1.3 GPU. When there is none (no
 * driver, an old card, only the software renderer), Direct3D goes back
 * to the builtin DLLs, which draw with OpenGL. On NVIDIA's own driver,
 * NVAPI is turned on so DLSS and Reflex work.
 *
 * The answer is kept in $XDG_RUNTIME_DIR/raye-wine-gpu for the current
 * boot: asking Vulkan takes a moment and drivers only change with a
 * restart.
 *
 * Exit status: 0 when Direct3D can use Vulkan, 1 when it cannot.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VENDOR_NVIDIA 0x10de
#define MAX_DEVICES 16

static const char builtin_d3d[] = "d3d8,d3d9,d3d10core,d3d11,dxgi,d3d12,d3d12core=b";

struct verdict
{
    int vulkan;     /* a GPU that DXVK can use */
    int nvidia;     /* that GPU runs NVIDIA's own driver */
};

static const char *type_name(VkPhysicalDeviceType type)
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
    default: return "other";
    }
}

static int nvidia_driver(void)
{
    return access("/proc/driver/nvidia/version", R_OK) == 0;
}

static struct verdict probe(FILE *report)
{
    struct verdict v = {0, 0};
    PFN_vkGetInstanceProcAddr get_proc;
    PFN_vkCreateInstance create_instance;
    PFN_vkEnumeratePhysicalDevices enum_devices;
    PFN_vkGetPhysicalDeviceProperties get_props;
    PFN_vkDestroyInstance destroy_instance;
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    VkPhysicalDevice devices[MAX_DEVICES];
    uint32_t count = MAX_DEVICES, i;
    VkInstance instance;
    void *lib;

    if (!(lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL)))
    {
        if (report) fprintf(report, "no Vulkan loader\n");
        return v;
    }
    get_proc = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    create_instance = get_proc ? (PFN_vkCreateInstance)get_proc(NULL, "vkCreateInstance") : NULL;
    app.pApplicationName = "raye-wine-gpu";
    app.apiVersion = VK_API_VERSION_1_3;
    info.pApplicationInfo = &app;
    if (!create_instance || create_instance(&info, NULL, &instance) != VK_SUCCESS)
    {
        if (report) fprintf(report, "no Vulkan driver\n");
        dlclose(lib);
        return v;
    }
    enum_devices = (PFN_vkEnumeratePhysicalDevices)get_proc(instance, "vkEnumeratePhysicalDevices");
    get_props = (PFN_vkGetPhysicalDeviceProperties)get_proc(instance, "vkGetPhysicalDeviceProperties");
    destroy_instance = (PFN_vkDestroyInstance)get_proc(instance, "vkDestroyInstance");

    if (enum_devices(instance, &count, devices) < 0) count = 0;
    for (i = 0; i < count; i++)
    {
        VkPhysicalDeviceProperties props;
        int usable;

        get_props(devices[i], &props);
        usable = props.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU &&
                 props.apiVersion >= VK_API_VERSION_1_3;
        if (report)
            fprintf(report, "%s: %s, Vulkan %u.%u%s\n", props.deviceName,
                    type_name(props.deviceType), VK_API_VERSION_MAJOR(props.apiVersion),
                    VK_API_VERSION_MINOR(props.apiVersion), usable ? "" : " (not used)");
        if (!usable) continue;
        v.vulkan = 1;
        if (props.vendorID == VENDOR_NVIDIA && nvidia_driver()) v.nvidia = 1;
    }
    destroy_instance(instance, NULL);
    dlclose(lib);
    return v;
}

static int read_boot_id(char *buf, size_t size)
{
    FILE *f = fopen("/proc/sys/kernel/random/boot_id", "r");
    int ok;

    if (!f) return 0;
    ok = fgets(buf, size, f) != NULL;
    fclose(f);
    if (ok) buf[strcspn(buf, "\n")] = 0;
    return ok;
}

static int cache_path(char *buf, size_t size)
{
    const char *dir = getenv("XDG_RUNTIME_DIR");

    if (!dir || dir[0] != '/') return 0;
    return snprintf(buf, size, "%s/raye-wine-gpu", dir) < (int)size;
}

static int cached(struct verdict *v)
{
    char path[4096], boot[64], line[128], want[64];
    FILE *f;
    int ok = 0;

    if (!cache_path(path, sizeof(path)) || !read_boot_id(want, sizeof(want))) return 0;
    if (!(f = fopen(path, "r"))) return 0;
    if (fgets(line, sizeof(line), f) &&
        sscanf(line, "%63s %d %d", boot, &v->vulkan, &v->nvidia) == 3 &&
        !strcmp(boot, want))
        ok = 1;
    fclose(f);
    return ok;
}

static void remember(const struct verdict *v)
{
    char path[4096], tmp[4200], boot[64];
    FILE *f;

    if (!cache_path(path, sizeof(path)) || !read_boot_id(boot, sizeof(boot))) return;
    snprintf(tmp, sizeof(tmp), "%s.%d", path, (int)getpid());
    if (!(f = fopen(tmp, "w"))) return;
    fprintf(f, "%s %d %d\n", boot, v->vulkan, v->nvidia);
    if (fclose(f) == 0) rename(tmp, path);
    else unlink(tmp);
}

int main(int argc, char **argv)
{
    struct verdict v;

    if (argc == 2 && !strcmp(argv[1], "--env"))
    {
        if (!cached(&v))
        {
            v = probe(NULL);
            remember(&v);
        }
        /* overrides the launcher adds to WINEDLLOVERRIDES */
        if (!v.vulkan)
            printf("RAYE_WINE_OVERRIDES='%s'\n", builtin_d3d);
        else if (v.nvidia)
            printf("RAYE_WINE_OVERRIDES='nvapi,nvapi64,nvofapi64=n'\nDXVK_ENABLE_NVAPI=1\n");
        else
            printf("RAYE_WINE_OVERRIDES=''\n");
        return v.vulkan ? 0 : 1;
    }
    if (argc != 1)
    {
        fprintf(stderr, "usage: raye-wine-gpu [--env]\n");
        return 2;
    }
    v = probe(stdout);
    remember(&v);
    printf("Direct3D: %s%s\n", v.vulkan ? "Vulkan" : "builtin (OpenGL)",
           v.nvidia ? ", NVAPI on" : "");
    return v.vulkan ? 0 : 1;
}
