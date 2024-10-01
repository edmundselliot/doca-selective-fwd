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

PipeMgr pipe_mgr = PipeMgr();

bool allow_offload(struct rte_mbuf* pkt)
{
    // todo: implement this function according to your firewall rules.
    return true;
}

std::string create_entry_name(const rte_ipv4_hdr* ipv4_hdr, const rte_tcp_hdr* tcp_hdr) {
    char src_ip_str[INET_ADDRSTRLEN];
    char dst_ip_str[INET_ADDRSTRLEN];

    // Convert IP addresses to strings
    inet_ntop(AF_INET, &ipv4_hdr->src_addr, src_ip_str, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &ipv4_hdr->dst_addr, dst_ip_str, INET_ADDRSTRLEN);

    std::ostringstream oss;
    oss << "hairpin_"
        << src_ip_str << ':' << ntohs(tcp_hdr->src_port)
        << "->"
        << dst_ip_str << ':' << ntohs(tcp_hdr->dst_port);
    return oss.str();
}

void
handle_packets(struct rte_mbuf* packets[],
               int nb_packets,
               int port_id_in,
               struct pmd_params_t* params)
{
    struct rte_ether_hdr* eth_hdr;
    struct rte_ipv4_hdr* ipv4_hdr;
    struct rte_tcp_hdr* tcp_hdr;

    for (int packet_idx = 0; packet_idx < nb_packets; packet_idx++) {
        eth_hdr = rte_pktmbuf_mtod(packets[packet_idx], struct rte_ether_hdr*);
        ipv4_hdr = (struct rte_ipv4_hdr*)((char*)eth_hdr + sizeof(struct rte_ether_hdr));
        tcp_hdr = (struct rte_tcp_hdr*)((char*)ipv4_hdr + sizeof(struct rte_ipv4_hdr));

        if (eth_hdr->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4) ||
            ipv4_hdr->next_proto_id != IPPROTO_TCP) {
            DOCA_LOG_INFO("Non-IPv4 TCP packet, skipping");
            continue;
        }

        if (allow_offload(packets[packet_idx])) {
            struct doca_flow_pipe_entry *entry;
            struct entries_status status = {};

            doca_error_t result = add_hairpin_pipe_entry(
                params->ports,
                port_id_in,
                params->app_cfg->hairpin_queues[port_id_in][port_id_in^1],
                params->app_cfg->hairpin_q_count,
                params->hairpin_pipes[port_id_in],
                ipv4_hdr->dst_addr,
                ipv4_hdr->src_addr,
                tcp_hdr->dst_port,
                tcp_hdr->src_port,
                params->queue_id,
                &status,
                &entry
            );
            if (result != DOCA_SUCCESS) {
                DOCA_LOG_ERR("Failed to add entry: %s", doca_error_get_descr(result));
                return;
            }
            std::string entry_name = "hairpin" + tcp_hdr->dst_port + '_';
            pipe_mgr.add_entry(create_entry_name(ipv4_hdr, tcp_hdr), entry);

            int nb_sent = rte_eth_tx_burst(port_id_in^1, 0, &packets[packet_idx], 1);
            if (nb_sent != 1) {
                DOCA_LOG_ERR("Failed to send packet");
            }
        }
    }
}

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
            handle_packets(packets, nb_packets, port_id_in, params);
        }
    }
}

void print_stats() {
    pipe_mgr.print_stats();
}
