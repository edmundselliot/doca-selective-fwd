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
void *aging_loop(void *arg) {
  struct doca_flow_port **ports = (struct doca_flow_port **)arg;
  int nb_flows_removed = 0;
  doca_error_t result;

  while (1) {
    for (int port_id = 0; port_id < NUM_PORTS; port_id++) {
      nb_flows_removed =
          doca_flow_aging_handle(ports[port_id], 0, 1000 /*1 ms*/, 0);
      if (nb_flows_removed > 0) {
        result =
            doca_flow_entries_process(ports[port_id], 0, DEFAULT_TIMEOUT_US, 0);
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
void start_pmd(struct application_dpdk_config *app_cfg,
               struct doca_flow_port *ports[NUM_PORTS],
               struct doca_flow_pipe *hairpin_pipes[NUM_PORTS],
               uint32_t nb_queues) {
  struct rte_mbuf *packets[PACKET_BURST_SZ];
  struct rte_ether_hdr *eth_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  struct rte_tcp_hdr *tcp_hdr;
  char src_addr[INET_ADDRSTRLEN];
  char dst_addr[INET_ADDRSTRLEN];
  doca_error_t result;
  pthread_t aging_thread_id;

  if (pthread_create(&aging_thread_id, NULL, aging_loop, (void *)ports) != 0) {
    DOCA_LOG_ERR("Failed to create aging thread");
    return;
  }

  DOCA_LOG_INFO("Setup done. Starting PMD");
  while (1) {
    for (int port_id_in = 0; port_id_in < NUM_PORTS; port_id_in++) {
      for (int queue_id = 0; queue_id < nb_queues; queue_id++) {
        int nb_packets =
            rte_eth_rx_burst(port_id_in, queue_id, packets, PACKET_BURST_SZ);
        if (nb_packets == 0) {
          continue;
        }

        for (int packet_idx = 0; packet_idx < nb_packets; packet_idx++) {
          eth_hdr =
              rte_pktmbuf_mtod(packets[packet_idx], struct rte_ether_hdr *);
          ipv4_hdr = (struct rte_ipv4_hdr *)((char *)eth_hdr +
                                             sizeof(struct rte_ether_hdr));
          tcp_hdr = (struct rte_tcp_hdr *)((char *)ipv4_hdr +
                                           sizeof(struct rte_ipv4_hdr));

          if (eth_hdr->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4) ||
              ipv4_hdr->next_proto_id != IPPROTO_TCP) {
            DOCA_LOG_INFO("Non-IPv4 TCP packet, skipping");
            continue;
          }

          inet_ntop(AF_INET, &ipv4_hdr->src_addr, src_addr, sizeof(src_addr));
          inet_ntop(AF_INET, &ipv4_hdr->dst_addr, dst_addr, sizeof(dst_addr));

          printf("[P%d Q%d] Allow %s:%d <-> %s:%d? (s/p/n): ", port_id_in,
                 queue_id, src_addr, rte_be_to_cpu_16(tcp_hdr->src_port),
                 dst_addr, rte_be_to_cpu_16(tcp_hdr->dst_port));
          uint16_t dest_port;
          unsigned char c;
          scanf(" %c", &c);
          if (c == 's') {
            dest_port = port_id_in;
          } else if (c == 'p') {
            dest_port = port_id_in ^ 1;
          } else {
            DOCA_LOG_INFO("Packet dropped");
            continue;
          }

          // Offload the flow to hardware
          result = add_hairpin_pipe_entry(
              ports, port_id_in, app_cfg->hairpin_queues[port_id_in][dest_port],
              app_cfg->hairpin_q_count, hairpin_pipes[port_id_in],
              ipv4_hdr->dst_addr, ipv4_hdr->src_addr, tcp_hdr->dst_port,
              tcp_hdr->src_port);
          if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to add main hairpin pipe entry: %s",
                         doca_error_get_descr(result));
            continue;
          }

          rte_eth_tx_burst(dest_port, queue_id, &packets[packet_idx], 1);
          DOCA_LOG_INFO("Packet forwarded and flow offloaded");
        }
      }
    }
  }
}
