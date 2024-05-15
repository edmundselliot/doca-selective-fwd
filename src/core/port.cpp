#include "main.h"

const char *
flow_op_to_str(enum doca_flow_entry_op op)
{
	switch (op) {
	case DOCA_FLOW_ENTRY_OP_ADD:
		return "DOCA_FLOW_ENTRY_OP_ADD";
	case DOCA_FLOW_ENTRY_OP_DEL:
		return "DOCA_FLOW_ENTRY_OP_DEL";
	case DOCA_FLOW_ENTRY_OP_UPD:
		return "DOCA_FLOW_ENTRY_OP_UPD";
	case DOCA_FLOW_ENTRY_OP_AGED:
		return "DOCA_FLOW_ENTRY_OP_AGED";
	default:
		return "UNKNOWN";
	}
}

static struct doca_flow_port *
port_init(struct port_ctx *port_ctx)
{
	doca_error_t res;

	std::string port_id_str = std::to_string(port_ctx->port_id);

    struct doca_flow_port_cfg *port_cfg;
    doca_flow_port_cfg_create(&port_cfg);
    doca_flow_port_cfg_set_devargs(port_cfg, port_id_str.c_str());
	doca_flow_port_cfg_set_dev(port_cfg, &port_ctx->dev);
	res = doca_flow_port_start(port_cfg, &port_ctx->port);

	// struct doca_flow_port_cfg port_cfg = {
	// 	.port_id = port_ctx->port_id,
	// 	.type = DOCA_FLOW_PORT_DPDK_BY_ID,
	// 	.devargs = port_id_str,
	// 	.dev = port_ctx->dev,
	// };

	if (res != DOCA_SUCCESS) {
		rte_exit(EXIT_FAILURE, "failed to initialize doca flow port: %d\n", res);
	}

	rte_eth_macaddr_get(port_ctx->port_id, &port_ctx->port_mac);

	DOCA_LOG_INFO("Started port %d: %02x:%02x:%02x:%02x:%02x:%02x",
		port_ctx->port_id,
		port_ctx->port_mac.addr_bytes[0],
		port_ctx->port_mac.addr_bytes[1],
		port_ctx->port_mac.addr_bytes[2],
		port_ctx->port_mac.addr_bytes[3],
		port_ctx->port_mac.addr_bytes[4],
		port_ctx->port_mac.addr_bytes[5]);

	return port_ctx->port;
}

/*
 * Entry processing callback
 *
 * @entry [in]: entry pointer
 * @pipe_queue [in]: queue identifier
 * @status [in]: DOCA Flow entry status
 * @op [in]: DOCA Flow entry operation
 * @user_ctx [out]: user context
 */
static void
check_for_valid_entry(struct doca_flow_pipe_entry *entry, uint16_t pipe_queue,
		      enum doca_flow_entry_status status, enum doca_flow_entry_op op, void *user_ctx)
{
	(void)entry;

	struct entries_status *entry_status = (struct entries_status *)user_ctx;

	if (entry_status == NULL)
		return;

	if (status == DOCA_FLOW_ENTRY_STATUS_SUCCESS) {
		DOCA_LOG_DBG("%s: op = %s, status = SUCCESS", __FUNCTION__, flow_op_to_str(op));
	}
	else {
		DOCA_LOG_WARN("%s: op = %s, status = %d, wanted %d", __FUNCTION__, flow_op_to_str(op), status, DOCA_FLOW_ENTRY_STATUS_SUCCESS);
		entry_status->failure = true; /* set failure to true if processing failed */
	}

	entry_status->nb_processed++;
	entry_status->entries_in_queue--;
}

int
flow_init(struct app_ctx *config)
{
	struct doca_flow_cfg *flow_cfg;
    doca_flow_cfg_create(&flow_cfg);
    doca_flow_cfg_set_pipe_queues(flow_cfg, config->dpdk_config.port_config.nb_queues);
    doca_flow_cfg_set_nr_counters(flow_cfg, 1024);
    doca_flow_cfg_set_mode_args(flow_cfg, "vnf,hws,hairpinq_num=4");
	doca_flow_cfg_set_cb_entry_process(flow_cfg, check_for_valid_entry);

	doca_error_t res = doca_flow_init(flow_cfg);
	if (res != DOCA_SUCCESS) {
		rte_exit(EXIT_FAILURE, "Failed to init DOCA Flow: %d\n", res);
	}
	DOCA_LOG_DBG("DOCA flow init done");

	port_init(&config->p0_ctx);

	// TODO once we add p1, put it here
	DOCA_LOG_DBG("DOCA flow port init done");

	return 0;
}

doca_error_t
open_doca_device_with_pci(const char *pci_addr, struct doca_dev **retval)
{
	struct doca_devinfo **dev_list;
	uint32_t nb_devs;
	uint8_t is_addr_equal = 0;
	doca_error_t res;
	size_t i;

	/* Set default return value */
	*retval = NULL;

	res = doca_devinfo_create_list(&dev_list, &nb_devs);
	if (res != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to load doca devices list. Doca_error value: %d", res);
		return res;
	}

	/* Search */
	for (i = 0; i < nb_devs; i++) {
		res = doca_devinfo_is_equal_pci_addr(dev_list[i], pci_addr, &is_addr_equal);
		if (res == DOCA_SUCCESS && is_addr_equal) {

			/* if device can be opened */
			res = doca_dev_open(dev_list[i], retval);
			if (res == DOCA_SUCCESS) {
				doca_devinfo_destroy_list(dev_list);
				return res;
			}
		}
	}

	DOCA_LOG_WARN("Matching device not found");
	res = DOCA_ERROR_NOT_FOUND;

	doca_devinfo_destroy_list(dev_list);
	return res;
}