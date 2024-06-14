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

#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_malloc.h>
#include <rte_byteorder.h>

#include <doca_log.h>
#include <doca_mmap.h>
#include <doca_flow.h>
#include <doca_argp.h>
#include <doca_buf_inventory.h>

#include "dpdk_utils.h"
#include "flow_common.h"

#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define NUM_PORTS 2
#define PACKET_BURST 256

void
start_pmd(
    struct doca_flow_port* ports[NUM_PORTS],
    struct doca_flow_pipe* hairpin_pipes[NUM_PORTS],
    uint32_t queues_per_port
);

doca_error_t
add_hairpin_pipe_entry(
    struct doca_flow_port *port,
    struct doca_flow_pipe *pipe,
    doca_be32_t dst_ip_addr,
    doca_be32_t src_ip_addr,
    doca_be16_t dst_port,
    doca_be16_t src_port
);

doca_error_t
configure_static_pipes(
	struct doca_flow_port *ports[NUM_PORTS],
	struct doca_flow_pipe *hairpin_pipes[NUM_PORTS]
);
