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
#define DATAPATH_AGING_HEURISTIC 1000

// The number of packets to process in a single burst
#define FLOW_BURST_SZ 16

struct flow_id_t {
    doca_be32_t src_ip;
    doca_be32_t dst_ip;
    doca_be16_t src_port;
    doca_be16_t dst_port;
};

DOCA_LOG_REGISTER(SELECTIVE_FWD_OFFLOAD);

__rte_always_inline
void handle_packets(struct rte_mbuf* packets[], int nb_packets, struct offload_params_t *params, struct rte_hash *entries, bool add) {
    doca_error_t result;
    struct entries_status status = {};
    doca_flow_pipe_entry *tmp_entry;
    uint32_t port_pkt_ctr[NUM_PORTS] = {0};

    for (int packet_idx = 0; packet_idx < nb_packets; packet_idx++) {
        struct rte_ether_hdr* eth_hdr = rte_pktmbuf_mtod(packets[packet_idx], struct rte_ether_hdr*);
        struct rte_ipv4_hdr* ipv4_hdr = (struct rte_ipv4_hdr*)((char*)eth_hdr + sizeof(struct rte_ether_hdr));
        struct rte_tcp_hdr* tcp_hdr = (struct rte_tcp_hdr*)((char*)ipv4_hdr + sizeof(struct rte_ipv4_hdr));

        // pmd worker does mbuf checks - not needed here outside of debug mode
        assert(eth_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4));
        assert(ipv4_hdr->next_proto_id == IPPROTO_TCP);

        struct flow_id_t flow_id = {
            .src_ip = ipv4_hdr->src_addr,
            .dst_ip = ipv4_hdr->dst_addr,
            .src_port = tcp_hdr->src_port,
            .dst_port = tcp_hdr->dst_port
        };

        if (add) {
            uint8_t port_id_in = *(RTE_MBUF_DYNFIELD(packets[packet_idx], params->mbuf_src_port_offset, uint8_t *));
            uint8_t port_id_out = port_id_in ^ 1;

            result = add_hairpin_pipe_entry(
                params->ports,
                port_id_in,
                params->app_cfg->hairpin_queues[port_id_in][port_id_out],
                params->app_cfg->hairpin_q_count,
                params->hairpin_pipes[port_id_in],
                ipv4_hdr->dst_addr,
                ipv4_hdr->src_addr,
                tcp_hdr->dst_port,
                tcp_hdr->src_port,
                params->doca_pipe_queue,
                &status,
                &tmp_entry);
            if (result != DOCA_SUCCESS) {
                DOCA_LOG_ERR("Failed to add entry: %s", doca_error_get_descr(result));
                return;
            }
            int ret = rte_hash_add_key_data(entries, &flow_id, tmp_entry);
            if (ret < 0) {
                DOCA_LOG_ERR("Failed to add entry to hash: %s", rte_strerror(-ret));
                return;
            }
        }
        else {
            int ret = rte_hash_lookup_data(entries, &flow_id, (void **)&tmp_entry);
            if (ret < 0) {
                DOCA_LOG_ERR("Failed to find entry in hash: %s", rte_strerror(-ret));
                return;
            }
            result = doca_flow_pipe_remove_entry(params->doca_pipe_queue, DOCA_FLOW_WAIT_FOR_BATCH, tmp_entry);
            if (result != DOCA_SUCCESS) {
                DOCA_LOG_ERR("Failed to remove entry: %s", doca_error_get_descr(result));
                return;
            }
            rte_hash_del_key(entries, &flow_id);
        }
    }

    for (int port_id = 0; port_id < NUM_PORTS; port_id++) {
        result = doca_flow_entries_process(params->ports[port_id], params->doca_pipe_queue, DEFAULT_TIMEOUT_US, port_pkt_ctr[port_id]);
        if (result != DOCA_SUCCESS || status.failure) {
            DOCA_LOG_ERR("Failed to process entries: %s", doca_error_get_descr(result));
            continue;
        }
    }
    if (add && status.nb_processed != nb_packets) {
        DOCA_LOG_ERR("Failed to process %d entries. Processed %d", nb_packets, status.nb_processed);
    }
}

__rte_always_inline
void handle_add_entry_ring(struct offload_params_t *params, struct rte_hash *entries) {
    struct rte_mbuf* packets[FLOW_BURST_SZ];

    uint32_t nb_packets = rte_ring_dequeue_burst(params->add_entry_ring, (void**)packets, FLOW_BURST_SZ, NULL);
    if (nb_packets == 0)
        return;

    handle_packets(packets, nb_packets, params, entries, true);
}

__rte_always_inline
void handle_remove_entry_ring(struct offload_params_t *params, struct rte_hash *entries) {
    struct rte_mbuf* packets[FLOW_BURST_SZ];
    int nb_packets;

    nb_packets = rte_ring_dequeue_burst(params->remove_entry_ring, (void**)packets, FLOW_BURST_SZ, NULL);
    if (nb_packets == 0)
        return;

    handle_packets(packets, nb_packets, params, entries, false);
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

    /*
        Thread-local structure for storing entries
    */
    std::string add_ring_name = "add_entry_ring_" + std::to_string(rte_lcore_id());
    struct rte_hash_parameters hash_params = {
        .name = add_ring_name.c_str(),
        .entries = 2097152,
        .key_len = sizeof(struct flow_id_t),
        .hash_func = rte_jhash,
        .hash_func_init_val = 0
    };
    struct rte_hash *entries = rte_hash_create(&hash_params);
    if (entries == NULL) {
        DOCA_LOG_ERR("Failed to create hash table");
        return -1;
    }

    while (1) {
        for (uint32_t i = 0; i < DATAPATH_AGING_HEURISTIC; i++) {
            handle_add_entry_ring(params, entries);
            handle_remove_entry_ring(params, entries);
        }
        handle_aging(params);
    }
}
