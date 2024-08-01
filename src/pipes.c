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

#include "selective_fwd.h"

DOCA_LOG_REGISTER(SELECTIVE_FWD::PIPES);

/*
 * Create DOCA Flow pipe with a match-all entry, that forwards the matched
 * traffic to RSS
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_rss_pipe(struct doca_flow_port* port,
                struct doca_flow_pipe** pipe,
                uint16_t nb_queues)
{
    struct doca_flow_match match;
    struct doca_flow_pipe_cfg* cfg;
    struct doca_flow_fwd fwd;
    doca_error_t result;
    uint16_t rss_queues[256];
    struct entries_status status;
    uint32_t nb_entries = 500000;

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
        DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = set_flow_pipe_cfg(cfg, "RSS_PIPE", DOCA_FLOW_PIPE_BASIC, false);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg: %s",
                     doca_error_get_descr(result));
        goto destroy_pipe_cfg;
    }
    result = doca_flow_pipe_cfg_set_match(cfg, &match, NULL);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg match: %s",
                     doca_error_get_descr(result));
        goto destroy_pipe_cfg;
    }

    result = doca_flow_pipe_cfg_set_nr_entries(cfg, nb_entries);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg nb_entries: %s",
                     doca_error_get_descr(result));
        goto destroy_pipe_cfg;
    }

    result = doca_flow_pipe_create(cfg, &fwd, NULL, pipe);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create RSS pipe: %s",
                     doca_error_get_descr(result));
        goto destroy_pipe_cfg;
    }
    doca_flow_pipe_cfg_destroy(cfg);

    /* Match on any packet */
    for (uint64_t i = 0; i < nb_entries; i++) {
        result = doca_flow_pipe_add_entry(0,
                                          *pipe,
                                          &match,
                                          NULL,
                                          NULL,
                                          &fwd,
                                          DOCA_FLOW_WAIT_FOR_BATCH,
                                          &status,
                                          NULL);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to add RSS pipe entry: %s",
                         doca_error_get_descr(result));
            return result;
        }
    }

    result = doca_flow_entries_process(port, 0, DEFAULT_TIMEOUT_US, nb_entries);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to process RSS entry: %s",
                     doca_error_get_descr(result));
        return result;
    }

    if (status.nb_processed != nb_entries || status.failure) {
        DOCA_LOG_ERR("Failed to process RSS entry");
        return DOCA_ERROR_BAD_STATE;
    }

    return result;

destroy_pipe_cfg:
    doca_flow_pipe_cfg_destroy(cfg);
    return result;
}

/*
 * Create DOCA Flow pipe with 5 tuple match that forwards the matched traffic to
 * the other port
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @pipe [out]: created pipe pointer
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
static doca_error_t
create_hairpin_pipe(struct doca_flow_port* port,
                    int port_id,
                    struct doca_flow_pipe* pipe_fwd_miss,
                    struct doca_flow_pipe** pipe)
{
    struct doca_flow_match match;
    struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
    struct doca_flow_fwd fwd, fwd_miss;
    struct doca_flow_pipe_cfg* pipe_cfg;
    struct doca_flow_monitor monitor;
    doca_error_t result;

    memset(&match, 0, sizeof(match));
    memset(&actions, 0, sizeof(actions));
    memset(&fwd, 0, sizeof(fwd));
    memset(&fwd_miss, 0, sizeof(fwd_miss));
    memset(&monitor, 0, sizeof(monitor));

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

    monitor.aging_sec = FLOW_TIMEOUT_SEC;

    result = doca_flow_pipe_cfg_create(&pipe_cfg, port);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create doca_flow_pipe_cfg: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result =
        set_flow_pipe_cfg(pipe_cfg, "HAIRPIN_PIPE", DOCA_FLOW_PIPE_BASIC, true);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg: %s",
                     doca_error_get_descr(result));
        goto destroy_pipe_cfg;
    }
    result = doca_flow_pipe_cfg_set_match(pipe_cfg, &match, NULL);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg match: %s",
                     doca_error_get_descr(result));
        goto destroy_pipe_cfg;
    }
    result = doca_flow_pipe_cfg_set_actions(
        pipe_cfg, actions_arr, NULL, NULL, NB_ACTIONS_ARR);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg actions: %s",
                     doca_error_get_descr(result));
        goto destroy_pipe_cfg;
    }

    result = doca_flow_pipe_cfg_set_monitor(pipe_cfg, &monitor);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg monitor: %s",
                     doca_error_get_descr(result));
        goto destroy_pipe_cfg;
    }

    /* forwarding traffic to other port */
    fwd.type = DOCA_FLOW_FWD_RSS;
    fwd.num_of_queues = 0xffffffff;

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
 * @port [in]: port of the entry
 * @pipe [in]: pipe of the entry
 * @dst_ip_addr [in]: destination IP address of the entry
 * @src_ip_addr [in]: source IP address of the entry
 * @dst_port [in]: destination port of the entry
 * @src_port [in]: source port of the entry
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise.
 */
doca_error_t
add_hairpin_pipe_entry(struct doca_flow_port* ports[NUM_PORTS],
                       int port_id_in,
                       uint16_t base_hairpin_q,
                       uint8_t hairpin_q_len,
                       struct doca_flow_pipe* pipe,
                       doca_be32_t dst_ip_addr,
                       doca_be32_t src_ip_addr,
                       doca_be16_t dst_port,
                       doca_be16_t src_port)
{
    struct doca_flow_match match;
    struct doca_flow_actions actions;
    struct doca_flow_pipe_entry* entry;
    struct entries_status status;
    doca_error_t result;

    /* example 5-tuple to forward */
    memset(&match, 0, sizeof(match));
    memset(&actions, 0, sizeof(actions));
    memset(&status, 0, sizeof(status));

    match.outer.ip4.dst_ip = dst_ip_addr;
    match.outer.ip4.src_ip = src_ip_addr;
    match.outer.tcp.l4_port.dst_port = dst_port;
    match.outer.tcp.l4_port.src_port = src_port;

    uint16_t hairpin_queues[hairpin_q_len];
    for (uint16_t i = 0; i < hairpin_q_len; i++)
        hairpin_queues[i] = base_hairpin_q + i;

    struct doca_flow_fwd fwd = {};
    fwd.type = DOCA_FLOW_FWD_RSS;
    fwd.rss_queues = (uint16_t*)&hairpin_queues;
    fwd.num_of_queues = hairpin_q_len;
    fwd.rss_outer_flags = DOCA_FLOW_RSS_IPV4 | DOCA_FLOW_RSS_TCP;

    result = doca_flow_pipe_add_entry(
        0, pipe, &match, &actions, NULL, &fwd, 0, &status, &entry);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to add entry: %s", doca_error_get_descr(result));
        return result;
    }

    result =
        doca_flow_entries_process(ports[port_id_in], 0, DEFAULT_TIMEOUT_US, 1);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to process entries: %s",
                     doca_error_get_descr(result));
        return result;
    }

    if (status.nb_processed != 1 || status.failure) {
        DOCA_LOG_ERR("Failed to process entries");
        return DOCA_ERROR_BAD_STATE;
    }

    DOCA_LOG_DBG("Added a hairpin pipe entry to port %d on queues %d-%d",
                 port_id_in,
                 base_hairpin_q,
                 base_hairpin_q + hairpin_q_len - 1);
    return DOCA_SUCCESS;
}

doca_error_t
configure_static_pipes(struct application_dpdk_config* app_cfg,
                       struct doca_flow_port* ports[NUM_PORTS],
                       struct doca_flow_pipe* hairpin_pipes[NUM_PORTS])
{
    doca_error_t result;

    struct doca_flow_pipe* rss_pipes[NUM_PORTS];
    for (int port_id = 0; port_id < NUM_PORTS; port_id++) {

        result = create_rss_pipe(ports[port_id],
                                 &rss_pipes[port_id],
                                 app_cfg->port_config.nb_queues);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to create RSS pipe: %s",
                         doca_error_get_descr(result));
            stop_doca_flow_ports(NUM_PORTS, ports);
            doca_flow_destroy();
            return result;
        }

        result = create_hairpin_pipe(ports[port_id],
                                     port_id,
                                     rss_pipes[port_id],
                                     &hairpin_pipes[port_id]);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to create hairpin pipe: %s",
                         doca_error_get_descr(result));
            stop_doca_flow_ports(NUM_PORTS, ports);
            doca_flow_destroy();
            return result;
        }
    }

    return result;
}
