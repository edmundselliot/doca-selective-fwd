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
 * Start workers:
 * - pmd workers: read packets and queue offloads to the offload workers
 * - offload workers: offload entries to hardware
 *
 * Queueing to offload workers:
 * - add_entry_ring: queue to add entries
 * - remove_entry_ring: queue to remove entries
 */
doca_error_t start_workers(
    uint32_t nb_offload_workers,
    uint32_t nb_pmd_workers,
    struct application_dpdk_config* app_cfg,
    struct doca_flow_port* ports[NUM_PORTS],
    struct doca_flow_pipe* hairpin_pipes[NUM_PORTS]
)
{
    DOCA_LOG_INFO("Starting %u offload workers and %u pmd workers", nb_offload_workers, nb_pmd_workers);

    uint32_t lcore_id;
    uint32_t offload_workers_launched = 0;
    uint32_t pmd_workers_launched = 0;

    assert(nb_pmd_workers == app_cfg->port_config.nb_queues);

    std::vector<struct rte_ring*>* add_entry_rings = new std::vector<struct rte_ring*>(nb_offload_workers);
    std::vector<struct rte_ring*>* remove_entry_rings = new std::vector<struct rte_ring*>(nb_offload_workers);

    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (offload_workers_launched < nb_offload_workers) {
            DOCA_LOG_INFO("Starting offload thread on lcore %u", lcore_id);

            struct offload_params_t *offload_params = new offload_params_t;
            if (offload_params == NULL) {
                DOCA_LOG_ERR("Failed to allocate memory for offload_params");
                return DOCA_ERROR_NO_MEMORY;
            }

            std::string add_ring_name = "add_entry_ring_" + std::to_string(lcore_id);
            std::string remove_ring_name = "remove_entry_ring_" + std::to_string(lcore_id);

            offload_params->app_cfg = app_cfg;
            offload_params->ports = ports;
            offload_params->hairpin_pipes = hairpin_pipes;
            offload_params->doca_pipe_queue = offload_workers_launched;
            offload_params->add_entry_ring = rte_ring_create(add_ring_name.c_str(), 4096, rte_socket_id(), RING_F_SC_DEQ);
            offload_params->remove_entry_ring = rte_ring_create(remove_ring_name.c_str(), 4096, rte_socket_id(), RING_F_SC_DEQ);
            if (offload_params->add_entry_ring == NULL || offload_params->remove_entry_ring == NULL) {
                DOCA_LOG_ERR("Failed to create rings");
                return DOCA_ERROR_NO_MEMORY;
            }
            rte_eal_remote_launch(start_offload_thread, (void*)offload_params, lcore_id);

            (*add_entry_rings)[offload_workers_launched] = offload_params->add_entry_ring;
            (*remove_entry_rings)[offload_workers_launched] = offload_params->remove_entry_ring;
            offload_workers_launched++;
        }
        else if (pmd_workers_launched < nb_pmd_workers) {
            DOCA_LOG_INFO("Starting PMD on lcore %u", lcore_id);

            struct pmd_params_t *pmd_params = new pmd_params_t;
            if (pmd_params == NULL) {
                DOCA_LOG_ERR("Failed to allocate memory for pmd_params");
                return DOCA_ERROR_NO_MEMORY;
            }

            pmd_params->app_cfg = app_cfg;
            pmd_params->queue_id = pmd_workers_launched;
            pmd_params->add_entry_rings = add_entry_rings;
            pmd_params->remove_entry_rings = remove_entry_rings;
            rte_eal_remote_launch(start_pmd, (void*)pmd_params, lcore_id);

            pmd_workers_launched++;
        }
        else {
            DOCA_LOG_INFO("Nothing launches on lcore %u", lcore_id);
        }
    }

    assert(offload_workers_launched == nb_offload_workers);
    assert(pmd_workers_launched == nb_pmd_workers);

    return DOCA_SUCCESS;
}

/*
 * Initialize doca, doca ports, create static configuration, and then start
 * worker threads that dynamically add/remove entries
 *
 * @nb_queues [in]: number of queues the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t run_app(struct application_dpdk_config* app_cfg)
{
    struct flow_resources resource = {};
    uint32_t nr_shared_resources[SHARED_RESOURCE_NUM_VALUES] = { 0 };
    struct doca_flow_port* port_arr[NUM_PORTS];
    struct doca_flow_pipe* hairpin_pipe_arr[NUM_PORTS];
    struct doca_dev* dev_arr[NUM_PORTS];
    doca_error_t result;

    resource.nr_counters = 8000000;

    result = init_doca_flow(app_cfg->port_config.nb_queues,
                            "vnf,hws",
                            &resource,
                            nr_shared_resources);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to init DOCA Flow: %s",
                     doca_error_get_descr(result));
        goto exit;
    }

    memset(dev_arr, 0, sizeof(struct doca_dev*) * NUM_PORTS);
    result = init_doca_flow_ports(NUM_PORTS, port_arr, true, dev_arr);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to init DOCA ports: %s",
                     doca_error_get_descr(result));
        goto cleanup_port_stopped;
    }

    // STATIC CONFIGURATION
    // 	On each port
    // 	1. Add an RSS pipe and a match-all entry on the RSS pipe to forward packets to RSS
    // 	2. Add a hairpin pipe with no entries in it. The entries will be dynamically added later.
    // 		- On miss, the hairpin pipe will forward packets to the RSS pipe.
    // 		- On hit, the hairpin pipe entry will hairpin packets to the other port's tx.
    result = configure_static_pipes(app_cfg, port_arr, hairpin_pipe_arr);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to configure static pipes: %s", doca_error_get_descr(result));
        goto cleanup;
    }

    /*
        DYNAMIC CONFIGURATION
        ┌────┐    ┌────┐              ┌───┐ ┌────┐   ┌───┐
        │RXQ0┼───►│PMD0├─────┬───────►│AR0├─►OFF0┼──►│PQ0│
        ├────┤    ├────┤     │        ├───┤ └▲───┘   └───┘
        │RXQ1├───►│PMD1│     ├───────►│RR0┼──┘
        ├────┤    ├────┤     │        └───┘
        │RXQ2┼───►│PMD2│     │        ┌───┐ ┌────┐   ┌───┐
        ├────┤    ├────┤     ├───────►│AR1├─►OFF1┼──►│PQ1│
        │RXQ3├───►│PMD3│     │        ├───┤ └▲───┘   └───┘
        └────┘    └────┘     └───────►│RR1┼──┘
                                      └───┘
        Note: any PMD can reach any AR/RR. For simplicity we only show the connections for PMD0 above.

        We start any number of PMD workers and offload workers.
        - The offload workers will each have an "add ring" and a "remove ring" which contain information
            about the flows to add and remove. They will pull information off those rings, and add remove
            using a unique pipe queue for each offload worker.
        - The PMD workers will read packets and queue offloads to the offload workers. Any PMD worker can
            queue to any offload worker. To decide which offload worker to queue to, all PMDs will
            hash the flow key and use the hash to decide which offload worker to queue to.
    */
    result = start_workers(
        app_cfg->reserved_cores, // nb offload workers
        app_cfg->port_config.nb_queues, // nb pmd workers
        app_cfg, port_arr, hairpin_pipe_arr);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to start workers: %s", doca_error_get_descr(result));
        goto cleanup;
    }

    while (1) {
        DOCA_LOG_INFO("stats");
        sleep(1);
    }

cleanup:
    stop_doca_flow_ports(NUM_PORTS, port_arr);
cleanup_port_stopped:
    doca_flow_destroy();
exit:
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
    dpdk_config.port_config.nb_ports = NUM_PORTS;
    dpdk_config.port_config.nb_hairpin_q = 4; // total per-port
    dpdk_config.reserve_main_thread = true; // used for stats
    dpdk_config.port_config.self_hairpin = true;
    dpdk_config.port_config.nb_queues = 2; // N queues and N pmd workers
    dpdk_config.reserved_cores = 2; // N set of rings and N offload workers

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
        DOCA_LOG_ERR("Failed to init ARGP resources: %s", doca_error_get_descr(result));
        goto sample_exit;
    }
    doca_argp_set_dpdk_program(dpdk_init);
    result = doca_argp_start(argc, argv);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to parse sample input: %s", doca_error_get_descr(result));
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
        DOCA_LOG_ERR("run_app() encountered an error: %s", doca_error_get_descr(result));
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
    DOCA_LOG_INFO("Sample finished %s", exit_status == EXIT_SUCCESS ? "successfully" : "with errors");
    return exit_status;
}
