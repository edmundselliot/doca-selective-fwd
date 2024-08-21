/*
 * Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

DOCA_LOG_REGISTER(SELECTIVE_FWD::PMD);

/*
 * Aging loop - garbage collector for stale flows
 *
 * @arg [in]: array of ports
 */
void*
aging_loop(void* arg)
{
    struct doca_flow_port** ports = (struct doca_flow_port**)arg;
    int nb_flows_removed = 0;
    doca_error_t result;

    while (1) {
        for (int port_id = 0; port_id < NUM_PORTS; port_id++) {
            nb_flows_removed =
                doca_flow_aging_handle(ports[port_id], 0, 1000 /*1 ms*/, 0);
            if (nb_flows_removed > 0) {
                result = doca_flow_entries_process(
                    ports[port_id], 0, DEFAULT_TIMEOUT_US, 0);
                if (result != DOCA_SUCCESS) {
                    DOCA_LOG_WARN("Failed to remove stale pipe entries: %s",
                                  doca_error_get_descr(result));
                }
            }
        }
        sleep(AGING_HANDLE_INTERVAL_SEC);
    }

    return NULL;
}

/*
 * Start the PMD
 *   In a real application, a user would start a PMD here listening on each
 * queue. For the sake of a simple demo, we instead query every packet received
 * on each queue and ask the user if they want to offload the flow to hardware.
 *
 * @ports [in]: array of ports
 * @hairpin_pipes [in]: array of hairpin pipes
 * @nb_queues [in]: number of queues
 */
void
start_pmd(struct application_dpdk_config* app_cfg,
          struct doca_flow_port* ports[NUM_PORTS],
          struct doca_flow_pipe* hairpin_pipes[NUM_PORTS],
          uint32_t nb_queues)
{
    doca_error_t result;

    // if (pthread_create(&aging_thread_id, NULL, aging_loop, (void*)ports) != 0) {
    //     DOCA_LOG_ERR("Failed to create aging thread");
    //     return;
    // }

    DOCA_LOG_INFO("Setup done. Starting PMD");
    uint32_t dummy = 0xdeadbeef;
    uint32_t max = pow(2, 23) - 10;
    for (uint32_t i = 0; i < max; i++) {
        result = add_hairpin_pipe_entry(
            ports,
            0,
            app_cfg->hairpin_queues[0][0],
            app_cfg->hairpin_q_count,
            hairpin_pipes[0],
            dummy,
            dummy,
            dummy,
            dummy);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR(
                "Failed to add main hairpin pipe entry %d: %s",
                i,
                doca_error_get_descr(result));
            continue;
        }
        if (i % 10000 == 0) {
            DOCA_LOG_INFO("Added entry %d", i);
        }
    }
}
