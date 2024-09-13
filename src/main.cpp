/*
 * Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of NVIDIA CORPORATION &
 * AFFILIATES (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 *
 */

#include "selective_fwd.h"

DOCA_LOG_REGISTER(SELECTIVE_FWD);

/*
 * Initialize doca, doca ports, create static configuration, and then start a
 * PMD
 *
 * @nb_queues [in]: number of queues the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
run_app(struct application_dpdk_config* app_cfg)
{
    struct flow_resources resource = {};
    uint32_t nr_shared_resources[SHARED_RESOURCE_NUM_VALUES] = { 0 };
    struct doca_flow_port* ports[NUM_PORTS];
    struct doca_flow_pipe* hairpin_pipes[NUM_PORTS];
    struct doca_dev* dev_arr[NUM_PORTS];
    doca_error_t result;

    resource.nr_counters = MAX_FLOWS_PER_PORT * NUM_PORTS;

    result = init_doca_flow(app_cfg->port_config.nb_queues,
                            "vnf,hws",
                            &resource,
                            nr_shared_resources);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to init DOCA Flow: %s",
                     doca_error_get_descr(result));
        return result;
    }

    memset(dev_arr, 0, sizeof(struct doca_dev*) * NUM_PORTS);
    result = init_doca_flow_ports(NUM_PORTS, ports, true, dev_arr);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to init DOCA ports: %s",
                     doca_error_get_descr(result));
        doca_flow_destroy();
        return result;
    }

    // STATIC CONFIGURATION
    // 	On each port
    // 	1. Add an RSS pipe and a match-all entry on the RSS pipe to forward
    // packets to RSS
    // 	2. Add a hairpin pipe with no entries in it. The entries will be
    // dynamically added later.
    // 		- On miss, the hairpin pipe will forward packets to the RSS
    // pipe.
    // 		- On hit, the hairpin pipe entry will hairpin packets to the
    // other port's tx.
    result = configure_static_pipes(app_cfg, ports, hairpin_pipes);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to configure static pipes: %s",
                     doca_error_get_descr(result));
        stop_doca_flow_ports(NUM_PORTS, ports);
        doca_flow_destroy();
        return result;
    }

    // DYNAMIC CONFIGURATION
    struct rte_ring* add_entry_ring = rte_ring_create("add_entry_ring",
                                                    4096,
                                                    rte_socket_id(),
                                                    RING_F_SC_DEQ);
    struct rte_ring* remove_entry_ring = rte_ring_create("remove_entry_ring",
                                                       4096,
                                                       rte_socket_id(),
                                                       RING_F_SC_DEQ);
    if (add_entry_ring == NULL || remove_entry_ring == NULL) {
        DOCA_LOG_ERR("Failed to create rings");
        return DOCA_ERROR_NO_MEMORY;
    }

    struct offload_params_t offload_params;
    offload_params.app_cfg = app_cfg;
    // offload_params.ports = ports;
    // offload_params.hairpin_pipes = hairpin_pipes;
    offload_params.add_entry_ring = add_entry_ring;
    offload_params.remove_entry_ring = remove_entry_ring;

    uint32_t lcore_id;
    uint32_t offload_threads_launched = 0;
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (offload_threads_launched < 1) {
            //  Start an offload thread which will offload the entries to hardware
            DOCA_LOG_INFO("Starting offload thread on lcore %u", lcore_id);
            rte_eal_remote_launch(start_offload_thread, (void*)&offload_params, lcore_id);
            offload_threads_launched++;
        } else {
            //  Start a PMD which will read packets and queue offloads to the offload thread
            DOCA_LOG_INFO("Starting PMD on lcore %u", lcore_id);
        }
    }

    // start_pmd(app_cfg, ports, hairpin_pipes, app_cfg->port_config.nb_queues, offload_ring);

    while (1) {
        DOCA_LOG_INFO("stats");
        sleep(1);
    }

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
main(int argc, char** argv)
{
    doca_error_t result;
    struct doca_log_backend* sdk_log;
    int exit_status = EXIT_FAILURE;
    struct application_dpdk_config dpdk_config;
    dpdk_config.port_config.nb_ports = 2;
    dpdk_config.port_config.nb_queues = 2;
    dpdk_config.port_config.nb_hairpin_q = 4; // total per-port
    dpdk_config.port_config.self_hairpin = true;

    /* Register a logger backend */
    result = doca_log_backend_create_standard();
    if (result != DOCA_SUCCESS)
        goto sample_exit;

    /* Register a logger backend for internal SDK errors and warnings */
    result = doca_log_backend_create_with_file_sdk(stderr, &sdk_log);
    if (result != DOCA_SUCCESS)
        goto sample_exit;
    result = doca_log_backend_set_sdk_level(sdk_log, DOCA_LOG_LEVEL_WARNING);
    if (result != DOCA_SUCCESS)
        goto sample_exit;

    DOCA_LOG_INFO("Starting the sample");

    result = doca_argp_init("doca_selective_fwd", NULL);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to init ARGP resources: %s",
                     doca_error_get_descr(result));
        goto sample_exit;
    }
    doca_argp_set_dpdk_program(dpdk_init);
    result = doca_argp_start(argc, argv);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to parse sample input: %s",
                     doca_error_get_descr(result));
        goto argp_cleanup;
    }

    /* update queues and ports */
    result = dpdk_queues_and_ports_init(&dpdk_config);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to update ports and queues");
        goto dpdk_cleanup;
    }

    /* configure static pipes, then run "pmd" */
    result = run_app(&dpdk_config);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("run_app() encountered an error: %s",
                     doca_error_get_descr(result));
        goto dpdk_ports_queues_cleanup;
    }

    exit_status = EXIT_SUCCESS;

dpdk_ports_queues_cleanup:
    dpdk_queues_and_ports_fini(&dpdk_config);
dpdk_cleanup:
    dpdk_fini();
argp_cleanup:
    doca_argp_destroy();
sample_exit:
    if (exit_status == EXIT_SUCCESS)
        DOCA_LOG_INFO("Sample finished successfully");
    else
        DOCA_LOG_INFO("Sample finished with errors");
    return exit_status;
}
