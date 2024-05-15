#include "main.h"

doca_error_t
create_ingress_root_pipe(
	struct doca_flow_port *port,
	uint16_t dst_port_id,
    struct doca_flow_pipe **configured_pipe
)
{
	DOCA_LOG_DBG("\n>> %s", __FUNCTION__);
	doca_error_t result = DOCA_SUCCESS;

	struct doca_flow_fwd fwd = {};
	/* Forwarding traffic to the egress root pipe (hairpin) */
	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = dst_port_id;

	doca_flow_match match_any = {0};

	doca_flow_pipe_cfg *pipe_cfg;
	IF_SUCCESS(result, doca_flow_pipe_cfg_create(&pipe_cfg, port));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_name(pipe_cfg, "INGRESS_ROOT"));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_type(pipe_cfg, DOCA_FLOW_PIPE_BASIC));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_is_root(pipe_cfg, true));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_match(pipe_cfg, &match_any, nullptr));
	// IF_SUCCESS(result, doca_flow_pipe_cfg_set_nr_entries(pipe_cfg, 3));
	IF_SUCCESS(result, doca_flow_pipe_create(pipe_cfg, &fwd, nullptr, configured_pipe));

	if (pipe_cfg) {
		doca_flow_pipe_cfg_destroy(pipe_cfg);
	}

	return result;
}
