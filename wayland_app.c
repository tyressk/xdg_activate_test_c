// main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <wayland-client.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>

// Include generated headers
#include "xdg-shell-client-protocol.h"
#include "xdg-activation-v1-client-protocol.h"

// Constants for the window size
#define WINDOW_WIDTH  200
#define WINDOW_HEIGHT 200

// Global variables for Wayland objects
struct wl_display *display = NULL;
struct wl_registry *registry = NULL;
struct wl_compositor *compositor = NULL;
struct wl_shm *shm = NULL;
struct xdg_wm_base *xdg_wm_base = NULL;
struct xdg_activation_v1 *xdg_activation = NULL;

// Function prototypes
struct wl_buffer *create_buffer();

// Global variable to hold the shm format
uint32_t shm_format = WL_SHM_FORMAT_XRGB8888;

// Window structure
struct window {
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    int configured;
};

// Function to handle xdg_surface events
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                                  uint32_t serial) {
    struct window *win = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    win->configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

// xdg_activation_token_v1 listener
static void activation_token_done(void *data,
                                  struct xdg_activation_token_v1 *token,
                                  const char *token_str) {
    char *token_copy = strdup(token_str);
    *(char **)data = token_copy;
    xdg_activation_token_v1_destroy(token);
}

static const struct xdg_activation_token_v1_listener activation_token_listener = {
    activation_token_done
};

// Registry listener
static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version) {
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    } else if (strcmp(interface, xdg_activation_v1_interface.name) == 0) {
        xdg_activation = wl_registry_bind(registry, name, &xdg_activation_v1_interface, 1);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name) {
    // Do nothing
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_global_remove
};

// Function to create a shared memory buffer
struct wl_buffer *create_buffer() {
    int stride = WINDOW_WIDTH * 4; // 4 bytes per pixel (ARGB8888)
    int size = stride * WINDOW_HEIGHT;

    // Create a temporary file for shared memory
    int fd = shm_open("/wayland-shm", O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        perror("shm_open failed");
        exit(1);
    }
    shm_unlink("/wayland-shm");

    // Resize the file to the required size
    if (ftruncate(fd, size) < 0) {
        perror("ftruncate failed");
        close(fd);
        exit(1);
    }

    // Map the file into memory
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        exit(1);
    }

    // Fill the buffer with a solid color (e.g., red)
    uint32_t *pixel = data;
    for (int i = 0; i < WINDOW_WIDTH * WINDOW_HEIGHT; ++i) {
        *pixel++ = 0xFFFF0000; // ARGB format: Alpha=0xFF, Red=0xFF, Green=0x00, Blue=0x00
    }

    // Create a wl_shm_pool
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
                                                         WINDOW_WIDTH, WINDOW_HEIGHT,
                                                         stride, shm_format);
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);

    return buffer;
}

// Function to create a window
struct window *create_window(const char *title, const char *app_id) {
    struct window *win = calloc(1, sizeof(struct window));

    win->surface = wl_compositor_create_surface(compositor);
    win->xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, win->surface);
    xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);

    win->xdg_toplevel = xdg_surface_get_toplevel(win->xdg_surface);
    xdg_toplevel_set_title(win->xdg_toplevel, title);
    xdg_toplevel_set_app_id(win->xdg_toplevel, app_id);

    // Commit the surface to inform the compositor about the new xdg_surface
    wl_surface_commit(win->surface);

    // Flush the requests to the compositor
    wl_display_flush(display);

    // Wait until we receive the configure event
    while (!win->configured) {
        wl_display_dispatch(display);
    }

    // Now we can create and attach a buffer to the surface
    struct wl_buffer *buffer = create_buffer();
    wl_surface_attach(win->surface, buffer, 0, 0);
    wl_surface_commit(win->surface);

    return win;
}

int main(int argc, char **argv) {
    // Connect to the Wayland display
    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return -1;
    }

    // Obtain the registry
    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    // Ensure we have the necessary globals
    if (!compositor || !xdg_wm_base || !xdg_activation || !shm) {
        fprintf(stderr, "Missing required Wayland interfaces\n");
        return -1;
    }

    // Create the first window
    struct window *win1 = create_window("First Window", "com.example.firstwindow");
    printf("First window created\n");

  

    // Request an activation token
    char *token_str = NULL;
    struct xdg_activation_token_v1 *token = xdg_activation_v1_get_activation_token(xdg_activation);
    xdg_activation_token_v1_add_listener(token, &activation_token_listener, &token_str);
    xdg_activation_token_v1_set_surface(token, win1->surface);
    xdg_activation_token_v1_commit(token);

    // Run the event loop to receive the token
    while (token_str == NULL) {
        wl_display_dispatch(display);
    }
  // Wait a few seconds
    sleep(3);
    printf("Activation token received: %s\n", token_str);

    // Create the second window
    struct window *win2 = create_window("Second Window", "com.example.secondwindow");
    printf("Second window created\n");

    // Activate the second window using the token
    xdg_activation_v1_activate(xdg_activation, token_str, win2->surface);
    free(token_str);

    // Run the event loop to display the second window and process activation
    while (wl_display_dispatch(display) != -1) {
        // Event loop
    }

    // Cleanup
    wl_display_disconnect(display);
    return 0;
}
