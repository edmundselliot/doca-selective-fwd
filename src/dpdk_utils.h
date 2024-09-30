/*
 * Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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

#ifndef COMMON_DPDK_UTILS_H_
#define COMMON_DPDK_UTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <rte_flow.h>
#include <rte_mbuf.h>

#include <doca_error.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define RX_RING_SIZE 1024 /* RX ring size */
#define TX_RING_SIZE 1024 /* TX ring size */
#define NUM_MBUFS                                                              \
    (8 * 1024)              /* Number of mbufs to be allocated in the mempool */
#define MBUF_CACHE_SIZE 250 /* mempool cache size */
#define MAX_PORTS 16        /* Maximum number of ports */

    struct doca_dev;
    struct dpdk_mempool_shadow;
    struct doca_buf_inventory;
    struct doca_buf;

    /* Port configuration */
    struct application_port_config
    {
        int nb_ports;       /* Set on init to 0 for don't care, required ports
                               otherwise */
        uint16_t nb_queues; /* Set on init to 0 for don't care, required minimum
                               cores otherwise */
        int nb_hairpin_q;   /* Set on init to 0 to disable, hairpin queues
                               otherwise */
        uint16_t
            enable_mbuf_metadata : 1; /* Set on init to 0 to disable, otherwise
                                         it will add meta to each mbuf */
        uint16_t self_hairpin : 1; /* Set on init to 1 enable both self and peer
                                      hairpin */
        uint16_t rss_support : 1;  /* Set on init to 0 for no RSS support, RSS
                                      support  otherwise */
        uint16_t lpbk_support : 1; /* Enable loopback support */
        uint16_t isolated_mode : 1; /* Set on init to 0 for no isolation,
                                       isolated mode otherwise */
        uint16_t switch_mode : 1;   /* Set on init to 1 for switch mode */
    };

    /* DPDK configuration */
    struct application_dpdk_config
    {
        struct application_port_config
            port_config;          /* DPDK port configuration */
        bool reserve_main_thread; /* Reserve lcore for the main thread */
        struct rte_mempool*
            mbuf_pool; /* Will be filled by "dpdk_queues_and_ports_init".
                        * Memory pool that will be used by the DPDK ports
                        * for allocating rte_pktmbuf
                        */

        // NxN matrix of hairpin queues
        // hairpin_queues[x][y] is the base queue number from port x to port y
        uint16_t hairpin_queues[MAX_PORTS][MAX_PORTS];
        // hairpin_q_count is the number of hairpin queues between each pair of
        // ports range will be from hairpin_queues[x][y] to hairpin_queues[x][y]
        // + hairpin_q_count - 1
        uint8_t hairpin_q_count;
        // worker cores which will not be used for a PMD and will not require a queue
        uint8_t reserved_cores;
    };

    /*
     * Initialize DPDK environment
     *
     * @argc [in]: number of program command line arguments
     * @argv [in]: program command line arguments
     * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
     */
    doca_error_t dpdk_init(int argc, char** argv);

    /*
     * Destroy DPDK environment
     */
    void dpdk_fini(void);

    /*
     * Initialize DPDK ports and queues
     *
     * @app_dpdk_config [in/out]: application DPDK configuration values
     * @return: DOCA_SUCCESS on success and DOCA_ERROR otherwise
     */
    doca_error_t dpdk_queues_and_ports_init(
        struct application_dpdk_config* app_dpdk_config);

    /*
     * Destroy DPDK ports and queues
     *
     * @app_dpdk_config [in]: application DPDK configuration values
     */
    void dpdk_queues_and_ports_fini(
        struct application_dpdk_config* app_dpdk_config);

    /*
     * Initialize a shadow of a DPDK memory pool the shadow will have all of
     * DPDK's memory registered to a device
     *
     * @mbuf_pool [in]: the DPDK memory pool
     * @device [in]: DOCA device for memory registration
     * @return: dpdk_mempool_shadow with memory of the DPDK pool registered on
     * success, NULL otherwise
     */
    struct dpdk_mempool_shadow* dpdk_mempool_shadow_create(
        struct rte_mempool* mbuf_pool,
        struct doca_dev* device);

    /*
     * Initialize a shadow of a DPDK memory pool the shadow will have all of RTE
     * external memory
     *
     * @ext_mem [in]: Pointer to the array of structures describing the external
     * memory for data buffers.
     * @ext_num [in]: Number of elements in the ext_mem array.
     * @device [in]: DOCA device for memory registration
     * @return: dpdk_mempool_shadow with memory of the DPDK pool registered on
     * success, NULL otherwise
     */
    struct dpdk_mempool_shadow* dpdk_mempool_shadow_create_extbuf(
        const struct rte_pktmbuf_extmem** ext_mem,
        uint32_t ext_num,
        struct doca_dev* device);

    /*
     * Find the DOCA mmap instance that contains the requested address range
     * then allocate DOCA buffer from it
     *
     * @mempool_shadow [in]: shadow of a DPDK memory pool
     * @inventory [in]: a DOCA buffer inventory used for allocating the buffer
     * @mem_range_start [in]: start address of memory range, must be within
     * range of some 'rte_mbuf'
     * @mem_range_size [in]: the size of the memory range in bytes, must be
     * within range of some 'rte_mbuf'
     * @out_buf [out]: DOCA buffer allocated with data pointing to the given
     * range
     * @return: DOCA_SUCCESS on success, and doca_error_t otherwise
     */
    doca_error_t dpdk_mempool_shadow_find_buf_by_data(
        struct dpdk_mempool_shadow* mempool_shadow,
        struct doca_buf_inventory* inventory,
        uintptr_t mem_range_start,
        size_t mem_range_size,
        struct doca_buf** out_buf);

    /*
     * Destroy the DPDK memory pool shadow
     *
     * @mempool_shadow [in]: shadow of a DPDK memory pool to be destroyed
     * @Note: this should be done before destroying the DPDK memory pool
     */
    void dpdk_mempool_shadow_destroy(
        struct dpdk_mempool_shadow* mempool_shadow);

    /*
     * Print packet header information
     *
     * @packet [in]: packet mbuf
     * @l2 [in]: if true the function prints l2 header
     * @l3 [in]: if true the function prints l3 header
     * @l4 [in]: if true the function prints l4 header
     */
    void print_header_info(const struct rte_mbuf* packet,
                           const bool l2,
                           const bool l3,
                           const bool l4);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* COMMON_DPDK_UTILS_H_ */
