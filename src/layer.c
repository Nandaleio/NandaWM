#include <stdlib.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "nanda.h"

/* Recompute positions and send configure for all layer surfaces on all
 * outputs, respecting exclusive zones so panels and docks carve out
 * screen real-estate in the correct order. */
void arrange_layers(struct nanda_server *server) {
	struct nanda_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct wlr_box full_area = {0};
		wlr_output_effective_resolution(output->wlr_output,
			&full_area.width, &full_area.height);
		struct wlr_box usable_area = full_area;

		for (int layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
				layer <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; layer++) {
			struct nanda_layer_surface *ls;
			wl_list_for_each(ls, &server->layer_surfaces, link) {
				if (ls->output != output) {
					continue;
				}
				if ((int)ls->layer_surface->current.layer != layer) {
					continue;
				}
				wlr_scene_layer_surface_v1_configure(
					ls->scene_layer_surface, &full_area, &usable_area);
			}
		}
	}
}

static void layer_surface_commit(struct wl_listener *listener, void *data) {
	struct nanda_layer_surface *ls =
		wl_container_of(listener, ls, surface_commit);
	/* Re-arrange on initial commit (sends first configure to client) and
	 * on any subsequent state changes (anchor, margin, exclusive_zone). */
	if (ls->layer_surface->initial_commit || ls->layer_surface->current.committed) {
		arrange_layers(ls->server);
	}
}

static void layer_surface_map(struct wl_listener *listener, void *data) {
	struct nanda_layer_surface *ls = wl_container_of(listener, ls, map);
	arrange_layers(ls->server);
}

static void layer_surface_unmap(struct wl_listener *listener, void *data) {
	struct nanda_layer_surface *ls = wl_container_of(listener, ls, unmap);
	arrange_layers(ls->server);
}

static void layer_surface_destroy(struct wl_listener *listener, void *data) {
	struct nanda_layer_surface *ls = wl_container_of(listener, ls, destroy);

	wl_list_remove(&ls->map.link);
	wl_list_remove(&ls->unmap.link);
	wl_list_remove(&ls->destroy.link);
	wl_list_remove(&ls->surface_commit.link);
	wl_list_remove(&ls->new_popup.link);
	wl_list_remove(&ls->link);

	free(ls);
}

static void layer_surface_new_popup(struct wl_listener *listener, void *data) {
	struct nanda_layer_surface *ls =
		wl_container_of(listener, ls, new_popup);
	struct wlr_xdg_popup *popup = data;
	popup->base->data = wlr_scene_xdg_surface_create(
		ls->scene_layer_surface->tree, popup->base);
}

void server_new_layer_surface(struct wl_listener *listener, void *data) {
	struct nanda_server *server =
		wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *layer_surface = data;

	/* The protocol requires us to assign an output if the client left it NULL. */
	if (!layer_surface->output) {
		if (wl_list_empty(&server->outputs)) {
			wlr_layer_surface_v1_destroy(layer_surface);
			return;
		}
		struct nanda_output *first =
			wl_container_of(server->outputs.next, first, link);
		layer_surface->output = first->wlr_output;
	}

	struct nanda_output *output = NULL;
	struct nanda_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->wlr_output == layer_surface->output) {
			output = o;
			break;
		}
	}
	if (!output) {
		wlr_layer_surface_v1_destroy(layer_surface);
		return;
	}

	struct nanda_layer_surface *ls = calloc(1, sizeof(*ls));
	ls->server = server;
	ls->output = output;
	ls->layer_surface = layer_surface;

	enum zwlr_layer_shell_v1_layer layer = layer_surface->pending.layer;
	ls->scene_layer_surface = wlr_scene_layer_surface_v1_create(
		server->layers[layer], layer_surface);
	ls->scene_layer_surface->tree->node.data = ls;

	ls->map.notify = layer_surface_map;
	wl_signal_add(&layer_surface->surface->events.map, &ls->map);
	ls->unmap.notify = layer_surface_unmap;
	wl_signal_add(&layer_surface->surface->events.unmap, &ls->unmap);
	ls->destroy.notify = layer_surface_destroy;
	wl_signal_add(&layer_surface->events.destroy, &ls->destroy);
	ls->surface_commit.notify = layer_surface_commit;
	wl_signal_add(&layer_surface->surface->events.commit, &ls->surface_commit);
	ls->new_popup.notify = layer_surface_new_popup;
	wl_signal_add(&layer_surface->events.new_popup, &ls->new_popup);

	wl_list_insert(&server->layer_surfaces, &ls->link);
}
