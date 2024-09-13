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
#include <rte_byteorder.h>
#include <doca_log.h>
#include "flow_common.h"

DOCA_LOG_REGISTER(flow_common);

/*
 * Entry processing callback
 *
 * @entry [in]: DOCA Flow entry pointer
 * @pipe_queue [in]: queue identifier
 * @status [in]: DOCA Flow entry status
 * @op [in]: DOCA Flow entry operation
 * @user_ctx [out]: user context
 */
void
check_for_valid_entry(struct doca_flow_pipe_entry* entry,
                      uint16_t pipe_queue,
                      enum doca_flow_entry_status status,
                      enum doca_flow_entry_op op,
                      void* user_ctx)
{
    (void)entry;
    (void)op;
    (void)pipe_queue;
    struct entries_status* entry_status = (struct entries_status*)user_ctx;

    if (entry_status == NULL)
        return;
    if (status != DOCA_FLOW_ENTRY_STATUS_SUCCESS)
        entry_status->failure =
            true; /* set failure to true if processing failed */

    switch (op) {

        case DOCA_FLOW_ENTRY_OP_AGED:
            doca_flow_pipe_remove_entry(pipe_queue, DOCA_FLOW_NO_WAIT, entry);

        case DOCA_FLOW_ENTRY_OP_ADD:
        case DOCA_FLOW_ENTRY_OP_DEL:
            break;
        default:
            DOCA_LOG_DBG("Entry 0x%lx, op code %d", (uint64_t)entry, op);
            break;
    }

    entry_status->nb_processed++;
}

doca_error_t
init_doca_flow(int nb_queues,
               const char* mode,
               struct flow_resources* resource,
               uint32_t nr_shared_resources[])
{
    return init_doca_flow_cb(nb_queues,
                             mode,
                             resource,
                             nr_shared_resources,
                             check_for_valid_entry,
                             NULL);
}

doca_error_t
init_doca_flow_cb(int nb_queues,
                  const char* mode,
                  struct flow_resources* resource,
                  uint32_t nr_shared_resources[],
                  doca_flow_entry_process_cb cb,
                  doca_flow_pipe_process_cb pipe_process_cb)
{
    struct doca_flow_cfg* flow_cfg;
    uint16_t qidx, rss_queues[nb_queues];
    struct doca_flow_resource_rss_cfg rss = { 0 };
    doca_error_t result, tmp_result;

    result = doca_flow_cfg_create(&flow_cfg);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create doca_flow_cfg: %s",
                     doca_error_get_descr(result));
        return result;
    }

    rss.nr_queues = nb_queues;
    for (qidx = 0; qidx < nb_queues; qidx++)
        rss_queues[qidx] = qidx;
    rss.queues_array = rss_queues;
    result = doca_flow_cfg_set_default_rss(flow_cfg, &rss);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_cfg rss: %s",
                     doca_error_get_descr(result));
        goto destroy_cfg;
    }

    result = doca_flow_cfg_set_pipe_queues(flow_cfg, nb_queues);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_cfg pipe_queues: %s",
                     doca_error_get_descr(result));
        goto destroy_cfg;
    }

    result = doca_flow_cfg_set_queue_depth(flow_cfg, 1024);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set queue depth: %s",
                     doca_error_get_descr(result));
        goto destroy_cfg;
    }

    result = doca_flow_cfg_set_mode_args(flow_cfg, mode);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_cfg mode_args: %s",
                     doca_error_get_descr(result));
        goto destroy_cfg;
    }

    result = doca_flow_cfg_set_nr_counters(flow_cfg, resource->nr_counters);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_cfg nr_counters: %s",
                     doca_error_get_descr(result));
        goto destroy_cfg;
    }

    result = doca_flow_cfg_set_nr_meters(flow_cfg, resource->nr_meters);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_cfg nr_meters: %s",
                     doca_error_get_descr(result));
        goto destroy_cfg;
    }

    result = doca_flow_cfg_set_cb_entry_process(flow_cfg, cb);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR(
            "Failed to set doca_flow_cfg doca_flow_entry_process_cb: %s",
            doca_error_get_descr(result));
        goto destroy_cfg;
    }

    result = doca_flow_cfg_set_cb_pipe_process(flow_cfg, pipe_process_cb);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_cfg pipe_process_cb: %s",
                     doca_error_get_descr(result));
        goto destroy_cfg;
    }

    for (int i = 0; i < SHARED_RESOURCE_NUM_VALUES; i++) {
        result = doca_flow_cfg_set_nr_shared_resource(
            flow_cfg, nr_shared_resources[i], (enum doca_flow_shared_resource_type)i);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to set doca_flow_cfg nr_shared_resources: %s",
                         doca_error_get_descr(result));
            goto destroy_cfg;
        }
    }

    result = doca_flow_init(flow_cfg);
    if (result != DOCA_SUCCESS)
        DOCA_LOG_ERR("Failed to initialize DOCA Flow: %s",
                     doca_error_get_descr(result));
destroy_cfg:
    tmp_result = doca_flow_cfg_destroy(flow_cfg);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to destroy doca_flow_cfg: %s",
                     doca_error_get_descr(tmp_result));
        DOCA_ERROR_PROPAGATE(result, tmp_result);
    }

    return result;
}

/*
 * Create DOCA Flow port by port id
 *
 * @port_id [in]: port ID
 * @dev [in]: doca device to attach
 * @port [out]: port handler on success
 * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
 */
static doca_error_t
create_doca_flow_port(int port_id,
                      struct doca_dev* dev,
                      struct doca_flow_port** port)
{
    int max_port_str_len = 128;
    struct doca_flow_port_cfg* port_cfg;
    char port_id_str[max_port_str_len];
    doca_error_t result, tmp_result;

    result = doca_flow_port_cfg_create(&port_cfg);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to create doca_flow_port_cfg: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = doca_flow_port_cfg_set_dev(port_cfg, dev);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_port_cfg dev: %s",
                     doca_error_get_descr(result));
        goto destroy_port_cfg;
    }

    snprintf(port_id_str, max_port_str_len, "%d", port_id);
    result = doca_flow_port_cfg_set_devargs(port_cfg, port_id_str);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_port_cfg devargs: %s",
                     doca_error_get_descr(result));
        goto destroy_port_cfg;
    }

    result = doca_flow_port_start(port_cfg, port);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to start doca_flow port: %s",
                     doca_error_get_descr(result));
        goto destroy_port_cfg;
    }

destroy_port_cfg:
    tmp_result = doca_flow_port_cfg_destroy(port_cfg);
    if (tmp_result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to destroy doca_flow port: %s",
                     doca_error_get_descr(tmp_result));
        DOCA_ERROR_PROPAGATE(result, tmp_result);
    }

    return result;
}

doca_error_t
stop_doca_flow_ports(int nb_ports, struct doca_flow_port* ports[])
{
    int portid;
    doca_error_t ret, doca_error = DOCA_SUCCESS;

    /*
     * Stop the ports in reverse order, since in switch mode port 0
     * is proxy port, and proxy port should stop as last.
     */
    for (portid = nb_ports - 1; portid >= 0; portid--) {
        if (ports[portid] != NULL) {
            ret = doca_flow_port_stop(ports[portid]);
            /* record first error */
            if (ret != DOCA_SUCCESS && doca_error == DOCA_SUCCESS)
                doca_error = ret;
        }
    }
    return doca_error;
}

doca_error_t
init_doca_flow_ports(int nb_ports,
                     struct doca_flow_port* ports[],
                     bool is_hairpin,
                     struct doca_dev* dev_arr[])
{
    int portid;
    doca_error_t result;

    for (portid = 0; portid < nb_ports; portid++) {
        /* Create doca flow port */
        result = create_doca_flow_port(portid, dev_arr[portid], &ports[portid]);
        if (result != DOCA_SUCCESS) {
            DOCA_LOG_ERR("Failed to start port: %s",
                         doca_error_get_descr(result));
            if (portid != 0)
                stop_doca_flow_ports(portid, ports);
            return result;
        }
    }
    return DOCA_SUCCESS;
}

doca_error_t
set_flow_pipe_cfg(struct doca_flow_pipe_cfg* cfg,
                  const char* name,
                  enum doca_flow_pipe_type type,
                  bool is_root)
{
    doca_error_t result;

    if (cfg == NULL) {
        DOCA_LOG_ERR("Failed to set DOCA Flow pipe configurations, cfg=NULL");
        return DOCA_ERROR_INVALID_VALUE;
    }

    result = doca_flow_pipe_cfg_set_name(cfg, name);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg name: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = doca_flow_pipe_cfg_set_type(cfg, type);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg type: %s",
                     doca_error_get_descr(result));
        return result;
    }

    result = doca_flow_pipe_cfg_set_is_root(cfg, is_root);
    if (result != DOCA_SUCCESS) {
        DOCA_LOG_ERR("Failed to set doca_flow_pipe_cfg is_root: %s",
                     doca_error_get_descr(result));
        return result;
    }

    return result;
}
