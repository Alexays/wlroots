#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/render/matrix.h>
#include <wlr/util/log.h>
#include "rootston/server.h"
#include "rootston/desktop.h"
#include "rootston/config.h"

static inline int64_t timespec_to_msec(const struct timespec *a) {
	return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

static void render_surface(struct wlr_surface *surface,
		struct roots_desktop *desktop, struct wlr_output *wlr_output,
		struct timespec *when, double lx, double ly, float rotation) {
	if (surface->texture->valid) {
		double surface_scale = surface->current->scale;
		double width = (double)surface->current->buffer_width / surface_scale;
		double height = (double)surface->current->buffer_height / surface_scale;
		int render_width = width * wlr_output->scale;
		int render_height = height * wlr_output->scale;
		double ox = lx, oy = ly;
		wlr_output_layout_output_coords(desktop->layout, wlr_output, &ox, &oy);
		ox *= wlr_output->scale;
		oy *= wlr_output->scale;

		if (wlr_output_layout_intersects(desktop->layout, wlr_output,
				lx, ly, lx + render_width, ly + render_height)) {
			float matrix[16];

			float translate_origin[16];
			wlr_matrix_translate(&translate_origin,
				(int)ox + render_width / 2, (int)oy + render_height / 2, 0);

			float rotate[16];
			wlr_matrix_rotate(&rotate, rotation);

			float translate_center[16];
			wlr_matrix_translate(&translate_center, -render_width / 2,
				-render_height / 2, 0);

			float scale[16];
			wlr_matrix_scale(&scale, render_width, render_height, 1);

			float transform[16];
			wlr_matrix_mul(&translate_origin, &rotate, &transform);
			wlr_matrix_mul(&transform, &translate_center, &transform);
			wlr_matrix_mul(&transform, &scale, &transform);
			wlr_matrix_mul(&wlr_output->transform_matrix, &transform, &matrix);

			wlr_render_with_matrix(desktop->server->renderer, surface->texture,
				&matrix);

			struct wlr_frame_callback *cb, *cnext;
			wl_list_for_each_safe(cb, cnext,
					&surface->current->frame_callback_list, link) {
				wl_callback_send_done(cb->resource, timespec_to_msec(when));
				wl_resource_destroy(cb->resource);
			}
		}

		struct wlr_subsurface *subsurface;
		wl_list_for_each(subsurface, &surface->subsurface_list, parent_link) {
			struct wlr_surface_state *state = subsurface->surface->current;
			double sx = state->subsurface_position.x;
			double sy = state->subsurface_position.y;
			double sw = state->buffer_width / state->scale;
			double sh = state->buffer_height / state->scale;
			if (rotation != 0.0) {
				// Coordinates relative to the center of the subsurface
				double ox = sx - width/2 + sw/2,
					oy = sy - height/2 + sh/2;
				// Rotated coordinates
				double rx = cos(-rotation)*ox - sin(-rotation)*oy,
					ry = cos(-rotation)*oy + sin(-rotation)*ox;
				sx = rx + width/2 - sw/2;
				sy = ry + height/2 - sh/2;
			}

			render_surface(subsurface->surface, desktop, wlr_output, when,
				lx + sx,
				ly + sy,
				rotation);
		}
	}
}

static void render_xdg_v6_popups(struct wlr_xdg_surface_v6 *surface,
		struct roots_desktop *desktop, struct wlr_output *wlr_output,
		struct timespec *when, double base_x, double base_y, float rotation) {
	// TODO: make sure this works with view rotation
	struct wlr_xdg_surface_v6 *popup;
	wl_list_for_each(popup, &surface->popups, popup_link) {
		if (!popup->configured) {
			continue;
		}

		double popup_x = base_x + surface->geometry->x +
			popup->popup_state->geometry.x - popup->geometry->x;
		double popup_y = base_y + surface->geometry->y +
			popup->popup_state->geometry.y - popup->geometry->y;
		render_surface(popup->surface, desktop, wlr_output, when, popup_x,
			popup_y, rotation);
		render_xdg_v6_popups(popup, desktop, wlr_output, when, popup_x, popup_y,
			rotation);
	}
}

static void render_wl_shell_surface(struct wlr_wl_shell_surface *surface, struct roots_desktop *desktop,
		struct wlr_output *wlr_output, struct timespec *when, double lx,
		double ly, float rotation, bool is_child) {
	if (is_child || surface->state != WLR_WL_SHELL_SURFACE_STATE_POPUP) {
		render_surface(surface->surface, desktop, wlr_output, when,
			lx, ly, rotation);
		struct wlr_wl_shell_surface *popup;
		wl_list_for_each(popup, &surface->popups, popup_link) {
			render_wl_shell_surface(popup, desktop, wlr_output, when,
				lx + popup->transient_state->x,
				ly + popup->transient_state->y,
				rotation, true);
		}
	}
}

static void render_view(struct roots_view *view, struct roots_desktop *desktop,
		struct wlr_output *wlr_output, struct timespec *when) {
	switch (view->type) {
	case ROOTS_XDG_SHELL_V6_VIEW:
		render_surface(view->wlr_surface, desktop, wlr_output, when,
			view->x, view->y, view->rotation);
		render_xdg_v6_popups(view->xdg_surface_v6, desktop, wlr_output,
			when, view->x, view->y, view->rotation);
		break;
	case ROOTS_WL_SHELL_VIEW:
		render_wl_shell_surface(view->wl_shell_surface, desktop, wlr_output,
			when, view->x, view->y, view->rotation, false);
		break;
	case ROOTS_XWAYLAND_VIEW:
		render_surface(view->wlr_surface, desktop, wlr_output, when,
			view->x, view->y, view->rotation);
		break;
	}
}

static void output_frame_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;
	struct roots_output *output = wl_container_of(listener, output, frame);
	struct roots_desktop *desktop = output->desktop;
	struct roots_server *server = desktop->server;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	wlr_output_make_current(wlr_output);
	wlr_renderer_begin(server->renderer, wlr_output);

	for (size_t i = 0; i < desktop->views->length; ++i) {
		struct roots_view *view = desktop->views->items[i];
		render_view(view, desktop, wlr_output, &now);
	}

	struct roots_drag_icon *drag_icon = NULL;
	struct roots_seat *seat = NULL;
	wl_list_for_each(seat, &server->input->seats, link) {
		wl_list_for_each(drag_icon, &seat->drag_icons, link) {
			if (!drag_icon->mapped) {
				continue;
			}
			struct wlr_surface *icon = drag_icon->surface;
			struct wlr_cursor *cursor = seat->cursor->cursor;
			double icon_x = cursor->x + drag_icon->sx;
			double icon_y = cursor->y + drag_icon->sy;
			render_surface(icon, desktop, wlr_output, &now, icon_x, icon_y, 0);
		}
	}

	wlr_renderer_end(server->renderer);
	wlr_output_swap_buffers(wlr_output);

	output->last_frame = desktop->last_frame = now;
}

static void set_mode(struct wlr_output *output,
		struct roots_output_config *oc) {
	struct wlr_output_mode *mode, *best = NULL;
	int mhz = (int)(oc->mode.refresh_rate * 1000);
	wl_list_for_each(mode, &output->modes, link) {
		if (mode->width == oc->mode.width && mode->height == oc->mode.height) {
			if (mode->refresh == mhz) {
				best = mode;
				break;
			}
			best = mode;
		}
	}
	if (!best) {
		wlr_log(L_ERROR, "Configured mode for %s not available", output->name);
	} else {
		wlr_log(L_DEBUG, "Assigning configured mode to %s", output->name);
		wlr_output_set_mode(output, best);
	}
}

void output_add_notify(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop = wl_container_of(listener, desktop,
		output_add);
	struct wlr_output *wlr_output = data;
	struct roots_input *input = desktop->server->input;
	struct roots_config *config = desktop->config;

	wlr_log(L_DEBUG, "Output '%s' added", wlr_output->name);
	wlr_log(L_DEBUG, "%s %s %s %"PRId32"mm x %"PRId32"mm", wlr_output->make,
		wlr_output->model, wlr_output->serial, wlr_output->phys_width,
		wlr_output->phys_height);
	if (wl_list_length(&wlr_output->modes) > 0) {
		struct wlr_output_mode *mode = NULL;
		mode = wl_container_of((&wlr_output->modes)->prev, mode, link);
		wlr_output_set_mode(wlr_output, mode);
	}

	struct roots_output *output = calloc(1, sizeof(struct roots_output));
	clock_gettime(CLOCK_MONOTONIC, &output->last_frame);
	output->desktop = desktop;
	output->wlr_output = wlr_output;
	output->frame.notify = output_frame_notify;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	wl_list_insert(&desktop->outputs, &output->link);

	struct roots_output_config *output_config =
		roots_config_get_output(config, wlr_output);
	if (output_config) {
		if (output_config->mode.width) {
			set_mode(wlr_output, output_config);
		}
		wlr_output->scale = output_config->scale;
		wlr_output_transform(wlr_output, output_config->transform);
		wlr_output_layout_add(desktop->layout,
				wlr_output, output_config->x, output_config->y);
	} else {
		wlr_output_layout_add_auto(desktop->layout, wlr_output);
	}

	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		if (wlr_xcursor_manager_load(seat->cursor->xcursor_manager,
				wlr_output->scale)) {
			wlr_log(L_ERROR, "Cannot load xcursor theme for output '%s' "
				"with scale %d", wlr_output->name, wlr_output->scale);
		}

		roots_seat_configure_cursor(seat);
		roots_seat_configure_xcursor(seat);
	}
}

void output_remove_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;
	struct roots_desktop *desktop = wl_container_of(listener, desktop, output_remove);
	struct roots_output *output = NULL, *_output;
	wl_list_for_each(_output, &desktop->outputs, link) {
		if (_output->wlr_output == wlr_output) {
			output = _output;
			break;
		}
	}
	if (!output) {
		return; // We are unfamiliar with this output
	}
	wlr_output_layout_remove(desktop->layout, output->wlr_output);
	// TODO: cursor
	//example_config_configure_cursor(sample->config, sample->cursor,
	//	sample->compositor);
	wl_list_remove(&output->link);
	wl_list_remove(&output->frame.link);
	free(output);
}
