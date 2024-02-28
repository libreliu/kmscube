#include "drm-lease-v1-client-protocol.h"

#include <wayland-client.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <glib.h>

#include "drm-lease.h"

typedef struct _LeaseDevice {
  struct wp_drm_lease_device_v1 *lease_device;
  GList *connectors;
} LeaseDevice;

static int find_device_in_list(GList				*devices,
				struct wp_drm_lease_device_v1	*lease_device)
{
	int index;
	int list_length = g_list_length(devices);
	for (index = 0; index < list_length; index++) {
		LeaseDevice *device = g_list_nth_data (devices, index);
		if (device->lease_device == lease_device) {
			break;
		}
	}

	if (index < list_length) {
		return index;
	}

	fprintf(stderr, "Failed to find device in devices list\n");
	return -1;
}

typedef struct _Lease {
	struct wp_drm_lease_device_v1 *device;
	struct wp_drm_lease_connector_v1 *connector;
	struct wp_drm_lease_request_v1 *request;
	struct wp_drm_lease_v1 *lease;
	int32_t fd;
} Lease;

struct wayland_state {
	GList *devices;
	Lease lease;
};

static void connector_name(void				*data,
		struct wp_drm_lease_connector_v1	*connector,
		const char				*name)
{

}

static void connector_description(void			*data,
		struct wp_drm_lease_connector_v1 	*connector,
		const char				*description)
{

}

static void connector_id(void				*data,
		struct wp_drm_lease_connector_v1 	*connector,
		uint32_t				id)
{

}

static void connector_done(void				*data,
		struct wp_drm_lease_connector_v1 	*connector)
{

}

static void connector_withdrawn(void			*data,
		struct wp_drm_lease_connector_v1 	*connector)
{

}

static const struct wp_drm_lease_connector_v1_listener connector_listener = {
	.name = connector_name,
	.description = connector_description,
	.connector_id = connector_id,
	.done = connector_done,
	.withdrawn = connector_withdrawn
};

static void device_drm_fd(void			*data,
		struct wp_drm_lease_device_v1	*lease_device,
		int32_t				fd)
{
	close(fd);
}

static void device_connector(void			*data,
		struct wp_drm_lease_device_v1		*lease_device,
		struct wp_drm_lease_connector_v1	*connector)
{
	struct wayland_state *state = data;

	int device_index = find_device_in_list (state->devices, lease_device);
	if (device_index < 0) {
		fprintf(stderr, "Could not find device in list on connector event\n");
		return;
	}

	LeaseDevice *device = g_list_nth_data (state->devices, device_index);
	device->connectors = g_list_append (device->connectors, connector);
	wp_drm_lease_connector_v1_add_listener (connector, &connector_listener, state);
}

static void device_done(void			*data,
		struct wp_drm_lease_device_v1	*lease_device)
{

}

static void device_released(void		*data,
		struct wp_drm_lease_device_v1	*lease_device)
{

}

static const struct wp_drm_lease_device_v1_listener device_listener = {
	.drm_fd = device_drm_fd,
	.connector = device_connector,
	.done = device_done,
	.released = device_released
};

static void registry_global (void	*data,
		struct wl_registry	*wl_registry,
		uint32_t		name,
		const char		*interface,
		uint32_t		version)
{
	struct wayland_state *state = data;

	if (strcmp(interface, wp_drm_lease_device_v1_interface.name) == 0) {
		LeaseDevice *device = g_new0(LeaseDevice, 1);
		device->lease_device =
			wl_registry_bind(wl_registry, name, &wp_drm_lease_device_v1_interface, 1);
		wp_drm_lease_device_v1_add_listener (device->lease_device, &device_listener, state);
		state->devices = g_list_append(state->devices, device);
	}
}

static void registry_remove (void	*data,
		struct wl_registry	*registry,
		uint32_t		name)
{

}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_remove,
};

static void lease_fd(void		*data,
		struct wp_drm_lease_v1	*lease,
		int32_t			fd)
{
	struct wayland_state *state = data;
	state->lease.fd = fd;
}

static void lease_finished(void		*data,
		struct wp_drm_lease_v1	*lease)
{

}

static const struct wp_drm_lease_v1_listener lease_listener = {
	.lease_fd = lease_fd,
	.finished = lease_finished
};

int init_drm_lease(drmModeRes **resources)
{
	struct wayland_state state = { 0 };

	// Connect to wayland
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "Failed to establish Wayland connection\n");
		return -1;
	}

	// Bind to drm_lease_device globals
	struct wl_registry *registry = wl_display_get_registry (display);
	wl_registry_add_listener(registry, &registry_listener, &state);

	// Wait for compositor response
	wl_display_dispatch (display);
	wl_display_roundtrip (display);

	if (!state.devices) {
		fprintf(stderr, "No available wp_drm_lease_device_v1 globals available\n");
		return -1;
	}

	LeaseDevice *device = NULL;
	int device_list_length = g_list_length(state.devices);
	for (int i = 0; i < device_list_length; i++) {
		LeaseDevice *list_device = g_list_nth_data (state.devices, i);
		//Just choose the first device with at least one connector
		if (list_device->connectors) {
			device = list_device;
			break;
		}
	}

	if (!device) {
		fprintf(stderr, "Failed to find any devices with connectors\n");
		return -1;
	}

	state.lease.device = device->lease_device;
	state.lease.connector = g_list_first(device->connectors)->data;
	state.lease.request = wp_drm_lease_device_v1_create_lease_request (state.lease.device);
	wp_drm_lease_request_v1_request_connector (state.lease.request, state.lease.connector);
	state.lease.lease = wp_drm_lease_request_v1_submit (state.lease.request);
	wp_drm_lease_v1_add_listener (state.lease.lease, &lease_listener, &state);

	// Wait for compositor response
	wl_display_dispatch (display);
	wl_display_roundtrip (display);

	if (state.lease.fd < 0) {
		fprintf(stderr, "Failed to acquire drm lease: %d\n", state.lease.fd);
		return -1;
	}

	*resources = drmModeGetResources(state.lease.fd);
	if (!*resources) {
		fprintf(stderr, "Error acquiring DRM resources: %s\n", g_strerror(errno));
		return -1;
	}

	return state.lease.fd;
}
