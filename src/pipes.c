/*
 * Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#include <string.h>
#include <unistd.h>

#include <rte_byteorder.h>
#include <rte_ethdev.h>

#include <doca_log.h>
#include <doca_flow.h>

#include "flow_common.h"
#include <arpa/inet.h>

#define PACKET_BURST 256

DOCA_LOG_REGISTER(FLOW_HAIRPIN_VNF);

static doca_error_t create_rss_pipe(struct doca_flow_port *port,
				    struct doca_flow_pipe **pipe,
                    uint16_t nb_queues)
{
	struct doca_flow_match match;
	struct doca_flow_pipe_cfg *cfg;
	struct doca_flow_fwd fwd;
	doca_error_t result;
    uint16_t rss_queues[256];
    struct entries_status status;

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));
    memset(&status, 0, sizeof(status));

    for (uint16_t i = 0; i < nb_queues; i++)
	  rss_queues[i] = i;

	/* RSS queue - send matched traffic to all the configured queues  */
	fwd.type = DOCA_FLOW_FWD_RSS;
	fwd.rss_queues = rss_queues;
	fwd.rss_outer_flags = DOCA_FLOW_RSS_IPV4 | DOCA_FLOW_RSS_TCP;
	fwd.num_of_queues = nb_queues;

	result = doca_flow_pipe_cfg_create(&cfg, port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = set_flow_pipe_cfg(cfg, "RSS_PIPE", DOCA_FLOW_PIPE_BASIC, false);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_match(cfg, &match, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg match: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}

	result = doca_flow_pipe_create(cfg, &fwd, NULL, pipe);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create RSS pipe: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	doca_flow_pipe_cfg_destroy(cfg);

	/* Match on any packet */
	result = doca_flow_pipe_add_entry(0, *pipe, &match, NULL, NULL, &fwd, 0, &status, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to add RSS pipe entry: %s", doca_error_get_descr(result));
		return result;
	}

	result = doca_flow_entries_process(port, 0, DEFAULT_TIMEOUT_US, 0);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to process RSS entry: %s", doca_error_get_descr(result));
        return result;
    }

    if (status.nb_processed != 1 || status.failure) {
        DOCA_LOG_ERR("Failed to process RSS entry");
        return DOCA_ERROR_BAD_STATE;
    }

	return result;

destroy_pipe_cfg:
	doca_flow_pipe_cfg_destroy(cfg);
	return result;
}

/*
 * Create DOCA Flow pipe with 5 tuple match that forwards the matched traffic to the other port
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t create_hairpin_pipe(
    struct doca_flow_port *port,
    int port_id,
    struct doca_flow_pipe *pipe_fwd_miss,
    struct doca_flow_pipe **pipe)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_fwd fwd, fwd_miss;
	struct doca_flow_pipe_cfg *pipe_cfg;
	doca_error_t result;

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&fwd_miss, 0, sizeof(fwd_miss));

	/* 5 tuple match */
	match.parser_meta.outer_l4_type = DOCA_FLOW_L4_META_TCP;
	match.parser_meta.outer_l3_type = DOCA_FLOW_L3_META_IPV4;
	match.outer.l4_type_ext = DOCA_FLOW_L4_TYPE_EXT_TCP;
	match.outer.l3_type = DOCA_FLOW_L3_TYPE_IP4;
	match.outer.ip4.src_ip = 0xffffffff;
	match.outer.ip4.dst_ip = 0xffffffff;
	match.outer.tcp.l4_port.src_port = 0xffff;
	match.outer.tcp.l4_port.dst_port = 0xffff;

	actions_arr[0] = &actions;

	result = doca_flow_pipe_cfg_create(&pipe_cfg, port);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		return result;
	}

	result = set_flow_pipe_cfg(pipe_cfg, "HAIRPIN_PIPE", DOCA_FLOW_PIPE_BASIC, true);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_match(pipe_cfg, &match, NULL);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg match: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}
	result = doca_flow_pipe_cfg_set_actions(pipe_cfg, actions_arr, NULL, NULL, NB_ACTIONS_ARR);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg actions: %s", doca_error_get_descr(result));
		goto destroy_pipe_cfg;
	}

	/* forwarding traffic to other port */
	fwd.type = DOCA_FLOW_FWD_PORT;
	fwd.port_id = port_id ^ 1;

    fwd_miss.type = DOCA_FLOW_FWD_PIPE;
    fwd_miss.next_pipe = pipe_fwd_miss;

	result = doca_flow_pipe_create(pipe_cfg, &fwd, &fwd_miss, pipe);
destroy_pipe_cfg:
	doca_flow_pipe_cfg_destroy(pipe_cfg);
	return result;
}

/*
 * Add DOCA Flow pipe entry to the hairpin pipe
 *
 * @pipe [in]: pipe of the entry
 * @status [in]: user context for adding entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
add_hairpin_pipe_entry(
    struct doca_flow_port *port,
    struct doca_flow_pipe *pipe,
    doca_be32_t dst_ip_addr,
    doca_be32_t src_ip_addr,
    doca_be16_t dst_port,
    doca_be16_t src_port
    )
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;
	doca_error_t result;
    struct entries_status status;

	/* example 5-tuple to forward */
	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
    memset(&status, 0, sizeof(status));

	match.outer.ip4.dst_ip = dst_ip_addr;
	match.outer.ip4.src_ip = src_ip_addr;
	match.outer.tcp.l4_port.dst_port = dst_port;
	match.outer.tcp.l4_port.src_port = src_port;

	result = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, &status, &entry);
	if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to add entry: %s", doca_error_get_descr(result));
		return result;
    }

    result = doca_flow_entries_process(port, 0, DEFAULT_TIMEOUT_US, 1);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to process entries: %s", doca_error_get_descr(result));
        return result;
    }

    if (status.nb_processed != 1 || status.failure) {
        DOCA_LOG_ERR("Failed to process entries");
        return DOCA_ERROR_BAD_STATE;
    }

	return DOCA_SUCCESS;
}

/*
 * Run configure_static_pipes sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t run_app(int nb_queues)
{
	int nb_ports = 2;
	struct flow_resources resource = {
        .nr_counters = 1024,
    };
	uint32_t nr_shared_resources[SHARED_RESOURCE_NUM_VALUES] = {0};
	struct doca_flow_port *ports[nb_ports];
	struct doca_dev *dev_arr[nb_ports];
	doca_error_t result;
	int port_id;

	result = init_doca_flow(nb_queues, "vnf,hws", &resource, nr_shared_resources);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA Flow: %s", doca_error_get_descr(result));
		return result;
	}

	memset(dev_arr, 0, sizeof(struct doca_dev *) * nb_ports);
	result = init_doca_flow_ports(nb_ports, ports, true, dev_arr);
	if (result != DOCA_SUCCESS) {
		DOCA_LOG_ERR("Failed to init DOCA ports: %s", doca_error_get_descr(result));
		doca_flow_destroy();
		return result;
	}

    // On each port:
    //   1. Add an RSS pipe and a match-all entry on the RSS pipe
    //   2. Add a hairpin pipe, which in miss, will forward packets to the RSS pipe
    struct doca_flow_pipe *rss_pipes[nb_ports];
    struct doca_flow_pipe *hairpin_pipes[nb_ports];
	for (port_id = 0; port_id < nb_ports; port_id++) {

        result = create_rss_pipe(ports[port_id], &rss_pipes[port_id], nb_queues);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to create RSS pipe: %s", doca_error_get_descr(result));
            stop_doca_flow_ports(nb_ports, ports);
            doca_flow_destroy();
            return result;
        }

		result = create_hairpin_pipe(ports[port_id], port_id, rss_pipes[port_id], &hairpin_pipes[port_id]);
		if (result != DOCA_SUCCESS) {
			DOCA_LOG_ERR("Failed to create hairpin pipe: %s", doca_error_get_descr(result));
			stop_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return result;
		}
	}

    // Listen for packets, when you see packets, prompt for allow/deny
    struct rte_mbuf *packets[PACKET_BURST];
    struct rte_ether_hdr *eth_hdr;
    struct rte_ipv4_hdr *ipv4_hdr;
    struct rte_tcp_hdr *tcp_hdr;
    char src_addr[INET_ADDRSTRLEN];
    char dst_addr[INET_ADDRSTRLEN];
    DOCA_LOG_INFO("Setup done. Listening for packets");
    while (1) {
        for (int port_id = 0; port_id < nb_ports; port_id++) {
            for (int queue_id = 0; queue_id < nb_queues; queue_id++) {
                int nb_packets = rte_eth_rx_burst(port_id, queue_id, packets, PACKET_BURST);
                if (nb_packets == 0) {
                    continue;
                }
                // DOCA_LOG_INFO("Received %d packets on port %d, queue %d", nb_packets, port_id, queue_id);

                // for demo only, assume it's an ipv4 TCP
                for (int packet_idx = 0; packet_idx < nb_packets; packet_idx++) {
                    eth_hdr = rte_pktmbuf_mtod(packets[packet_idx], struct rte_ether_hdr *);
                    ipv4_hdr = (struct rte_ipv4_hdr *)((char *)eth_hdr + sizeof(struct rte_ether_hdr));
                    tcp_hdr = (struct rte_tcp_hdr *)((char *)ipv4_hdr + sizeof(struct rte_ipv4_hdr));

                    inet_ntop(AF_INET, &ipv4_hdr->src_addr, src_addr, sizeof(src_addr));
                    inet_ntop(AF_INET, &ipv4_hdr->dst_addr, dst_addr, sizeof(dst_addr));

                    printf("Offload %s:%d -> %s:%d? (y/n): ", src_addr, rte_be_to_cpu_16(tcp_hdr->src_port), dst_addr, rte_be_to_cpu_16(tcp_hdr->dst_port));
                    fflush(stdout);
                    unsigned char c;
                    scanf(" %c", &c);
                    if (c == 'y') {
                        add_hairpin_pipe_entry(
                            ports[port_id],
                            hairpin_pipes[port_id],
                            ipv4_hdr->dst_addr,
                            ipv4_hdr->src_addr,
                            tcp_hdr->dst_port,
                            tcp_hdr->src_port
                        );
                        DOCA_LOG_INFO("Flow offloaded");
                    }
                    else {
                        DOCA_LOG_INFO("Flow will not be offloaded");
                    }
                }
            }
        }
    }

	return result;
}
