#ifndef WLR_TYPES_WLR_OUTPUT_H
#define WLR_TYPES_WLR_OUTPUT_H

#include <stdbool.h>
#include <wayland-util.h>
#include <wayland-server.h>

struct wlr_output_mode {
	uint32_t flags; // enum wl_output_mode
	int32_t width, height;
	int32_t refresh; // mHz
	struct wl_list link;
};

struct wlr_output_cursor {
	struct wlr_output *output;
	double x, y;
	bool enabled;
	uint32_t width, height;
	int32_t hotspot_x, hotspot_y;
	struct wl_list link;

	// only when using a software cursor without a surface
	struct wlr_renderer *renderer;
	struct wlr_texture *texture;

	// only when using a cursor surface
	struct wlr_surface *surface;
	struct wl_listener surface_commit;
	struct wl_listener surface_destroy;
};

struct wlr_output_impl;

struct wlr_output {
	const struct wlr_output_impl *impl;
	struct wlr_backend *backend;

	struct wl_global *wl_global;
	struct wl_list wl_resources;

	uint32_t flags;
	char name[16];
	char make[48];
	char model[16];
	char serial[16];
	uint32_t scale;
	int32_t width, height;
	int32_t phys_width, phys_height; // mm
	enum wl_output_subpixel subpixel;
	enum wl_output_transform transform;
	bool needs_swap;

	float transform_matrix[16];

	/* Note: some backends may have zero modes */
	struct wl_list modes;
	struct wlr_output_mode *current_mode;

	struct {
		struct wl_signal frame;
		struct wl_signal swap_buffers;
		struct wl_signal resolution;
		struct wl_signal destroy;
	} events;

	struct wl_list cursors; // wlr_output_cursor::link
	struct wlr_output_cursor *hardware_cursor;

	// the output position in layout space reported to clients
	int32_t lx, ly;

	void *data;
};

struct wlr_surface;

void wlr_output_enable(struct wlr_output *output, bool enable);
bool wlr_output_set_mode(struct wlr_output *output,
	struct wlr_output_mode *mode);
void wlr_output_transform(struct wlr_output *output,
	enum wl_output_transform transform);
void wlr_output_set_position(struct wlr_output *output, int32_t lx, int32_t ly);
void wlr_output_destroy(struct wlr_output *output);
void wlr_output_effective_resolution(struct wlr_output *output,
	int *width, int *height);
void wlr_output_make_current(struct wlr_output *output);
void wlr_output_swap_buffers(struct wlr_output *output);
void wlr_output_set_gamma(struct wlr_output *output,
	uint32_t size, uint16_t *r, uint16_t *g, uint16_t *b);
uint32_t wlr_output_get_gamma_size(struct wlr_output *output);

struct wlr_output_cursor *wlr_output_cursor_create(struct wlr_output *output);
bool wlr_output_cursor_set_image(struct wlr_output_cursor *cursor,
	const uint8_t *pixels, int32_t stride, uint32_t width, uint32_t height,
	int32_t hotspot_x, int32_t hotspot_y);
void wlr_output_cursor_set_surface(struct wlr_output_cursor *cursor,
	struct wlr_surface *surface, int32_t hotspot_x, int32_t hotspot_y);
bool wlr_output_cursor_move(struct wlr_output_cursor *cursor,
	double x, double y);
void wlr_output_cursor_destroy(struct wlr_output_cursor *cursor);

#endif
