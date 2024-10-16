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
#include <time.h>
#include <poll.h>

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
struct wl_seat *seat = NULL;
struct wl_pointer *pointer = NULL;

// Function prototypes
struct wl_buffer *create_buffer();

// Global variable to hold the shm format
uint32_t shm_format = WL_SHM_FORMAT_XRGB8888;

// Variables to hold the serial and seat
uint32_t input_serial = 0;

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

// Pointer listener
static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy) {
    // Do nothing
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface) {
    // Do nothing
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    // Do nothing
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time,
                           uint32_t button, uint32_t state) {
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        input_serial = serial;
        printf("Pointer button pressed, serial: %u\n", serial);
    }
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
                         uint32_t time, uint32_t axis,
                         wl_fixed_t value) {
    // Do nothing
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
    // Do nothing
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
                                uint32_t axis_source) {
    // Do nothing
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
                              uint32_t time, uint32_t axis) {
    // Do nothing
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
                                  uint32_t axis, int32_t discrete) {
    // Do nothing
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

// Seat listener
static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
        pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(pointer, &pointer_listener, NULL);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && pointer) {
        wl_pointer_destroy(pointer);
        pointer = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    // Do nothing
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
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
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
        wl_seat_add_listener(seat, &seat_listener, NULL);
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
    if (!compositor || !xdg_wm_base || !xdg_activation || !shm || !seat) {
        fprintf(stderr, "Missing required Wayland interfaces\n");
        return -1;
    }

    // Create the first window
    struct window *win1 = create_window("First Window", "com.example.firstwindow");
    printf("First window created\n");

    // Inform the user to click on the first window
    printf("Please click on the first window to capture input serial...\n");

    // Wait until we have an input serial from a pointer button event
    while (input_serial == 0) {
        wl_display_dispatch(display);
    }

    // Request an activation token
    char *token_str = NULL;
    struct xdg_activation_token_v1 *token = xdg_activation_v1_get_activation_token(xdg_activation);
    xdg_activation_token_v1_add_listener(token, &activation_token_listener, &token_str);
    xdg_activation_token_v1_set_serial(token, input_serial, seat);
    xdg_activation_token_v1_set_app_id(token, "com.example.firstwindow");
    xdg_activation_token_v1_commit(token);

    // Run the event loop to receive the token
    while (token_str == NULL) {
        wl_display_dispatch(display);
    }

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
