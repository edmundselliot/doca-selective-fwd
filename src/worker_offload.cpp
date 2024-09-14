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

// The number of times an offload thread will handle stale flows before taking a break to handle aging
// Higher -> less granular aging, less frequent datapath interruptions
// Lower -> more granular aging, more frequent datapath interruptions
//
// Maximum datapath interruption length is at most DEFAULT_TIMEOUT_US
#define DATAPATH_AGING_HEURISTIC 100000

DOCA_LOG_REGISTER(SELECTIVE_FWD_OFFLOAD);

__rte_always_inline
void handle_add_entry_ring(struct offload_params_t *params, std::vector<struct doca_flow_pipe_entry*> &entries) {
    struct rte_mbuf* packets[PACKET_BURST_SZ];
    doca_error_t result;
    struct entries_status status = {};

    int nb_packets = 1024;
    entries.resize(nb_packets);
    std::fill(entries.begin(), entries.begin() + nb_packets, nullptr);
    // nb_packets = rte_ring_dequeue_burst(params->add_entry_ring, (void**)packets, PACKET_BURST_SZ, NULL);
    // if (nb_packets == 0)
    //     return;

    uint32_t fake_addr = 0x0a0b0c01;
    for (int packet_idx = 0; packet_idx < nb_packets; packet_idx++) {
        // rte_pktmbuf_dump(stdout, packets[i], packets[i]->pkt_len);
        // eth_hdr = rte_pktmbuf_mtod(packets[packet_idx], struct rte_ether_hdr*);
        // ipv4_hdr = (struct rte_ipv4_hdr*)((char*)eth_hdr + sizeof(struct rte_ether_hdr));
        // tcp_hdr = (struct rte_tcp_hdr*)((char*)ipv4_hdr + sizeof(struct rte_ipv4_hdr));

        // doca_add_entry here
        result = add_hairpin_pipe_entry(
            params->ports,
            0,
            params->app_cfg->hairpin_queues[0][1],
            params->app_cfg->hairpin_q_count,
            params->hairpin_pipes[0],
            fake_addr,
            fake_addr++,
            1 + params->doca_pipe_queue,
            80 + packet_idx,
            params->doca_pipe_queue,
            &status,
            &entries[packet_idx]);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to add entry: %s", doca_error_get_descr(result));
            return;
        }
    }

    result = doca_flow_entries_process(params->ports[0], params->doca_pipe_queue, DEFAULT_TIMEOUT_US, nb_packets);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to process entries: %s", doca_error_get_descr(result));
        return;
    }

    if (status.nb_processed != nb_packets || status.failure) {
        DOCA_LOG_ERR("Failed to process %d entries, processed %d instead", nb_packets, status.nb_processed);
        return;
    }
    DOCA_LOG_DBG("Processed %d entries", status.nb_processed);
}

__rte_always_inline
void handle_remove_entry_ring(struct offload_params_t *params, std::vector<struct doca_flow_pipe_entry*> &entries) {
    struct rte_mbuf* packets[PACKET_BURST_SZ];
    int nb_packets;

    // nb_packets = rte_ring_dequeue_burst(params->remove_entry_ring, (void**)packets, PACKET_BURST_SZ, NULL);
    // if (nb_packets == 0)
        // return;

    for (auto entry : entries) {
        doca_flow_pipe_remove_entry(params->doca_pipe_queue, DOCA_FLOW_WAIT_FOR_BATCH, entry);
    }

    doca_error_t result = doca_flow_entries_process(params->ports[0], params->doca_pipe_queue, DEFAULT_TIMEOUT_US, entries.size());
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to process entries: %s", doca_error_get_descr(result));
        return;
    }
    entries.clear();
}

__rte_always_inline
void handle_aging(struct offload_params_t *params) {
    int nb_flows_removed;
    doca_error_t result;

    for (int port_id = 0; port_id < NUM_PORTS; port_id++) {
        nb_flows_removed = doca_flow_aging_handle(params->ports[port_id], params->doca_pipe_queue, 1000 /*1 ms*/, 0);
        if (nb_flows_removed > 0) {
            result = doca_flow_entries_process(params->ports[port_id], params->doca_pipe_queue, DEFAULT_TIMEOUT_US, 0);
            if (result != DOCA_SUCCESS) {
                DOCA_LOG_WARN("Failed to remove stale pipe entries: %s", doca_error_get_descr(result));
            }
            DOCA_LOG_INFO("Removed %d stale flows from port %d", nb_flows_removed, port_id);
        }
    }
}

int start_offload_thread(void *offload_params)
{
    struct offload_params_t *params = (struct offload_params_t *)offload_params;

    std::vector<struct doca_flow_pipe_entry*> entries;

    while (1) {
        // each iter

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 1024; i++) {
            handle_add_entry_ring(params, entries);
            handle_remove_entry_ring(params, entries);
        }
        // handle_aging(params);
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;

        // Print the elapsed time
        DOCA_LOG_INFO("1M flows add + remove time: %f seconds", elapsed.count());
    }
}
