#ifndef WLR_TYPES_WLR_SERVER_DECORATION_H
#define WLR_TYPES_WLR_SERVER_DECORATION_H

#include <wayland-server.h>

struct wlr_server_decoration_manager {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wl_list decorations; // wlr_server_decoration::link

	uint32_t default_mode; // enum org_kde_kwin_server_decoration_manager_mode

	struct {
		struct wl_signal new_decoration;
	} events;

	void *data;
};

struct wlr_server_decoration {
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wl_list link;

	uint32_t mode; // enum org_kde_kwin_server_decoration_manager_mode

	struct {
		struct wl_signal destroy;
		struct wl_signal mode;
	} events;

	struct wl_listener surface_destroy_listener;

	void *data;
};

struct wlr_server_decoration_manager *wlr_server_decoration_manager_create(
	struct wl_display *display);
void wlr_server_decoration_manager_set_default_mode(
	struct wlr_server_decoration_manager *manager, uint32_t default_mode);
void wlr_server_decoration_manager_destroy(
	struct wlr_server_decoration_manager *manager);

#endif
