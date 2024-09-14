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

DOCA_LOG_REGISTER(SELECTIVE_FWD_PMD);

bool allow_offload(struct rte_mbuf* pkt)
{
    // todo: implement this function according to your firewall rules.
    return true;
}

void
handle_packets(struct rte_mbuf* packets[],
               int nb_packets,
               struct application_dpdk_config* app_cfg,
               std::vector<struct rte_ring*>* add_entry_rings,
               std::vector<struct rte_ring*>* remove_entry_rings)
{
    struct rte_ether_hdr* eth_hdr;
    struct rte_ipv4_hdr* ipv4_hdr;
    struct rte_tcp_hdr* tcp_hdr;
    struct rte_ring* relevant_ring;
    uint32_t flow_hash;
    uint32_t ring_idx;
    size_t num_rings = add_entry_rings->size();

    for (int packet_idx = 0; packet_idx < nb_packets; packet_idx++) {
        eth_hdr = rte_pktmbuf_mtod(packets[packet_idx], struct rte_ether_hdr*);
        ipv4_hdr = (struct rte_ipv4_hdr*)((char*)eth_hdr + sizeof(struct rte_ether_hdr));
        tcp_hdr = (struct rte_tcp_hdr*)((char*)ipv4_hdr + sizeof(struct rte_ipv4_hdr));

        if (eth_hdr->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4) ||
            ipv4_hdr->next_proto_id != IPPROTO_TCP) {
            DOCA_LOG_INFO("Non-IPv4 TCP packet, skipping");
            continue;
        }

        flow_hash = ipv4_hdr->src_addr ^ ipv4_hdr->dst_addr ^ tcp_hdr->src_port ^ tcp_hdr->dst_port;
        ring_idx = flow_hash % num_rings;

        if (allow_offload(packets[packet_idx])) {
            relevant_ring = (*add_entry_rings)[ring_idx];
            if (rte_ring_enqueue(relevant_ring, packets[packet_idx])) {
                DOCA_LOG_ERR("Failed to enqueue packet to ring");
            }
        }
    }
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
int start_pmd(void* pmd_params)
{
    struct pmd_params_t* params = (struct pmd_params_t*)pmd_params;
    struct rte_mbuf* packets[PACKET_BURST_SZ];
    int nb_packets;

    while (1) {
        for (int port_id_in = 0; port_id_in < NUM_PORTS; port_id_in++) {
            nb_packets = rte_eth_rx_burst(port_id_in, params->queue_id, packets, PACKET_BURST_SZ);
            if (nb_packets == 0) {
                continue;
            }
            handle_packets(packets, nb_packets, params->app_cfg, params->add_entry_rings, params->remove_entry_rings);
        }
    }
}

//     struct pmd_params_t pmd_params = *(struct pmd_params_t*)_pmd_params;
//     struct rte_mbuf* packets[PACKET_BURST_SZ];
//     struct rte_ether_hdr* eth_hdr;
//     struct rte_ipv4_hdr* ipv4_hdr;
//     struct rte_tcp_hdr* tcp_hdr;
//     doca_error_t result;
//     enum doca_flow_entry_op op;
//     struct rte_ring *relevant_op;

//     DOCA_LOG_INFO("Setup done. Starting PMD");
//     while (1) {
//         for (int port_id_in = 0; port_id_in < NUM_PORTS; port_id_in++) {
//             for (int queue_id = 0; queue_id < pmd_params.nb_queues; queue_id++) {
//                 int nb_packets = rte_eth_rx_burst(port_id_in, queue_id, packets, PACKET_BURST_SZ);
//                 if (nb_packets == 0) {
//                     continue;
//                 }

//                 for (int packet_idx = 0; packet_idx < nb_packets; packet_idx++) {

//                     eth_hdr = rte_pktmbuf_mtod(packets[packet_idx],
//                                                struct rte_ether_hdr*);
//                     ipv4_hdr =
//                         (struct rte_ipv4_hdr*)((char*)eth_hdr +
//                                                sizeof(struct rte_ether_hdr));
//                     tcp_hdr =
//                         (struct rte_tcp_hdr*)((char*)ipv4_hdr +
//                                               sizeof(struct rte_ipv4_hdr));

//                     if (eth_hdr->ether_type !=
//                             rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4) ||
//                         ipv4_hdr->next_proto_id != IPPROTO_TCP) {
//                         DOCA_LOG_INFO("Non-IPv4 TCP packet, skipping");
//                         continue;
//                     }

// #ifdef DEBUG
//                     char src_addr[INET_ADDRSTRLEN];
//                     char dst_addr[INET_ADDRSTRLEN];
//                     inet_ntop(AF_INET,
//                               &ipv4_hdr->src_addr,
//                               src_addr,
//                               sizeof(src_addr));
//                     inet_ntop(AF_INET,
//                               &ipv4_hdr->dst_addr,
//                               dst_addr,
//                               sizeof(dst_addr));

//                     printf("[P%d Q%d] Queue %s:%d <-> %s:%d? (s/p/n): ",
//                            port_id_in,
//                            queue_id,
//                            src_addr,
//                            rte_be_to_cpu_16(tcp_hdr->src_port),
//                            dst_addr,
//                            rte_be_to_cpu_16(tcp_hdr->dst_port));
// #endif
//                     if (tcp_hdr->tcp_flags & RTE_TCP_SYN_FLAG) {
//                         relevant_op = pmd_params.add_entry_ring;
//                     }
//                     else if (tcp_hdr->tcp_flags & (RTE_TCP_FIN_FLAG | RTE_TCP_RST_FLAG)) {
//                         relevant_op = pmd_params.remove_entry_ring;
//                     }
//                     else {
//                         continue;
//                     }
//                 }
//             }
//         }
//     }
// }
