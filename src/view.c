#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "nanda.h"

/* Make view fill the primary output and give it keyboard focus. */
void focus_view(struct nanda_view *view) {
	struct nanda_server *server = view->server;
	struct nanda_view *prev = server->focused_view;

	if (prev == view) {
		return;
	}
	if (prev) {
		wlr_xdg_toplevel_set_activated(prev->xdg_toplevel, false);
		wlr_scene_node_set_enabled(&prev->scene_tree->node, false);
	}

	server->focused_view = view;
	wlr_scene_node_set_enabled(&view->scene_tree->node, true);
	wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);

	struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
	if (kb) {
		wlr_seat_keyboard_notify_enter(server->seat,
			view->xdg_toplevel->base->surface,
			kb->keycodes, kb->num_keycodes, &kb->modifiers);
	}
}

/* Alt+Tab: cycle focus to the previously opened view (wraps around). */
void cycle_view(struct nanda_server *server) {
	if (wl_list_empty(&server->views)) {
		return;
	}

	struct wl_list *next_link;
	if (!server->focused_view ||
			server->focused_view->link.prev == &server->views) {
		next_link = server->views.prev;
	} else {
		next_link = server->focused_view->link.prev;
	}

	struct nanda_view *next = wl_container_of(next_link, next, link);
	focus_view(next);
}

/* ---- sizing helpers ---- */

static void get_output_size(struct nanda_server *server, int *w, int *h) {
	*w = 800;
	*h = 600;
	struct nanda_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		wlr_output_effective_resolution(o->wlr_output, w, h);
		return;
	}
}

static void configure_fullscreen(struct nanda_view *view) {
	int w, h;
	get_output_size(view->server, &w, &h);
	wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, true);
	wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
	wlr_scene_node_set_position(&view->scene_tree->node, 0, 0);
}

/* ---- xdg surface events ---- */

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	struct nanda_view *view = wl_container_of(listener, view, map);
	view->mapped = true;

	configure_fullscreen(view);

	/* Append to tail; Alt+Tab cycles backward (newest→oldest) */
	wl_list_insert(view->server->views.prev, &view->link);
	focus_view(view);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	struct nanda_view *view = wl_container_of(listener, view, unmap);
	view->mapped = false;

	bool was_focused = (view->server->focused_view == view);
	wl_list_remove(&view->link);

	if (was_focused) {
		view->server->focused_view = NULL;
		if (!wl_list_empty(&view->server->views)) {
			struct nanda_view *next = wl_container_of(
				view->server->views.next, next, link);
			focus_view(next);
		}
	}
}

static void view_handle_destroy(struct wl_listener *listener, void *data) {
	struct nanda_view *view = wl_container_of(listener, view, destroy);

	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);

	free(view);
}

/* The protocol requires we always respond to maximize/fullscreen requests
 * with a configure, even if we deny them. We always grant them. */
static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data) {
	struct nanda_view *view = wl_container_of(listener, view, request_maximize);
	configure_fullscreen(view);
	wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
	struct nanda_view *view = wl_container_of(listener, view, request_fullscreen);
	configure_fullscreen(view);
	wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

/* ---- shell global events ---- */

void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	struct nanda_server *server =
		wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *toplevel = data;

	struct nanda_view *view = calloc(1, sizeof(*view));
	view->server = server;
	view->xdg_toplevel = toplevel;

	/* Create a scene subtree for this toplevel; its node.data lets us
	 * walk from a scene node back to the view. */
	view->scene_tree = wlr_scene_xdg_surface_create(
		server->shell_tree, toplevel->base);
	view->scene_tree->node.data = view;
	toplevel->base->data = view->scene_tree;

	/* Start hidden; focus_view() will enable it. */
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	/* wm_capabilities requires xdg-shell >= v5; skip for older clients */
	if (wl_resource_get_version(toplevel->base->resource) >= 5) {
		wlr_xdg_toplevel_set_wm_capabilities(toplevel,
			WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE |
			WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
	}

	/* Send initial size if outputs exist so the client doesn't open tiny */
	if (!wl_list_empty(&server->outputs)) {
		int w, h;
		get_output_size(server, &w, &h);
		wlr_xdg_toplevel_set_maximized(toplevel, true);
		wlr_xdg_toplevel_set_size(toplevel, w, h);
	}

	/* map/unmap live on wlr_surface in wlroots 0.18 */
	view->map.notify = xdg_toplevel_map;
	wl_signal_add(&toplevel->base->surface->events.map, &view->map);
	view->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);

	view->destroy.notify = view_handle_destroy;
	wl_signal_add(&toplevel->events.destroy, &view->destroy);
	view->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&toplevel->events.request_maximize, &view->request_maximize);
	view->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&toplevel->events.request_fullscreen, &view->request_fullscreen);
}

void server_new_xdg_popup(struct wl_listener *listener, void *data) {
	struct wlr_xdg_popup *popup = data;

	/* Attach popups (context menus, dropdowns) to their parent's scene tree
	 * so they render on top of and relative to the parent surface. */
	struct wlr_xdg_surface *parent =
		wlr_xdg_surface_try_from_wlr_surface(popup->parent);
	assert(parent != NULL);
	struct wlr_scene_tree *parent_tree = parent->data;
	popup->base->data = wlr_scene_xdg_surface_create(parent_tree, popup->base);
}
