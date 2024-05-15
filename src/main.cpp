
#include "main.h"

struct app_ctx app_cfg = {0};

//
// argv requirements:
// 1. Must have -a option with a PCI address in the format -a00:03.0
// 2. Must enable hardware steering with "dv_flow_en=2,dv_xmeta_en=4"
//
doca_error_t
argv_cleanup(int argc, char* argv[], std::string& pci_addr) {
	doca_error_t result = DOCA_SUCCESS;
	std::regex e_pci ("-a([0-9a-fA-F]{2}:[0-9a-fA-F]{2}.[0-9a-fA-F])");
	bool pci_found = false;
	std::regex e_dv_flow_en ("dv_flow_en=2");
	bool dv_flow_en_found = false;
	std::regex e_dv_xmeta_en ("dv_xmeta_en=4");
	bool dv_xmeta_en_found = false;


	// Rewrite the PCI address to 00:00.0
	std::smatch match;
	for(int i = 0; i < argc; i++) {
		std::string arg = std::string(argv[i]);
		if (std::regex_search(arg, match, e_pci) && match.size() > 1) {
			pci_addr = match.str(1);
			pci_found = true;
			arg = std::regex_replace(arg, e_pci, "-a00:00.0");
			argv[i] = strdup(arg.c_str());
			break;
		}
	}

	// Validate that dv_flow_en and dv_xmeta_en are set
	for(int i = 0; i < argc; i++) {
		std::string arg = std::string(argv[i]);
		if (std::regex_search(arg, match, e_dv_flow_en)) {
			dv_flow_en_found = true;
		}
		if (std::regex_search(arg, match, e_dv_xmeta_en)) {
			dv_xmeta_en_found = true;
		}
	}

	if (!pci_found || !dv_flow_en_found || !dv_xmeta_en_found) {
		result = DOCA_ERROR_NOT_FOUND;
		goto exit_err;
	}
	return result;

exit_err:
	DOCA_LOG_CRIT("Failed to parse argv: %s", doca_error_get_descr(result));
	return result;
}

/*
 * Sample main function
 *
 * @argc [in]: command line arguments size
 * @argv [in]: array of command line arguments
 * @return: EXIT_SUCCESS on success and EXIT_FAILURE otherwise
 */
int
main(int argc, char **argv)
{
    doca_error_t result;
	struct doca_log_backend *sdk_log;
	std::string pci_addr;

	result = argv_cleanup(argc, argv, pci_addr);
	if (result != DOCA_SUCCESS)
		goto exit;

	/* Register a logger backend */
	result = doca_log_backend_create_standard();
	if (result != DOCA_SUCCESS)
		goto exit;

	//
	// Register a logger backend for internal SDK errors and warnings
	//
	result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
	if (result != DOCA_SUCCESS)
		goto exit;
	result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);
	if (result != DOCA_SUCCESS)
		goto exit;

	//
	// Register the command line arguments
	//
	result = doca_argp_init("doca-router", &app_cfg);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
		goto exit;
	}
	doca_argp_set_dpdk_program(dpdk_init);
	// Initialize DPDK, but argv have PCI 0, so device is hidden from DPDK here
	result = doca_argp_start(argc, argv);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to parse input: %s", doca_error_get_descr(result));
		goto exit;
	}

	result = open_doca_device_with_pci(pci_addr.c_str(), &app_cfg.p0_ctx.dev);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to open doca device: %s", doca_error_get_descr(result));
		goto exit;
	}

	//
	// Initialize DPDK
	//
	result = doca_dpdk_port_probe(app_cfg.p0_ctx.dev, "dv_flow_en=2,dv_xmeta_en=4");
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to probe port: %s", doca_error_get_descr(result));
		goto exit;
	}

	app_cfg.dpdk_config.port_config.nb_ports = rte_eth_dev_count_avail();
	app_cfg.dpdk_config.port_config.nb_hairpin_q = rte_eth_dev_count_avail();
	app_cfg.dpdk_config.port_config.nb_queues = rte_lcore_count();
	app_cfg.dpdk_config.port_config.self_hairpin = true;
	app_cfg.dpdk_config.reserve_main_thread = true;
	result = dpdk_queues_and_ports_init(&app_cfg.dpdk_config);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to update ports and queues");
		goto exit;
	}

	//
	// Initialize DOCA & DOCA ports
	//
	flow_init(&app_cfg);


	//
	// App is set up at this point. Monitor stats and wait for exit signal
	//
    std::this_thread::sleep_for(std::chrono::seconds(5));

exit:
	DOCA_LOG_INFO("Sample exiting...");
	doca_flow_destroy();
	doca_argp_destroy();
	if (result != DOCA_SUCCESS)
		DOCA_LOG_CRIT("Router exited with error: %s", doca_error_get_descr(result));
    return result == DOCA_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}