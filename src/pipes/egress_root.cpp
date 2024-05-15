#include "main.h"

// Input: port is the port to create the pipe on, port_id is the id of port
// Output: pipe is put into configured_pipe
doca_error_t
create_egress_root_pipe (
	struct doca_flow_port *port,
	uint16_t port_id,
    struct doca_flow_pipe **configured_pipe
)
{
	doca_error_t result = DOCA_SUCCESS;
	doca_flow_match match_any = {0};

	doca_flow_fwd fwd = {};
	/* Forwarding traffic to the wire */
	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id;

	struct doca_flow_monitor monitor_count = {};
	monitor_count.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;

	doca_flow_pipe_cfg *pipe_cfg;
	IF_SUCCESS(result, doca_flow_pipe_cfg_create(&pipe_cfg, port));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_name(pipe_cfg, "EGRESS_ROOT"));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_domain(pipe_cfg, DOCA_FLOW_PIPE_DOMAIN_EGRESS));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_is_root(pipe_cfg, true));
	// IF_SUCCESS(result, doca_flow_pipe_cfg_set_nr_entries(pipe_cfg, 1));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_match(pipe_cfg, &match_any, nullptr));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_miss_counter(pipe_cfg, true));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_monitor(pipe_cfg, &monitor_count));
	IF_SUCCESS(result, doca_flow_pipe_create(pipe_cfg, &fwd, nullptr/*&fwd_miss*/, configured_pipe));

	if (pipe_cfg) {
		doca_flow_pipe_cfg_destroy(pipe_cfg);
	}

	return result;
}