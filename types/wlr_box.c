#include <limits.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <wlr/types/wlr_box.h>
#include <wlr/util/log.h>

void wlr_box_closest_point(struct wlr_box *box, double x, double y,
		double *dest_x, double *dest_y) {
	// find the closest x point
	if (x < box->x) {
		*dest_x = box->x;
	} else if (x > box->x + box->width) {
		*dest_x = box->x + box->width;
	} else {
		*dest_x = x;
	}

	// find closest y point
	if (y < box->y) {
		*dest_y = box->y;
	} else if (y > box->y + box->height) {
		*dest_y = box->y + box->height;
	} else {
		*dest_y = y;
	}
}

bool wlr_box_empty(struct wlr_box *box) {
	return box == NULL || box->width <= 0 || box->height <= 0;
}

bool wlr_box_intersection(struct wlr_box *box_a,
		struct wlr_box *box_b, struct wlr_box **box_dest) {
	struct wlr_box *dest = *box_dest;
	bool a_empty = wlr_box_empty(box_a);
	bool b_empty = wlr_box_empty(box_b);

	if (a_empty || b_empty) {
		dest->x = 0;
		dest->y = 0;
		dest->width = -100;
		dest->height = -100;
		return false;
	}

	int x1 = fmax(box_a->x, box_b->x);
	int y1 = fmax(box_a->y, box_b->y);
	int x2 = fmin(box_a->x + box_a->width, box_b->x + box_b->width);
	int y2 = fmin(box_a->y + box_a->height, box_b->y + box_b->height);

	dest->x = x1;
	dest->y = y1;
	dest->width = x2 - x1;
	dest->height = y2 - y1;

	return !wlr_box_empty(dest);
}

bool wlr_box_contains_point(struct wlr_box *box, double x, double y) {
	if (wlr_box_empty(box)) {
		return false;
	} else {
		return x >= box->x && x <= box->x + box->width &&
			y >= box->y && y <= box->y + box->height;
	}
}
