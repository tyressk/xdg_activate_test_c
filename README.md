gcc -g wayland_app.c xdg-shell-client-protocol.c xdg-activation-v1-client-protocol.c -o wayland_app $(pkg-config --cflags --libs wayland-client) -I.
