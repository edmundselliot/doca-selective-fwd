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

DOCA_LOG_REGISTER(SELECTIVE_FWD_OFFLOAD);

int start_offload_thread(void *offload_params)
{
    struct offload_params_t *params = (struct offload_params_t *)offload_params;
    struct rte_mbuf* packets[PACKET_BURST_SZ];
    int nb_packets;

    while (1) {
        nb_packets = rte_ring_dequeue_burst(params->add_entry_ring, (void**)packets, PACKET_BURST_SZ, NULL);
        if (nb_packets == 0) {
            continue;
        }

        for (int i = 0; i < nb_packets; i++) {
            DOCA_LOG_INFO("Packet %d:\n", i);
            // rte_pktmbuf_dump(stdout, packets[i], packets[i]->pkt_len);
        }
    }
}
