#include "main.h"

doca_error_t
create_ingress_root_pipe(
	struct doca_flow_port *port,
	uint16_t dst_port_id,
    struct doca_flow_pipe **configured_pipe,
	struct doca_flow_pipe_entry **configured_entry
)
{
	DOCA_LOG_DBG("\n>> %s", __FUNCTION__);
	doca_error_t result = DOCA_SUCCESS;

	struct doca_flow_fwd fwd = {};
	/* Forwarding traffic to the egress root pipe (hairpin) */
	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = dst_port_id;

	struct doca_flow_fwd fwd_miss = {};
	/* Miss is dropped */
	fwd_miss.type = DOCA_FLOW_FWD_DROP;

	struct doca_flow_match match = {0};
	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.outer.ip4.dst_ip = 0xffffffff;

	struct doca_flow_monitor monitor_count = {};
	monitor_count.counter_type = DOCA_FLOW_RESOURCE_TYPE_NON_SHARED;

	struct doca_flow_pipe_cfg *pipe_cfg;
	IF_SUCCESS(result, doca_flow_pipe_cfg_create(&pipe_cfg, port));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_name(pipe_cfg, "INGRESS_ROOT"));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_type(pipe_cfg, DOCA_FLOW_PIPE_BASIC));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_is_root(pipe_cfg, true));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_match(pipe_cfg, &match, nullptr));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_miss_counter(pipe_cfg, true));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_nr_entries(pipe_cfg, 1));
	IF_SUCCESS(result, doca_flow_pipe_cfg_set_monitor(pipe_cfg, &monitor_count));
	IF_SUCCESS(result, doca_flow_pipe_create(pipe_cfg, &fwd, &fwd_miss, configured_pipe));

	if (pipe_cfg) {
		doca_flow_pipe_cfg_destroy(pipe_cfg);
	}

	struct doca_flow_actions actions;
	memset(&actions, 0, sizeof(actions));
	match.outer.ip4.dst_ip = BE_IPV4_ADDR(60, 0, 0, 2);
	IF_SUCCESS(result, doca_flow_pipe_add_entry(0, *configured_pipe, &match, &actions, NULL, NULL, DOCA_FLOW_NO_WAIT, NULL, configured_entry));

	return result;
}
