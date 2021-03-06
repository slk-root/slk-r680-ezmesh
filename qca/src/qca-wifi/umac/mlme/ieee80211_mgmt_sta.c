/*
 * Copyright (c) 2011-2021 Qualcomm Innovation Center, Inc.
 * All Rights Reserved
 * Confidential and Proprietary - Qualcomm Innovation Center, Inc.
 *
 * 2011-2016 Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Qualcomm Atheros Confidential and Proprietary.
 *
 * Copyright (c) 2010, Atheros Communications Inc.
 * All Rights Reserved.

 */

#include "ieee80211_mlme_priv.h"
#include "ieee80211_bssload.h"
#include <ieee80211_mlme_dfs_dispatcher.h>
#include <wlan_son_pub.h>
#include <wlan_objmgr_psoc_obj.h>
#include <wlan_objmgr_pdev_obj.h>
#include <wlan_dfs_ioctl.h>
#include <wlan_utility.h>
#include <wlan_reg_services_api.h>
#if QCN_IE
#include <ol_if_athvar.h>
#endif
#ifdef QCA_SUPPORT_CP_STATS
#include <wlan_cp_stats_ic_utils_api.h>
#endif
#define IEEE80211_IS_MAX_CST_GREATER_THAN_ONE_BEACON_INTERVAL(_ic,_ni) \
((_ic)->ic_mcst_of_rootap > ((_ni)->ni_chanswitch_tbtt * (_ni)->ni_intval))
#define UNSIGNED_MAX_24BIT 0xFFFFFF

#if QCN_IE
extern ol_ath_soc_softc_t *ol_global_soc[GLOBAL_SOC_SIZE];
extern int ol_num_global_soc;
#endif

static OS_TIMER_FUNC(ieee80211_synced_channel_switch);
static OS_TIMER_FUNC(ieee80211_synced_disconnect_sta);
int
ieee80211_recv_asresp(struct ieee80211_node *ni,
                      wbuf_t wbuf,
                      int subtype)
{
    struct ieee80211vap *vap = ni->ni_vap;
    struct ieee80211_frame *wh;
    u_int8_t *frm, *efrm;
    u_int16_t capinfo, associd, status;

    wh = (struct ieee80211_frame *) wbuf_header(wbuf);
    frm = (u_int8_t *)&wh[1];
    efrm = wbuf_header(wbuf) + wbuf_get_pktlen(wbuf);

    if (vap->iv_opmode != IEEE80211_M_STA && vap->iv_opmode != IEEE80211_M_BTAMP) {
#ifdef QCA_SUPPORT_CP_STATS
        vdev_cp_stats_rx_mgmt_discard_inc(vap->vdev_obj, 1);
#endif
        return -EINVAL;
    }

    IEEE80211_VERIFY_ADDR(ni);

    /*
     * asresp frame format
     *  [2] capability information
     *  [2] status
     *  [2] association ID
     *  [tlv] supported rates
     *  [tlv] extended supported rates
     *  [tlv] WME
     *  [tlv] HT
     *  [tlv] VHT
     *  [tlv] Bandwidth NSS mapping
     */
    IEEE80211_VERIFY_LENGTH(efrm - frm, 6);
    capinfo = le16toh(*(u_int16_t *)frm); frm += 2;
    status = le16toh(*(u_int16_t *)frm); frm += 2;
    associd = le16toh(*(u_int16_t *)frm); frm += 2;

    ieee80211_mlme_recv_assoc_response(ni, subtype, capinfo, status,  associd, frm, efrm - frm, wbuf);

    return 0;
}

static bool
ieee80211_phymode_match(ieee80211_scan_entry_t scan_entry, struct ieee80211_node *ni)
{
    u_int32_t    ni_phy_mode, se_phy_mode;

    ni_phy_mode = wlan_channel_phymode(ni->ni_chan);
    se_phy_mode = util_scan_entry_phymode(scan_entry);

    return (se_phy_mode == ni_phy_mode);
}

static bool
ieee80211_ssid_match(ieee80211_scan_entry_t scan_entry, struct ieee80211_node *ni)
{
    u_int8_t    ssid_len;
    u_int8_t    *ssid;

    /*
     * If beacon/probe response contains SSID, accept it only if it matches
     * the node's SSID;
     * If beacon/probe response has no SSID, accept it to reduce incidence of
     * timeouts.
     */
    ssid_len = util_scan_entry_ssid(scan_entry)->length;
    ssid = util_scan_entry_ssid(scan_entry)->ssid;
    if (ssid_len != 0) {
        return (OS_MEMCMP(ssid, ni->ni_essid, ssid_len) == 0);
    }

    // SSID is NULL - accept it.
    return true;
}

void ieee80211_scs_vattach(struct ieee80211vap *vap) {
   struct ieee80211com *ic = vap->iv_ic;
   osdev_t os_handle = ic->ic_osdev;
   vap->iv_cswitch_rxd = 0;

   OS_INIT_TIMER(os_handle, &vap->iv_cswitch_timer, ieee80211_synced_channel_switch, vap, QDF_TIMER_TYPE_WAKE_APPS);
   OS_INIT_TIMER(os_handle, &vap->iv_disconnect_sta_timer, ieee80211_synced_disconnect_sta, vap, QDF_TIMER_TYPE_WAKE_APPS);
}

void ieee80211_scs_vdetach(struct ieee80211vap *vap) {
   OS_FREE_TIMER(&vap->iv_cswitch_timer);
   OS_FREE_TIMER(&vap->iv_disconnect_sta_timer);
}

static OS_TIMER_FUNC(ieee80211_synced_disconnect_sta)
{
    struct ieee80211vap                          *vap = NULL;
    struct ieee80211_node                        *ni = NULL;

    OS_GET_TIMER_ARG(vap, struct ieee80211vap *);

    ni = vap->iv_ni;

    ieee80211_mlme_recv_csa(ni, IEEE80211_RADAR_DETECT_DEFAULT_DELAY,true);
    OS_CANCEL_TIMER(&vap->iv_disconnect_sta_timer);
}

static OS_TIMER_FUNC(ieee80211_synced_channel_switch)
{
    struct ieee80211vap          *vap  = NULL;
    struct ieee80211com          *ic   = NULL;
    struct ieee80211_node        *ni   = NULL;
    struct ieee80211_ath_channel *chan = NULL;
    int err = EOK;

    OS_GET_TIMER_ARG(vap, struct ieee80211vap *);

    ic = vap->iv_ic;

    ni = vap->iv_ni;
    if (!ni) {
        IEEE80211_DPRINTF(vap, IEEE80211_MSG_MLME,
                          "%s: Invalid node structure. Cannot service CSA if there is no BSS peer",
                          __func__);
        goto exit;
    }

    chan = vap->iv_cswitch_chan;

    if (chan != NULL) {
        IEEE80211_DPRINTF(vap, IEEE80211_MSG_MLME,
                "%s: Prev chan=%u freq=%d New Chan=%u freq %d, active AP=%u rx CSA=%u\n",
                __func__,ic->ic_curchan->ic_ieee, ic->ic_curchan->ic_freq, chan->ic_ieee, chan->ic_freq,
                ieee80211_num_apvap_running(ic),
                vap->iv_cswitch_rxd);
    }

    /*
     * Service the CSA in this API only if the following conditions hold true:
     * (1) CSA IE was received.
     * (2) There is no running AP vap
     *     If running AP VAP is present, channel switch is handled in ieee80211_beacon_update().
     * (3) STA is active (if STA disconnects before the timer expires, cancel the CSA.
     */
    if (vap->iv_cswitch_rxd && !ieee80211_num_apvap_running(ic) &&
        (wlan_vdev_chan_config_valid(vap->vdev_obj) == QDF_STATUS_SUCCESS) &&
        (ic->ic_flags & IEEE80211_F_CHANSWITCH)) {
        /*
         * For Station, just switch channel right away.
         */
        if (!IEEE80211_IS_CHAN_SWITCH_STARTED(ic) && (chan != NULL)) {

            IEEE80211_CHAN_SWITCH_START(ic);

            if (err == EOK) {
            /* Since Root AP is going to come up in the channel after a long time, we can disconnect */
               if (IEEE80211_IS_PRIMARY_OR_SECONDARY_CHAN_DFS(chan)&&
                   IEEE80211_IS_MAX_CST_GREATER_THAN_ONE_BEACON_INTERVAL(ic, ni)) {
                    ieee80211_mlme_recv_csa(ni, IEEE80211_RADAR_DETECT_DEFAULT_DELAY,true);
                } else {
                    u_int16_t  chanchange_chwidth;

                    switch(ic->ic_chanchange_chwidth) {
                        case CHWIDTH_20:
                            chanchange_chwidth = IEEE80211_CWM_WIDTH20;
                            break;
                        case CHWIDTH_40:
                            chanchange_chwidth = IEEE80211_CWM_WIDTH40;
                            break;
                        case CHWIDTH_80:
                            chanchange_chwidth = IEEE80211_CWM_WIDTH80;
                            break;
                        case CHWIDTH_160:
                            chanchange_chwidth = IEEE80211_CWM_WIDTH160;
                            break;
                        default:
                            chanchange_chwidth = ic->ic_cwm_get_width(ic);;
                            qdf_err("Invalid change channel width specified Using current channel width %d\n",chanchange_chwidth);
                            break;
                    }

                    if (chanchange_chwidth != ni->ni_chwidth)
                    {
                        ni->ni_chwidth = chanchange_chwidth;
                        /* ni_phymode is updated based on iv_cur_mode when
                         * channel width is 160. Hence update iv_cur_mode
                         * based on new target channel's width.
                         */
                        ni->ni_vap->iv_cur_mode = ieee80211_chan2mode(chan);
                        ieee80211_update_ht_vht_he_phymode(ic, ni);
                        /* Updated phymode is sent to FW as part of
                         * ol_ath_send_peer_assoc() */
                    }

                    /* Update node channel setting */
                    ieee80211_node_set_chan(ni);

                    wlan_pdev_mlme_vdev_sm_seamless_chan_change(ic->ic_pdev_obj,
                                      vap->vdev_obj, chan);
                }

                ieee80211com_clear_flags(ic, IEEE80211_F_CHANSWITCH);
                IEEE80211_CHAN_SWITCH_END(ic);
                /* Resetting max_chan_switch_time to zero */
                ic->ic_mcst_of_rootap = 0;
                /* Resetting has_rootap_done_cac to false */
                ic->ic_has_rootap_done_cac = false;

            } else if (err == EBUSY) {
                err = EOK;
            }
        } /* End of if (!IEEE80211_IS_CHAN_SWITCH_STARTED(ic) && (chan != NULL))  */
        if (err != EOK) {
            /*
             * If failed to switch the channel, mark the AP as radar detected and disconnect from the AP.
             */
            ieee80211_mlme_recv_csa(ni, IEEE80211_RADAR_DETECT_DEFAULT_DELAY, true);
        }

    }

exit:
    vap->iv_cswitch_rxd = 0;
}

void ieee80211_process_csa(struct ieee80211_node *ni,
                struct ieee80211_ath_channel *chan,
                struct ieee80211_ath_channelswitch_ie *chanie,
                struct ieee80211_extendedchannelswitch_ie *echanie, u_int8_t *update_beacon)
{
        struct ieee80211com *ic = ni->ni_ic;
        struct ieee80211vap *vap = ni->ni_vap;
        struct wlan_objmgr_pdev *pdev = ic->ic_pdev_obj;

        if(pdev == NULL) {
                qdf_print("%s : pdev is null", __func__);
                return;
        }

        ic->ic_curchan->ic_flagext |= IEEE80211_CHAN_CSA_RECEIVED;

        if((vap->iv_opmode == IEEE80211_M_STA)
                        && !(ic->ic_flags & IEEE80211_F_CHANSWITCH)) {
                ieee80211_dfs_cancel_waitfor_csa_timer(ic);
                /*
                 * This VAP is in WDS mode and the received beacon is from
                 * our AP which contains a CSA.
                 */
                ic->ic_chanchange_chan_freq = chan->ic_freq;
                ic->ic_chanchange_tbtt = (chanie != NULL) ? chanie->tbttcount :
                                                            echanie->tbttcount;
                vap->iv_bsschan  = chan;
                vap->iv_cswitch_chan = chan;

                /* Override the iv_bsschan and ic_chanchange_chan
                 * if ignore CSA_DFS is set
                 */

                if(IEEE80211_IS_CSH_IGNORE_CSA_DFS_ENABLED(ic) &&
                                IEEE80211_IS_PRIMARY_OR_SECONDARY_CHAN_DFS(chan)) {
                        uint16_t target_chan_freq = 0;
                        struct ieee80211_ath_channel *ptarget_channel = NULL;
                        struct ch_params ch_params;
                        enum ieee80211_phymode chan_mode;

                        ieee80211_regdmn_get_chan_params(ic, &ch_params);

                        if (wlan_objmgr_pdev_try_get_ref(pdev, WLAN_DFS_ID) !=
                                        QDF_STATUS_SUCCESS) {
                                return;
                        }
                        target_chan_freq = mlme_dfs_random_channel(
                                        pdev, &ch_params, DFS_RANDOM_CH_FLAG_NO_DFS_CH);
                        if(!target_chan_freq) {
                                target_chan_freq = mlme_dfs_random_channel(pdev,
                                                &ch_params, 0);
                        }
                        wlan_objmgr_pdev_release_ref(pdev, WLAN_DFS_ID);

                        if(target_chan_freq) {
                                chan_mode = ieee80211_get_target_channel_mode(ic,
                                                &ch_params);
                                ptarget_channel = ieee80211_find_dot11_channel(ic,
                                                                               target_chan_freq,
                                                                               ch_params.mhz_freq_seg1, chan_mode);
                                if (ptarget_channel) {
                                        chan = ptarget_channel;
                                        vap->iv_bsschan = ptarget_channel;
                                        ic->ic_chanchange_chan_freq = ptarget_channel->ic_freq;
                                        vap->iv_cswitch_chan = chan;
                                }
                        } else {
                                ic->no_chans_available = 1;
                                qdf_print("%s: vap-%d(%s) channel is not available, bringdown all the AP vaps",
                                                __func__,vap->iv_unit,vap->iv_netdev_name);
                                osif_bring_down_vaps(ic, vap);
                        }
                }
                ic->ic_chanchange_channel = chan;
                ic->ic_chanchange_chwidth = ieee80211_get_chan_width(chan);
                ic->ic_chanchange_secoffset = ieee80211_sec_chan_offset(chan);
                ic->ic_chanchange_chanflag = chan->ic_flags;
                /* Update node channel setting */
                ieee80211_node_set_chan(ni);
                if (ieee80211_num_apvap_running(ic)) {
                    wlan_pdev_mlme_vdev_sm_csa_restart(pdev, chan);
                    /* update beacon only if AP vdev is running */
                    if (update_beacon)
                        *update_beacon = 1;
                }
                /* The IEEE80211_F_CHANSWITCH flag in ic will be cleared by
                 * the VAP that changes channel. Set it unconditionally.
                 */
                ieee80211com_set_flags(ic, IEEE80211_F_CHANSWITCH);
        }

        vap->iv_cswitch_rxd = 1;
        if (chanie != NULL) {
                ni->ni_chanswitch_tbtt = chanie->tbttcount;
        }
        else if (echanie != NULL) {
                ni->ni_chanswitch_tbtt = echanie->tbttcount;
        }
        else {
                // should never come here
                ni->ni_chanswitch_tbtt = 0;
        }
        vap->iv_ni = ni;
        vap->channel_switch_state = 1; /* channel switch in progress */
        OS_SET_TIMER(&vap->iv_cswitch_timer,IEEE80211_TU_TO_MS(ni->ni_chanswitch_tbtt*ni->ni_intval));
}

/**
 * find_min_val_in_arr() - Find the minimum value in a given integer array.
 *
 * @min: Pointer to the output minimum value.
 * @arr: Input integer array.
 * @size: Size of the input integer array.
 * Return: void.
 */
static void find_min_val_in_arr(int8_t *min, int8_t *arr, uint8_t size)
{
    int i;

    *min = arr[0];

    for (i = 0; i < size; ++i)
        if (*min < arr[i])
            *min = arr[i];

}

#define MIN_6G_CHANWIDTH 20
static int8_t
iee80211_tpe_log2(uint8_t value)
{
    int8_t log_out;

    switch (value) {
        case 1:
            log_out = 0;
            break;
        case 2:
            log_out = 1;
            break;
        case 4:
            log_out = 2;
            break;
        case 8:
            log_out = 3;
            break;
        default:
            log_out = -1;
    }
    return log_out;
}

/**
 * ieee80211_scale_down_power() - The power received in beacon is in 0.5dBm units.
 * Scale down the power to 1dBm scale before processing it.
 *
 * @arr: The input array which contains power values in 0.5dBm, is converted to
 * the output array which contains power value in 1dBm scale.
 * @num: Number of elements of the array to be converted.
 * Return: void.
 */
static void
ieee80211_scale_down_power(int8_t *arr, uint8_t num)
{
    int i;

    for (i = 0; i < num; i++) {
        arr[i] /= 2;
    }
}

/**
 * ieee80211_process_tpe_eirp() - Parse and store the count(in the TPE IE)
 * number of eirp power values received from the TPE.
 *
 * When the count is 0 - The power value indicates maximum tx power for 20MHz.
 * When the count is 1 - The power value indicates maximum tx power for 20MHz,
 *                       and 40MHz.
 * When the count is 2 - The power value indicates maximum tx power for 20MHZ,
 *                       40MHz and 80MHz.
 * When the count is 3 - The power value indicates maximum tx power for 20MHZ,
 *                       40MHz,80MHz, 80P80MHz and 160MHz.
 * @ni: Pointer to the node that received the TPE.
 * @tpe: Input TPE IE.
 * Return: void.
 */
static void
ieee80211_process_tpe_eirp(struct ieee80211_node *ni,
                           struct ieee80211_ie_vht_txpwr_env *tpe)
{
    u_int8_t count;
    int8_t count_from_chwidth;
    int8_t *local_max_txpwr;
    bool is_power_changed;
    uint8_t n_cur_supp_bw;
    uint8_t n_elem_to_cmp;

    count = tpe->tpe_payload.tpe_info_cnt;
    n_cur_supp_bw = ieee80211_get_chan_width(ni->ni_chan) / MIN_6G_CHANWIDTH;
    count_from_chwidth = iee80211_tpe_log2(n_cur_supp_bw);
    count_from_chwidth = (count_from_chwidth == -1) ? count : count_from_chwidth;

    local_max_txpwr = tpe->tpe_payload.local_max_txpwr;
    ieee80211_scale_down_power(local_max_txpwr, count + 1);
    /* memcomparison of mode and old values from ni */
    n_elem_to_cmp = qdf_min(count_from_chwidth + 1, count + 1);
    is_power_changed = !qdf_mem_cmp(ni->ni_eirp_limit, local_max_txpwr, n_elem_to_cmp);

    if (is_power_changed) {
        /* Apply the power change and send the command to FW. */
        qdf_mem_copy(ni->ni_eirp_limit, local_max_txpwr, n_elem_to_cmp);
        /* write a minimum function to send only 1 value or store all the values */
        find_min_val_in_arr(&ni->ni_eirp_min_lim, local_max_txpwr, n_elem_to_cmp);
    }

}

#define TWO_POW(n) (0x1 << (n))
/**
 * ieee80211_process_tpe_eirppsd() - Parse and store the count(in the TPE IE)
 * number of eirppsd power values received from the TPE.
 *
 * When the count is 0 - The power value indicates maximum tx power for any PPDU.
 * When the count is 1 - The power value indicates maximum tx power for the
 * 20MHz channel.
 * When the count is 2 - The power values indicate maximum tx powers for two
 * consecutive 20MHz-subchannels of the 40Mhz channel.
 * When the count is 3 - The power values indicate maximum tx powers for four
 * consecutive 20MHz-subchannels of the 80Mhz channel.
 * When the count is 4 - The power values indicate maximum tx powers for eight
 * 20MHz-subchannels of the 160MHz/80P80Mhz channel.
 *
 * @ni: Pointer to the node that received the TPE.
 * @tpe: Input TPE IE.
 * Return: void.
 */
static void
ieee80211_process_tpe_eirppsd(struct ieee80211_node *ni,
                              struct ieee80211_ie_vht_txpwr_env *tpe)
{
    u_int8_t count;
    int8_t *local_max_txpwr;
    uint8_t n_subchans_in_ie;
    uint8_t n_max_subchans;
    uint8_t n_elem_to_cmp;
    bool is_power_changed;

    n_max_subchans = ieee80211_get_chan_width(ni->ni_chan)/ MIN_6G_CHANWIDTH;
    count = tpe->tpe_payload.tpe_info_cnt;

    local_max_txpwr = tpe->tpe_payload.local_max_txpwr;

    n_subchans_in_ie = count ? TWO_POW(count-1) : 1;
    ieee80211_scale_down_power(local_max_txpwr, n_subchans_in_ie);

    n_elem_to_cmp = qdf_min(n_subchans_in_ie, n_max_subchans);
    /* memcomparison of mode and old values from ni */
    is_power_changed = !qdf_mem_cmp(ni->ni_eirppsd_limit, local_max_txpwr, n_elem_to_cmp);

    if (is_power_changed) {
        qdf_mem_copy(ni->ni_eirppsd_limit, local_max_txpwr, n_subchans_in_ie);
        /* Apply the power change and send the command to FW. */
        /* write a minimum function to send only 1 value or store all the values */
        find_min_val_in_arr(&ni->ni_eirppsd_min_lim, local_max_txpwr, n_subchans_in_ie);
    }
}

/**
 * ieee80211_process_tpe_ie() - Parse and store the count(in the TPE IE) number
 * of power values received from the TPE.
 *
 * @ni: Pointer to the node that received the TPE.
 * @input_tpe: Input TPE IE.
 * Return: void.
 */
static void
ieee80211_process_tpe_ie(struct ieee80211_node *ni, u_int8_t **input_tpe)
{
    u_int8_t intrprt;
    u_int8_t category;
    enum reg_6g_client_type local_sta_category;
    int i;
    struct ieee80211_ie_vht_txpwr_env *tpe[WLAN_MAX_NUM_TPE_IE];

    if (!input_tpe)
        return;

    if (!wlan_reg_is_6ghz_chan_freq(ni->ni_chan->ic_freq))
        return;

    /* If the AP is a standard power AP, ignore the TPE
     * and use local power from regulatory
     */
    switch (ni->ni_ap_power_type) {
        case REG_INDOOR_AP:
            break;
        case REG_STANDARD_POWER_AP:
        default:
            return;
    }

    wlan_reg_get_cur_6g_client_type(ni->ni_ic->ic_pdev_obj, &local_sta_category);

    for (i = 0; i < WLAN_MAX_NUM_TPE_IE; i++) {
        tpe[i] = (struct ieee80211_ie_vht_txpwr_env *) input_tpe[i];

        if (!tpe[i])
            continue;
        category = tpe[i]->tpe_payload.tpe_info_cat;

        if (category != local_sta_category)
            continue;

        intrprt = tpe[i]->tpe_payload.tpe_info_intrpt;

        /* If the STA VAP's mobility type/category does not match the
         * TPE element's category then ignore the TPE.
         * NOTE:- The root AP may send more than one TPE IEs in the
         * same beacon/Probe Response.
         */
        switch (intrprt) {
            case IEEE80211_TPE_LOCAL_EIRP:
            case IEEE80211_TPE_REG_EIRP:
                ieee80211_process_tpe_eirp(ni, tpe[i]);
                break;
            case IEEE80211_TPE_LOCAL_EIRP_PSD:
            case IEEE80211_TPE_REG_EIRP_PSD:
                ieee80211_process_tpe_eirppsd(ni, tpe[i]);
                break;
            default:
                /* Maximum Transmit Power interpretation 4-7 is Reserved*/
                break;
        }
    }

}

uint8_t ieee80211_get_snr(ieee80211_scan_entry_t  scan_entry, struct ieee80211vap *vap)
{
    uint8_t                                      snr;
    qdf_list_t                                   *list;
    struct scan_filter                           *filter;
    struct scan_cache_entry                      *se = NULL;
    qdf_list_node_t                              *se_list = NULL;
    struct scan_cache_node                       *se_node;

    filter = qdf_mem_malloc(sizeof(*filter));
    if (!filter) {
        snr= util_scan_entry_snr(scan_entry);
        return snr;
    } else {
        filter->num_of_bssid = 1;
        filter->ssid_list[0].length = util_scan_entry_ssid(scan_entry)->length;
        if (filter->ssid_list[0].length) {
            filter->num_of_ssid = 1;
            OS_MEMCPY(filter->ssid_list[0].ssid,
                      util_scan_entry_ssid(scan_entry)->ssid,
                      util_scan_entry_ssid(scan_entry)->length);
        } else {
            filter->num_of_ssid = 0;
        }

        OS_MEMCPY((filter->bssid_list), util_scan_entry_bssid(scan_entry), QDF_MAC_ADDR_SIZE);

        list = ucfg_scan_get_result(wlan_vap_get_pdev(vap), filter);
        if (!list) {
            snr= util_scan_entry_snr(scan_entry);
            goto free_filter;
        }
        qdf_list_peek_front(list, &se_list);
        if (se_list) {
            se_node = qdf_container_of(se_list,
                    struct scan_cache_node, node);
            se = se_node->entry;
        }
        if(!se) {
            snr= util_scan_entry_snr(scan_entry);
        } else {
            snr= util_scan_entry_snr(se);
        }
    }
    ucfg_scan_purge_results(list);
free_filter:
    qdf_mem_free(filter);

    return snr;
}

#if DBDC_REPEATER_SUPPORT
static void
ieee80211_bringdown_secondary_stavap(struct ieee80211com *ic)
{
    int i = 0;
    struct ieee80211com *tmp_ic;
    struct global_ic_list *ic_list = ic->ic_global_list;
    wlan_if_t tmp_stavap;

    if (ic_list->num_stavaps_up != 2) {
        return;
    }

    for (i = 0; i < MAX_RADIO_CNT; i++) {
       GLOBAL_IC_LOCK_BH(ic_list);
       tmp_ic = ic_list->global_ic[i];
       GLOBAL_IC_UNLOCK_BH(ic_list);
       if(tmp_ic && tmp_ic->ic_sta_vap) {
           tmp_stavap = tmp_ic->ic_sta_vap;
           if (wlan_is_connected(tmp_stavap) && (tmp_stavap != ic_list->max_priority_stavap_up)) {
                qdf_print("RootAP access status changed, bring down secondary stavap:%s",ether_sprintf(tmp_stavap->iv_myaddr));
                IEEE80211_DELIVER_EVENT_MLME_DISASSOC_INDICATION(tmp_stavap, tmp_stavap->iv_bss->ni_macaddr, 0, IEEE80211_REASON_UNSPECIFIED);
                break;
           }
       }
    }
}
#endif
#include <wlan_vdev_mlme.h>

void ieee80211_recv_beacon_sta(struct ieee80211_node *ni, wbuf_t wbuf, int subtype,
                               struct ieee80211_rx_status *rs,
			       ieee80211_scan_entry_t  scan_entry)
{
    struct ieee80211vap                          *tmpvap, *vap = ni->ni_vap;
    struct ieee80211com                          *ic = ni->ni_ic;
    u_int16_t                                    capinfo, erp;
    u_int8_t                                     *tim_elm = NULL;
    struct ieee80211_ath_channelswitch_ie            *chanie = NULL;
    struct ieee80211_extendedchannelswitch_ie    *echanie = NULL;
    struct ieee80211_ie_wide_bw_switch           *widebwie = NULL;
    struct ieee80211_ie_sec_chan_offset          *secchanoff = NULL;
    struct ieee80211_max_chan_switch_time_ie     *mcst = NULL;
    u_int8_t                                     *cswarp = NULL;
    struct ieee80211_ath_channel*                    chan = NULL;
    u_int8_t                                     *htcap = NULL;
    u_int8_t                                     *htinfo = NULL;
    u_int8_t                                     *wme;
    struct ieee80211_frame                       *wh;
    u_int64_t                                    tsf;
    systime_t                                    previous_beacon_time;
    u_int8_t                                     *vhtcap = NULL;
    u_int8_t                                     *vhtop = NULL;
    u_int8_t                                     *opmode = NULL;
    u_int8_t                                     *bwnss_map = NULL;
    enum ieee80211_cwm_width                     ochwidth = ni->ni_chwidth;
    struct ieee80211_ie_whc_apinfo               *se_sonadv = NULL;
    u_int8_t                                     *hecap = NULL;
    u_int8_t                                     *hecap_6g = NULL;
    u_int8_t                                     *heop = NULL;
    u_int8_t                                     *muedca = NULL;
    u_int8_t                                     *mdie = NULL;
    u_int8_t                                     **tpe = NULL;
    struct wlan_objmgr_pdev                      *pdev;
    uint8_t                                      snr;
    struct son_beacon_frm_info                   beacon_info = {0};
#if DBDC_REPEATER_SUPPORT
    struct ieee80211_ie_extender                 *se_extender = NULL;
    struct global_ic_list *ic_list = ic->ic_global_list;
    struct ieee80211_mlme_priv    *mlme_priv;
    struct ieee80211vap *stavap;
#endif
    bool                                         peer_update_required = false;
    u_int8_t update_beacon                       = 0;

    pdev = ic->ic_pdev_obj;
    if(pdev == NULL) {
        qdf_print("%s : pdev is null", __func__);
        return;
    }

    wh = (struct ieee80211_frame *)wbuf_header(wbuf);
    OS_MEMCPY((u_int8_t *)&tsf, util_scan_entry_tsf(scan_entry), sizeof(tsf));
    capinfo = util_scan_entry_capinfo(scan_entry).value;

    /* FIX for EV# 98854: Preserving last successful beacon */
    previous_beacon_time = vap->iv_last_beacon_time;
    /*
     * When operating in station mode, check for state updates.
     * Be careful to ignore beacons received while doing a
     * background scan.  We consider only 11g/WMM stuff right now.
     */
    if ((ieee80211_node_get_associd(ni) != 0) &&
	  IEEE80211_ADDR_EQ(util_scan_entry_macaddr(scan_entry),ieee80211_node_get_bssid(ni))) {
        /*
         * record tsf of last beacon
         */
        OS_MEMCPY(ni->ni_tstamp.data, &tsf, sizeof(ni->ni_tstamp));

        /*
         * record absolute time of last beacon
         */
        vap->iv_last_beacon_time = OS_GET_TIMESTAMP();

        /*
         * check for ERP change
         */
        erp = util_scan_entry_erpinfo(scan_entry);
        if ((ni->ni_erp != erp) && (ieee80211_vaps_ready(ic, IEEE80211_M_HOSTAP) == 0)) {
            IEEE80211_NOTE(vap, IEEE80211_MSG_ASSOC, ni,
                           "erp change: was 0x%x, now 0x%x",
                           ni->ni_erp, erp);
            if (erp & IEEE80211_ERP_USE_PROTECTION)
                IEEE80211_ENABLE_PROTECTION(ic);
            else
                IEEE80211_DISABLE_PROTECTION(ic);
            IEEE80211_COMM_LOCK(ic);
            TAILQ_FOREACH(tmpvap, &(ic)->ic_vaps, iv_next) {
                ieee80211_vap_erpupdate_set(tmpvap);
            }
            IEEE80211_COMM_UNLOCK(ic);
            ieee80211_set_protmode(ic);
            ni->ni_erp = erp;
            if (ni->ni_erp & IEEE80211_ERP_LONG_PREAMBLE)
                IEEE80211_ENABLE_BARKER(ic);
            else
                IEEE80211_DISABLE_BARKER(ic);
        }

        /*
         * check for slot time change
         */
        if ((ni->ni_capinfo ^ capinfo) & IEEE80211_CAPINFO_SHORT_SLOTTIME) {
            IEEE80211_NOTE(vap, IEEE80211_MSG_ASSOC, ni,
                           "capabilities change: was 0x%x, now 0x%x",
                           ni->ni_capinfo, capinfo);
            /*
             * NB: we assume short preamble doesn't
             *     change dynamically
             */
            ieee80211_set_shortslottime(ic,
                                        IEEE80211_IS_CHAN_A(vap->iv_bsschan) ||
                                        IEEE80211_IS_CHAN_11NA(vap->iv_bsschan) ||
                                        (capinfo & IEEE80211_CAPINFO_SHORT_SLOTTIME));
            ni->ni_capinfo &= ~IEEE80211_CAPINFO_SHORT_SLOTTIME;
            ni->ni_capinfo |= capinfo & IEEE80211_CAPINFO_SHORT_SLOTTIME;
        }

        /*
         * check for tim
         */
        if (wlan_scan_can_transmit(wlan_vdev_get_pdev(vap->vdev_obj))) {
            tim_elm = util_scan_entry_tim(scan_entry);
            if (tim_elm != NULL) {
                struct ieee80211_ath_tim_ie    *tim = (struct ieee80211_ath_tim_ie *) tim_elm;
                struct vdev_mlme_proto_generic *mlme_generic = &ni->ni_vap->vdev_mlme->proto.generic;

                /* Also update vap's iv_dtim_period. OS may querry */
                mlme_generic->dtim_period = tim->tim_period;
                /* Only field iv_dtim_period was being updated, while iv_dtim_count was never changed.
                 * It was fine while nobody used it, but TDLS does.
                 * (AP code has a private copy of beacon to be transmitted, and updates DTIM count that
                 * copy instead of updating/reading the val    ue from the VAP
                 */
                ni->ni_vap->iv_dtim_count = tim->tim_count;
            }
        }

        /*
         * check for WMM parameters
         */
        if (((wme = util_scan_entry_wmeparam(scan_entry)) != NULL) ||
            ((wme = util_scan_entry_wmeinfo(scan_entry))  != NULL)) {
            int         _retval;
            u_int8_t    qosinfo;

            /* Node is WMM-capable if WME IE (either subtype) is present */
            ni->ni_ext_caps |= IEEE80211_NODE_C_QOS;

            ni->ni_wme_miss_threshold = 0;

            if (vap->iv_opmode != IEEE80211_M_BTAMP) {
            /* Parse IE according to subtype */
            if (iswmeparam(wme)) {
                _retval = ieee80211_parse_wmeparams(vap, wme, &qosinfo, 0);
            }
            else {
                _retval = ieee80211_parse_wmeinfo(vap, wme, &qosinfo);
            }

            /* Check whether WME parameters changed */
            if (_retval > 0) {
                /*
                 * Do not update U-APSD capability; this is done
                 * during association request.
                 */

                /* Check whether node is in a WMM connection */
                if (ni->ni_flags & IEEE80211_NODE_QOS) {
                    /* Update WME parameters  */
                    if (iswmeparam(wme)) {
                        ieee80211_wme_updateparams(vap);
                    }
                    else {
                        ieee80211_wme_updateinfo(vap);
                    }
                }

                /*
                 * If association used U-APSD and AP no longer
                 * supports it we must disassociate and renegotiate
                 * U-APSD support.
                 *
                 * We don't need to do anything if association did not
                 * use U-APSD and AP now supports it.
                 */
                if (ni->ni_flags & IEEE80211_NODE_UAPSD) {
                    if ((qosinfo & WME_CAPINFO_UAPSD_EN) == 0) {
                        ieee80211node_clear_flag(ni, IEEE80211_NODE_UAPSD);

                        /* Disassociate for unspecified QOS-related reason. */
                        ieee80211_mlme_recv_deauth(ni, IEEE80211_REASON_QOS);
                    }
                }
            }
            }
        } else {
            /* If WME IE not present node is not WMM capable */
            ni->ni_wme_miss_threshold++;
            if(ni->ni_wme_miss_threshold > 3 ) {
                ni->ni_ext_caps &= ~IEEE80211_NODE_C_QOS;

            /*
             * If association used U-APSD and AP no longer
             * supports it we must disassociate and renegotiate
             * U-APSD support.
             *
             * We don't need to do anything if association did not
             * use U-APSD and AP now supports it.
             */
                if (ni->ni_flags & IEEE80211_NODE_UAPSD) {
                    ieee80211node_clear_flag(ni, IEEE80211_NODE_UAPSD);

                    /* Disassociate for unspecified QOS-related reason. */
                    //ieee80211_mlme_recv_deauth(ni, IEEE80211_REASON_QOS);
                    qdf_nofl_info("AP no longer supports UAPSD, disconnecting from AP\n");
                    wlan_mlme_deauth_request(vap, ni->ni_macaddr, IEEE80211_REASON_QOS);
                }
            }
        }

        /*
         * check for spectrum management
         */
        if ((capinfo & IEEE80211_CAPINFO_SPECTRUM_MGMT) || ieee80211_ic_2g_csa_is_set(ic)) {
            chanie  = (struct ieee80211_ath_channelswitch_ie *) util_scan_entry_csa(scan_entry);
            echanie = (struct ieee80211_extendedchannelswitch_ie *) util_scan_entry_xcsa(scan_entry);
            widebwie = (struct ieee80211_ie_wide_bw_switch *) util_scan_entry_widebw(scan_entry);
            mcst = (struct ieee80211_max_chan_switch_time_ie *)util_scan_entry_mcst(scan_entry);
            secchanoff = (struct ieee80211_ie_sec_chan_offset *) util_scan_entry_secchanoff(scan_entry);
            cswarp =   util_scan_entry_cswrp(scan_entry);

            if(chanie || echanie) {
                chan = ieee80211_get_new_sw_chan (ni, chanie, echanie, secchanoff, widebwie, cswarp);
                if (chan) {
                    IEEE80211_DPRINTF_IC(ic, IEEE80211_VERBOSE_NORMAL, IEEE80211_MSG_EXTIOCTL_CHANSWITCH,
                                         "%s: Channel switch announcement received, switching to chan: %d freq: %d, mode/flags: 0x%llx\n",
                                         __func__, chan->ic_ieee, chan->ic_freq, chan->ic_flags);
                } else {
                    IEEE80211_DPRINTF_IC(ic, IEEE80211_VERBOSE_NORMAL, IEEE80211_MSG_EXTIOCTL_CHANSWITCH,
                                         "%s: Channel is not available, disconnect the STA vap.\n", __func__);
                    vap->iv_ni = ni;
                    OS_SET_TIMER(&vap->iv_disconnect_sta_timer,IEEE80211_TU_TO_MS(ni->ni_chanswitch_tbtt*ni->ni_intval));
                }
                ieee80211_mgmt_sta_send_csa_rx_nl_msg(ic, ni, chan, chanie, echanie, secchanoff, widebwie);

                /* MCST IE is sent along with chanie/echanie. Hence process mcst
                 * only if chanie or echanie is present.
                 * If MCSTIE is not sent by Root AP, set max_chanswitch_time to:
                 * 1. A very big value if target channel is DFS. This sets
                 * "ic->ic_has_rootap_done_cac" to false and RE AP does CAC on
                 * DFS chan.
                 * 2. Zero if target channel is non-DFS. This sets
                 * "ic->ic_has_rootap_done_cac" to true and ensures that RE's
                 * STA vap does not disconnect and scan.
                 */
                if (mcst){
                    ic->ic_mcst_of_rootap =
                        ieee80211_get_max_chan_switch_time(mcst);
                } else {
                    if (chan &&
                        !IEEE80211_IS_PRIMARY_OR_SECONDARY_CHAN_DFS(chan)) {
                        ic->ic_mcst_of_rootap = 0;
                    } else {
                        ic->ic_mcst_of_rootap = UNSIGNED_MAX_24BIT;
                    }
                }

                if(ic->ic_mcst_of_rootap <= ni->ni_intval) {
                    ic->ic_has_rootap_done_cac = true;
                }
            }
            ni->ni_capinfo |= IEEE80211_CAPINFO_SPECTRUM_MGMT;
        } else {
            ni->ni_capinfo &= ~IEEE80211_CAPINFO_SPECTRUM_MGMT;
        }

        /*
         * check HT IEs
         * Parse HT capabilities with VHT
         */
        if (IEEE80211_NODE_USE_HT(ni)) {
            htcap  = util_scan_entry_htcap(scan_entry);
            htinfo = util_scan_entry_htinfo(scan_entry);
            if (htcap) {
                ieee80211_parse_htcap(ni, htcap, &peer_update_required);
            }
            if (htinfo) {
                ieee80211_parse_htinfo(ni, htinfo);
            }
        }

        bwnss_map = util_scan_entry_bwnss_map(scan_entry);
        if (bwnss_map) {
            ni->ni_bw160_nss = IEEE80211_GET_BW_NSS_FWCONF_160(*(u_int32_t *)bwnss_map);
            ni->ni_bw80p80_nss = 0;
            ni->ni_prop_ie_used = true;
        } else {
            ni->ni_bw160_nss = 0;
            ni->ni_bw80p80_nss = 0;
        }

        if (IEEE80211_NODE_USE_VHT(ni)) {
            vhtcap = util_scan_entry_vhtcap(scan_entry);
            vhtop = util_scan_entry_vhtop(scan_entry);
            opmode = util_scan_entry_omn(scan_entry);

            if ((vhtcap != NULL) && (vhtop != NULL) && htinfo) {
                ieee80211_parse_vhtcap(ni, vhtcap, &peer_update_required);
                ieee80211_parse_vhtop(ni, vhtop, htinfo);
            }

            if (opmode) {
                ieee80211_parse_opmode_notify(ni, opmode, subtype);
            }
        }

        if (IEEE80211_NODE_USE_HE(ni)) {

            hecap = util_scan_entry_hecap(scan_entry);
            heop  = util_scan_entry_heop(scan_entry);
            hecap_6g = util_scan_entry_he_6g_cap(scan_entry);

            if (hecap != NULL) {
                ieee80211_parse_hecap(ni, hecap, subtype);
            }

            if (hecap_6g != NULL) {
                ieee80211_parse_he_6g_bandcap(ni, hecap_6g, subtype);
            }

            if (heop != NULL) {
                u_int32_t saved_ni_heop_param = ni->ni_he.heop_param;
                u_int32_t heop_info;
#if SUPPORT_11AX_D3
                u_int8_t saved_ni_heop_bsscolor_info =
                    ni->ni_he.heop_bsscolor_info;
#endif

                ieee80211_parse_heop(ni, heop, subtype, &update_beacon);

#if SUPPORT_11AX_D3
                if ((ni->ni_he.heop_param != saved_ni_heop_param) &&
                    (ni->ni_he.heop_bsscolor_info) == saved_ni_heop_bsscolor_info) {
                    heop_info = (ni->ni_he.heop_param |
                            (ni->ni_he.heop_bsscolor_info << HEOP_PARAM_S));
#else
                if (ni->ni_he.heop_param != saved_ni_heop_param) {
                    /* There is a change in the AP side HE OP params.
                     * We need to update target with these new params.
                     */
                    heop_info = ni->ni_he.heop_param;
#endif
                    if(ic->ic_vap_set_param &&
                        (ic->ic_vap_set_param(vap,
                          IEEE80211_CONFIG_HE_OP, heop_info) != EOK)) {

                        qdf_print("%s WARNING!!! heop set to target failed", __func__);
                    }
                }
            }
        }

        if((muedca = util_scan_entry_muedca(scan_entry)) != NULL) {

            ieee80211_parse_muedcaie(vap, muedca);
        }

        if (IEEE80211_NODE_USE_HT(ni) ||
            IEEE80211_NODE_USE_VHT(ni) ||
            IEEE80211_NODE_USE_HE(ni)) {

            if (IEEE80211_IS_CHAN_80MHZ(vap->iv_bsschan)) {
                /* Transitions: 80->40, 80->20, 40->80, 40->20, 20->80, 20->40 */
                if  ((ochwidth != ni->ni_chwidth) && (ni->ni_chwidth <= vap->iv_sta_negotiated_ch_width)) {
                    ic->ic_chwidth_change(ni);
                }
            } else if (IEEE80211_IS_CHAN_11N_HT40(vap->iv_bsschan) ||
                       IEEE80211_IS_CHAN_11AC_VHT40(vap->iv_bsschan)||
                       IEEE80211_IS_CHAN_11AX_HE40(vap->iv_bsschan)) {

               /* Transitions: 40->20, 20->40 */

                /*
                 * Check for AP changing channel width.
                 * Verify the AP is capable of 40 MHz as sometimes the AP
                 * can get into an inconsistent state (see EV 72018)
                 */
                if ((ni->ni_htcap & IEEE80211_HTCAP_C_CHWIDTH40) &&
                    (ochwidth != ni->ni_chwidth) && (ni->ni_chwidth <= vap->iv_sta_negotiated_ch_width)) {
                        u_int32_t  rxlinkspeed, txlinkspeed; /* bits/sec */
                        ic->ic_chwidth_change(ni);
                        mlme_get_linkrate(ni, &rxlinkspeed, &txlinkspeed);
                        IEEE80211_DELIVER_EVENT_LINK_SPEED(vap, rxlinkspeed, txlinkspeed);
                }
            }
        }

    }

    /*
     * Check BBSID before updating our beacon configuration to make
     * sure the received beacon is really from our AP.
     */
    if ((ni == vap->iv_bss) &&
        (IEEE80211_ADDR_EQ( util_scan_entry_bssid(scan_entry), ieee80211_node_get_bssid(ni)))) {
        //
        // Check ssid and phymode to make sure ESS/phymode broadcasted by the AP is the same to
        // which we're trying to connect. AP may have been reconfigured but
        // old entry remains in the scan list until aged out. Only perform phymode and channel check
        // if we are operating in auto mode.Perform the channel check and ignore the phymode check
        // if we are operating in forced mode.
        //
        if (ieee80211_ssid_match(scan_entry, ni)  &&
            ((!ieee80211_is_phymode_auto(vap->iv_des_mode) &&
              (ieee80211_channel_frequency(ni->ni_chan) == ieee80211_channel_frequency(wlan_util_scan_entry_channel(scan_entry)))) ||
              (ieee80211_phymode_match(scan_entry, ni) &&
               (ni->ni_chan == wlan_util_scan_entry_channel(scan_entry)))) ) {
            /* If fast bss transition enabled then track mdie in beacons */
            if (vap->iv_roam.iv_ft_enable) {
                if((mdie = util_scan_entry_mdie(scan_entry)) != NULL) {
                        qdf_mem_copy(vap->iv_roam.iv_mdie, mdie, MD_IE_LEN);
                } else {
                    if (vap->iv_roam.iv_mdie[1])
                        qdf_mem_zero(vap->iv_roam.iv_mdie, MD_IE_LEN);
                }
            } else {
                if (vap->iv_roam.iv_mdie[1])
                    qdf_mem_zero(vap->iv_roam.iv_mdie, MD_IE_LEN);
            }

            vap->iv_lastbcn_phymode_mismatch = 0;
            ieee80211_mlme_join_complete_infra(ni);
        }
        else {
         /* EV# 98854 Reverting the Beacon time stamp if scan_entry
            channel mismatches  */
            vap->iv_lastbcn_phymode_mismatch = 1;
            vap->iv_last_beacon_time = previous_beacon_time;
        }

        if ((wlan_vdev_is_up(vap->vdev_obj) == QDF_STATUS_SUCCESS) &&
           (subtype == IEEE80211_FC0_SUBTYPE_BEACON)) {
            ic->ic_beacon_update(ni, rs->rs_snr);
            ieee80211_beacon_chanutil_update(vap);
        }

    snr = ieee80211_get_snr(scan_entry, vap);
    se_sonadv = (struct ieee80211_ie_whc_apinfo *)util_scan_entry_sonie(scan_entry);
    beacon_info.se_sonadv = se_sonadv;
    beacon_info.snr = snr;
    if (son_update_mgmt_frame(vap->vdev_obj, ni->peer_obj, subtype, wbuf_header(wbuf), wbuf_get_pktlen(wbuf), &beacon_info) > 0) {
        son_pdev_appie_update(ic);
        update_beacon = 1;
    }

#if DBDC_REPEATER_SUPPORT
    if (ic_list->same_ssid_support) {
        se_extender = (struct ieee80211_ie_extender *)util_scan_entry_extenderie(scan_entry);
        if (se_extender) {
            if(ic->ic_sta_vap && (vap == ic->ic_sta_vap)) {
                stavap = ic->ic_sta_vap;
                mlme_priv = stavap->iv_mlme_priv;
                if (mlme_priv->im_connection_up) {
                    if (IEEE80211_ADDR_EQ(vap->iv_bss->ni_macaddr ,wh->i_addr3)) {
                        if ((ic_list->extender_info & ROOTAP_ACCESS_MASK) != ((se_extender->extender_info) & ROOTAP_ACCESS_MASK)) {
                            ic_list->extender_info &= ~ROOTAP_ACCESS_MASK;
                            ic_list->extender_info |= (se_extender->extender_info & ROOTAP_ACCESS_MASK);
                            update_beacon = 1;
                            if ((ic_list->extender_info & ROOTAP_ACCESS_MASK) == 0x0) {
                                ic_list->rootap_access_downtime = OS_GET_TIMESTAMP();
                                ieee80211_bringdown_secondary_stavap(ic);
                            }
                        }
                    }
                }
            }
        }
    }
#endif

        ieee80211_vap_compute_tsf_offset(vap, wbuf, rs);
        tpe = util_scan_entry_tpe(scan_entry);
        ieee80211_process_tpe_ie(ni, tpe);

        /*
        * Set or clear flag indicating reception of channel switch announcement
        * in this channel. This flag should be set before notifying the scan
        * algorithm.
        * We should not send probe requests on a channel on which CSA was
        * received until we receive another beacon without the said flag.
        */
        if (((chanie != NULL) || (echanie != NULL)) && chan && (chan != ic->ic_curchan)) {
                /* Current code do not handle CSA properly when DOTH is not enabled
                 * In this case, drop CSA to avoid undefined behavior.
                 */
                if (!(ieee80211_num_apvap_running(ic)) || ieee80211_ic_doth_is_set(ic))
                    ieee80211_process_csa(ni, chan, chanie, echanie, &update_beacon);
        } else {
            ic->ic_curchan->ic_flagext &= ~IEEE80211_CHAN_CSA_RECEIVED;
        }

	ieee80211_process_beacon_for_ru26_update(vap, scan_entry);

        if (peer_update_required) {
           /*
            * Based on the HT and VHT capability IEs, update the target with
            * with updated peer info.
            */
           if (wlan_vdev_is_up(vap->vdev_obj) == QDF_STATUS_SUCCESS) {
                if (ieee80211_validate_assoc_info(ni) == QDF_STATUS_SUCCESS) {
                    ic->ic_newassoc(ni, 0);
                } else {
                    qdf_rl_err("Not updating assoc parameters since assoc info validation failed");
                }
           }
        }

	if(update_beacon) {
	    wlan_pdev_beacon_update(ic);
        }
    }
}

/**
 * check wpa/rsn ie is present in the ie buffer passed in.
 */
bool ieee80211_check_wpaie(struct ieee80211vap *vap, u_int8_t *iebuf, u_int32_t length)
{
    struct wlan_crypto_params tmp_crypto_params;
    bool add_wpa_ie = true;
    u_int8_t *iebuf_end = iebuf + length;

    qdf_mem_zero(&tmp_crypto_params, sizeof(tmp_crypto_params));

    while (add_wpa_ie && ((iebuf+1) < iebuf_end )) {
        if (iebuf[0] == IEEE80211_ELEMID_VENDOR) {
            if (iswpaoui(iebuf) &&  (wlan_crypto_wpaie_check((struct wlan_crypto_params *)&tmp_crypto_params, iebuf) == 0)) {
                /* found WPA IE */
                add_wpa_ie = false;
            }
        } else if (iebuf[0] == IEEE80211_ELEMID_RSN) {
            if (wlan_crypto_rsnie_check((struct wlan_crypto_params *)&tmp_crypto_params, iebuf) == 0) {
                /* found RSN IE */
                add_wpa_ie = false;
            }
        }
        iebuf += iebuf[1] + 2;
        continue;
    }

    if (!add_wpa_ie) {
        struct wlan_crypto_params *vdev_crypto_params;
        vdev_crypto_params = wlan_crypto_vdev_get_crypto_params(vap->vdev_obj);
        if (!vdev_crypto_params)
            qdf_mem_copy(vdev_crypto_params, &tmp_crypto_params, sizeof(struct wlan_crypto_params));
    }
    return add_wpa_ie;
}

#if QCN_IE
static void
ieee80211_get_qcn_bsscolor_rept_info(struct ieee80211vap *vap)
{
    ol_ath_soc_softc_t *soc = NULL;
    struct wlan_objmgr_psoc_objmgr *psoc_objmgr = NULL;
    struct wlan_objmgr_pdev *pdev = NULL;
    struct ieee80211com *ic = NULL,*tmpic = NULL;
    struct ieee80211vap *tmp_vap = NULL;
    int soc_idx;
    bool apvap_on_other_radio = 0;
    bool apvap_on_same_radio = 0;
    int id =0;

    ic = vap->iv_ic;
    for (soc_idx = 0; soc_idx < ol_num_global_soc; soc_idx++) {
        soc = ol_global_soc[soc_idx];
        if (!soc || !(ic->ic_is_target_lithium(soc->psoc_obj))) {
            continue;
        }
        psoc_objmgr = &soc->psoc_obj->soc_objmgr;
        for (id=0;id<WLAN_UMAC_MAX_PDEVS;id++) {
            pdev = psoc_objmgr->wlan_pdev_list[id];
            if (pdev) {
                tmpic = wlan_pdev_get_mlme_ext_obj(pdev);
                if (tmpic) {
                    TAILQ_FOREACH(tmp_vap, &tmpic->ic_vaps, iv_next) {
                        if (tmp_vap->iv_opmode == IEEE80211_M_HOSTAP) {
                            if (ic == tmpic) {
                                apvap_on_same_radio = 1;
                            } else {
                                apvap_on_other_radio = 1;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    if (apvap_on_same_radio && ic->ic_rept_clients) {
        vap->iv_bsscolor_rptr_info = QCN_REPT_BH_APVAP_ON_SAME_RADIO;
    } else if (apvap_on_same_radio && !ic->ic_rept_clients) {
        vap->iv_bsscolor_rptr_info = QCN_REPT_FH_APVAP_ON_SAME_RADIO;
    } else if (apvap_on_other_radio) {
        vap->iv_bsscolor_rptr_info = QCN_REPT_APVAP_ON_OTHER_RADIO;
    } else {
        vap->iv_bsscolor_rptr_info = QCN_REPT_ONLY_STAVAP;
    }
    return;
}
#endif
/*
 * Setup an association/reassociation request,
 * and returns the frame length.
 */
static u_int16_t
ieee80211_setup_assoc(
    struct ieee80211_node *ni,
    struct ieee80211_frame *wh,
    int reassoc,
    u_int8_t *previous_bssid
    )
{
    struct ieee80211vap *vap = ni->ni_vap;
    struct ieee80211com *ic = ni->ni_ic;
    u_int8_t *frm;
    u_int16_t capinfo = 0;
    struct ieee80211_bwnss_map nssmap;
    u_int16_t ie_len;
    u_int8_t subtype = reassoc ? IEEE80211_FC0_SUBTYPE_REASSOC_REQ :
        IEEE80211_FC0_SUBTYPE_ASSOC_REQ;
    bool  add_wpa_ie;
    u_int8_t rx_chainmask = ieee80211com_get_rx_chainmask(ic);
#if DBDC_REPEATER_SUPPORT
    struct global_ic_list *ic_list = ic->ic_global_list;
#endif

    qdf_mem_zero(&nssmap, sizeof(nssmap));

    ieee80211_send_setup(vap, ni, wh,
                         (IEEE80211_FC0_TYPE_MGT | subtype),
                         vap->iv_myaddr, ni->ni_macaddr, ni->ni_bssid);
    frm = (u_int8_t *)&wh[1];

    /*
     * asreq frame format
     *[2] capability information
     *[2] listen interval
     *[6*] current AP address (reassoc only)
     *[tlv] ssid
     *[tlv] supported rates
     *[4] power capability
     *[28] supported channels element
     *[tlv] extended supported rates
     *[tlv] WME [if enabled and AP capable]
     *[tlv] HT Capabilities
     *[tlv] VHT Capabilities
     *[tlv] Atheros advanced capabilities
     *[tlv] user-specified ie's
     *[tlv] Bandwidth-NSS mapping
     */
    if (vap->iv_opmode == IEEE80211_M_STA || vap->iv_opmode == IEEE80211_M_BTAMP)
        capinfo |= IEEE80211_CAPINFO_ESS;
    else
        ASSERT(0);

    if (IEEE80211_VAP_IS_PRIVACY_ENABLED(vap))
        capinfo |= IEEE80211_CAPINFO_PRIVACY;
    /*
     * NB: Some 11a AP's reject the request when
     *     short premable is set.
     */
    if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) &&
        IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan))
        capinfo |= IEEE80211_CAPINFO_SHORT_PREAMBLE;
    if ((ni->ni_capinfo & IEEE80211_CAPINFO_SHORT_SLOTTIME) &&
        (ic->ic_flags & IEEE80211_F_SHSLOT))
        capinfo |= IEEE80211_CAPINFO_SHORT_SLOTTIME;
    if (ieee80211_ic_doth_is_set(ic) && ieee80211_vap_doth_is_set(vap))
        capinfo |= IEEE80211_CAPINFO_SPECTRUM_MGMT;
    if (ieee80211_vap_rrm_is_set(vap))
        capinfo |= IEEE80211_CAPINFO_RADIOMEAS;

    *(u_int16_t *)frm = htole16(capinfo);
    frm += 2;

    *(u_int16_t *)frm = htole16(ic->ic_lintval);
    frm += 2;

    if (reassoc) {
        IEEE80211_ADDR_COPY(frm, previous_bssid);
        frm += QDF_MAC_ADDR_SIZE;
    }

    frm = ieee80211_add_ssid(frm,
                             ni->ni_essid,
                             ni->ni_esslen);
    frm = ieee80211_add_rates(vap, frm, &ni->ni_rates);

    /*
     * DOTH elements
     */
    if (ieee80211_ic_doth_is_set(ic) && ieee80211_vap_doth_is_set(vap)) {
        frm = ieee80211_add_doth(frm, vap);
    }

    if (IEEE80211_VAP_IS_WDS_ENABLED(vap) &&
        son_vdev_fext_capablity(vap->vdev_obj,
                                SON_CAP_GET,
                                WLAN_VDEV_FEXT_SON_SPL_RPT)) {
        ieee80211_add_or_retrieve_ie_from_app_opt_ies(vap, IEEE80211_FRAME_TYPE_ASSOCREQ,
                                                      IEEE80211_ELEMID_VENDOR, IEEE80211_ELEMID_VENDOR_SON_REPT,
                                                      &frm, TYPE_APP_IE_BUF, NULL, true);
    }
    add_wpa_ie=true;
    /*
     * check if os shim has setup RSN IE it self.
     */
    IEEE80211_VAP_LOCK(vap);
    if (vap->iv_opt_ie.length) {
        add_wpa_ie = ieee80211_check_wpaie(vap, vap->iv_opt_ie.ie,
                                           vap->iv_opt_ie.length);
    }
    if (add_wpa_ie) {
        /* Continue looking for the WPA IE */
        add_wpa_ie = ieee80211_mlme_app_ie_check_wpaie(vap,IEEE80211_FRAME_TYPE_ASSOCREQ);
    }
    IEEE80211_VAP_UNLOCK(vap);

    if (add_wpa_ie) {
        if (wlan_crypto_vdev_has_auth_mode(vap->vdev_obj, (1 << WLAN_CRYPTO_AUTH_RSNA))) {
            frm = wlan_crypto_build_rsnie(vap->vdev_obj, frm, (struct qdf_mac_addr*)ni->ni_bssid);
            if (!frm) {
                return 0;
            }
        }
    }

    /*Add RM Enabled capabilities IE*/
    if (vap->iv_rrm_cap_ie)
        frm = ieee80211_add_rrm_cap_ie(frm, ni);

#if ATH_SUPPORT_WAPI
    if (wlan_crypto_vdev_has_auth_mode(vap->vdev_obj, (1 << WLAN_CRYPTO_AUTH_WAPI)))
    {
        frm = ieee80211_setup_wapi_ie(vap, frm);
        if (!frm) {
            return 0;
        }
    }
#endif

    frm = ieee80211_add_xrates(vap, frm, &ni->ni_rates);
	if (!reassoc) {
	    wlan_set_tspecActive(vap, 0);
	}

    if (vap->iv_ccx_evtable && vap->iv_ccx_evtable->wlan_ccx_set_vperf) {
        vap->iv_ccx_evtable->wlan_ccx_set_vperf(vap->iv_ccx_arg, 0);
    }


    /*
     * XXX: don't do WMM association with safe mode enabled, because
     * Vista OS doesn't know how to decrypt QoS frame.
     */
    if (ieee80211_vap_wme_is_set(vap) &&
        (ni->ni_ext_caps & IEEE80211_NODE_C_QOS) &&
        !IEEE80211_VAP_IS_SAFEMODE_ENABLED(vap)) {
        struct ieee80211_wme_tspec *sigtspec = &ni->ni_ic->ic_sigtspec;
        struct ieee80211_wme_tspec *datatspec = &ni->ni_ic->ic_datatspec;
        u_int8_t    tsrsiev[16];
        u_int8_t    tsrsvlen = 0;
        u_int32_t   minphyrate;

        frm = ieee80211_add_wmeinfo(frm, ni, WME_INFO_OUI_SUBTYPE, NULL, 0);
        if (iswmetspec((u_int8_t *)sigtspec)) {
            frm = ieee80211_add_wmeinfo(frm, ni, WME_TSPEC_OUI_SUBTYPE,
                                        &sigtspec->ts_tsinfo[0],
                                        sizeof (struct ieee80211_wme_tspec) - offsetof(struct ieee80211_wme_tspec, ts_tsinfo));
#if AH_UNALIGNED_SUPPORTED
            minphyrate = __get32(&sigtspec->ts_min_phy[0]);
#else
            minphyrate = *((u_int32_t *) &sigtspec->ts_min_phy[0]);
#endif
            if (vap->iv_ccx_evtable && vap->iv_ccx_evtable->wlan_ccx_fill_tsrsie) {
                vap->iv_ccx_evtable->wlan_ccx_fill_tsrsie(vap->iv_ccx_arg,
                                ((struct ieee80211_tsinfo_bitmap *) &sigtspec->ts_tsinfo[0])->tid,
                                minphyrate, &tsrsiev[0], &tsrsvlen);
            }
            if (tsrsvlen > 0) {
                *frm++ = IEEE80211_ELEMID_VENDOR;
                *frm++ = tsrsvlen;
                qdf_mem_copy(frm, &tsrsiev[0], tsrsvlen);
                frm += tsrsvlen;
            }
        }
        if (iswmetspec((u_int8_t *)datatspec)){
            frm = ieee80211_add_wmeinfo(frm, ni, WME_TSPEC_OUI_SUBTYPE,
                                        &datatspec->ts_tsinfo[0],
                                        sizeof (struct ieee80211_wme_tspec) - offsetof(struct ieee80211_wme_tspec, ts_tsinfo));
#if AH_UNALIGNED_SUPPORTED
            minphyrate = __get32(&datatspec->ts_min_phy[0]);
#else
            minphyrate = *((u_int32_t *) &datatspec->ts_min_phy[0]);
#endif
            if (vap->iv_ccx_evtable && vap->iv_ccx_evtable->wlan_ccx_fill_tsrsie) {
                vap->iv_ccx_evtable->wlan_ccx_fill_tsrsie(vap->iv_ccx_arg,
                                ((struct ieee80211_tsinfo_bitmap *) &datatspec->ts_tsinfo[0])->tid,
                                minphyrate, &tsrsiev[0], &tsrsvlen);
            }
            if (tsrsvlen > 0) {
                *frm++ = IEEE80211_ELEMID_VENDOR;
                *frm++ = tsrsvlen;
                qdf_mem_copy(frm, &tsrsiev[0], tsrsvlen);
                frm += tsrsvlen;
            }
    }
    }

#if UMAC_SUPPORT_WNM
    if (ieee80211_vap_wnm_is_set(vap)) {
        frm = ieee80211_add_timreq_ie(vap, ni, frm);
    }
#endif

    if ((ni->ni_flags & IEEE80211_NODE_HT) &&
        (!IEEE80211_IS_CHAN_6GHZ(ic->ic_curchan)) &&
        (IEEE80211_IS_CHAN_11AX(ic->ic_curchan) ||
         IEEE80211_IS_CHAN_11AC(ic->ic_curchan) ||
         IEEE80211_IS_CHAN_11N(ic->ic_curchan)) && ieee80211vap_htallowed(vap)) {
        frm = ieee80211_add_htcap(frm, ni, subtype);

        if (IEEE80211_IS_HTVIE_ENABLED(ic)) {
            frm = ieee80211_add_htcap_vendor_specific(frm, ni, subtype);
        }
    }

    /* Add extended capbabilities, if applicable */
    frm = ieee80211_add_extcap(frm, ni, IEEE80211_FC0_SUBTYPE_ASSOC_REQ);

    /*
     * VHT IEs
     */
      /* Add vht cap for 2.4G mode, if 256QAM is enabled */
    if ((IEEE80211_IS_CHAN_11AX(ic->ic_curchan) ||
         IEEE80211_IS_CHAN_11AC(ic->ic_curchan) ||
         IEEE80211_IS_CHAN_11NG(ic->ic_curchan))) {

        if(!IEEE80211_IS_CHAN_6GHZ(ic->ic_curchan) &&
                ieee80211vap_vhtallowed(vap)) {
            /* Add VHT capabilities IE */
            frm = ieee80211_add_vhtcap(frm, ni, ic,
                                IEEE80211_FC0_SUBTYPE_ASSOC_REQ, NULL, NULL);
        }

        /* Add optional VHT opmode notify if sta is configured to operate on VHT40/VHT20 */
        switch(vap->iv_des_mode) {
            case IEEE80211_MODE_11AC_VHT20:
            case IEEE80211_MODE_11AC_VHT40PLUS:
            case IEEE80211_MODE_11AC_VHT40MINUS:
            case IEEE80211_MODE_11AC_VHT40:
            case IEEE80211_MODE_11AXA_HE20:
            case IEEE80211_MODE_11AXA_HE40PLUS:
            case IEEE80211_MODE_11AXA_HE40MINUS:
            case IEEE80211_MODE_11AXA_HE40:
                frm = ieee80211_add_opmode_notify(frm, ni, ic, subtype);
                break;

            default:
                /* Not adding opmode notify IE */
                break;
        }
    }

    /*
     * VHT Interop IEs for 2.4NG mode, if 256QAM & 11NG vht interop is enabled
     */
    if (IEEE80211_IS_CHAN_11NG(ic->ic_curchan) && ieee80211vap_vhtallowed(vap)
                        && ieee80211vap_11ng_vht_interopallowed(vap)) {
        /* Add Vendor specific VHT Interop IE with Vht cap & Vht op IE*/
        frm = ieee80211_add_interop_vhtcap(frm, ni, ic, IEEE80211_FC0_SUBTYPE_ASSOC_REQ);
    }

    if (ieee80211_vap_wme_is_set(vap) &&  IEEE80211_IS_CHAN_11AX(ic->ic_curchan)
         && ieee80211vap_heallowed(vap)) {

        /* Add HE Capabilities IE */
        frm = ieee80211_add_hecap(frm, ni, ic, IEEE80211_FC0_SUBTYPE_ASSOC_REQ);

        if(IEEE80211_IS_CHAN_6GHZ(ic->ic_curchan)) {
            /* Add HE 6GHz Band Capbilities IE */
            frm = ieee80211_add_6g_bandcap(frm, ni, ic,
                            IEEE80211_FC0_SUBTYPE_ASSOC_REQ);
        }

    }

    /* Insert ieee80211_ie_ath_extcap IE to beacon */
    if ((ni->ni_flags & IEEE80211_NODE_ATH) &&
        ic->ic_ath_extcap) {
        u_int16_t ath_extcap = 0;
        u_int8_t  ath_rxdelim = 0;

        if (ieee80211_has_weptkipaggr(ni)) {
                ath_extcap |= IEEE80211_ATHEC_WEPTKIPAGGR;
                ath_rxdelim = ic->ic_weptkipaggr_rxdelim;
        }

        if ((ni->ni_flags & IEEE80211_NODE_OWL_WDSWAR) &&
            (ic->ic_ath_extcap & IEEE80211_ATHEC_OWLWDSWAR)) {
                ath_extcap |= IEEE80211_ATHEC_OWLWDSWAR;
        }

        if (ieee80211com_has_extradelimwar(ic))
            ath_extcap |= IEEE80211_ATHEC_EXTRADELIMWAR;

        frm = ieee80211_add_athextcap(frm, ath_extcap, ath_rxdelim);
    }

    /*
     * add wpa/rsn ie if os did not setup one.
     */
    if (add_wpa_ie) {
        int32_t authmode = 0;
        authmode = wlan_crypto_get_param(vap->vdev_obj, WLAN_CRYPTO_PARAM_AUTH_MODE);
        if ( authmode == -1 ) {
            qdf_err("crypto_err while getting authmode params\n");
            return -1;
        }

        if (authmode & (1 << WLAN_CRYPTO_AUTH_WPA)) {
            frm = wlan_crypto_build_wpaie(vap->vdev_obj, frm);
            if (!frm) {
                return 0;
            }
        }

        if (authmode & (1 << WLAN_CRYPTO_AUTH_CCKM)) {
            /*
             * CCKM AKM can be either added to WPA IE or RSN IE,
             * depending on the AP's configuration
             */
            if (authmode & (1 << WLAN_CRYPTO_AUTH_RSNA))
                frm = wlan_crypto_build_rsnie(vap->vdev_obj, frm, (struct qdf_mac_addr*)ni->ni_bssid);
            else if (authmode & (1 << WLAN_CRYPTO_AUTH_WPA))
                frm = wlan_crypto_build_wpaie(vap->vdev_obj, frm);

            if (!frm) {
                return 0;
            }
        }
    }

    if (!(vap->iv_ext_nss_support) && !(ic->ic_disable_bwnss_adv) && !ieee80211_get_bw_nss_mapping(vap, &nssmap, rx_chainmask)) {
        frm = ieee80211_add_bw_nss_maping(frm, &nssmap);
    }

    IEEE80211_VAP_LOCK(vap);

    if (!reassoc) {
        if (vap->iv_roam.iv_ft_enable) {
            if(vap->iv_roam.iv_mdie[1]) {
                qdf_mem_copy(frm, vap->iv_roam.iv_mdie, MD_IE_LEN);
                frm += MD_IE_LEN;
            }
        }
    }

    if (vap->iv_opt_ie.length){
        if(!vap->iv_roam.iv_ft_roam) {
            OS_MEMCPY(frm, vap->iv_opt_ie.ie,
                      vap->iv_opt_ie.length);
            frm += vap->iv_opt_ie.length;
        }
    }

    /* Add the Application IE's */
    frm = ieee80211_mlme_app_ie_append(vap, IEEE80211_FRAME_TYPE_ASSOCREQ, frm);

    /* Add SON IE */
    if (IEEE80211_VAP_IS_WDS_ENABLED(vap) &&
            !son_vdev_map_capability_get(vap->vdev_obj, SON_MAP_CAPABILITY)) {
        ie_len = __ieee80211_add_or_retrieve_ie_from_app_opt_ies(vap, IEEE80211_FRAME_TYPE_ASSOCREQ,
                                                               IEEE80211_ELEMID_VENDOR, IEEE80211_ELEMID_VENDOR_SON_AP,
                                                               &frm, TYPE_APP_IE_BUF, true);
    }
    IEEE80211_VAP_UNLOCK(vap);
#if DBDC_REPEATER_SUPPORT
    if (ic_list->same_ssid_support && (vap == ic->ic_sta_vap)) {
        /* Add the Extender IE */
        frm = ieee80211_add_extender_ie(vap, IEEE80211_FRAME_TYPE_ASSOCREQ, frm);
    }
#endif

#if QCN_IE
    if ((vap == ic->ic_sta_vap) && IEEE80211_VAP_IS_WDS_ENABLED(vap)) {
        ieee80211_get_qcn_bsscolor_rept_info(vap);
    }
    frm = ieee80211_add_qcn_info_ie(frm, vap, &ie_len,
            QCN_MAC_PHY_PARAM_IE_TYPE, NULL);
    if (vap->iv_bsscolor_rptr_info) {
        /*clear the flag, to avoid adding bsscolor attribute on other frames*/
        vap->iv_bsscolor_rptr_info = 0;
    }
#endif

    frm = ieee80211_add_generic_vendor_capabilities_ie(frm, ic);
    if (!frm)
        return 0;

    return (frm - (u_int8_t *)wh);
}

/*
 * Send an assoc/reassoc request frame
 */
int
ieee80211_send_assoc(struct ieee80211_node *ni,
                     int reassoc, u_int8_t *prev_bssid)
{
    struct ieee80211vap *vap = ni->ni_vap;
    struct ieee80211com *ic = ni->ni_ic;
    wbuf_t wbuf;
    u_int16_t length;

    wbuf = wbuf_alloc(ic->ic_osdev, WBUF_TX_MGMT, MAX_TX_RX_PACKET_SIZE);
    if (wbuf == NULL)
        return -ENOMEM;

    length = ieee80211_setup_assoc(ni, (struct ieee80211_frame *)wbuf_header(wbuf),
                                   reassoc, prev_bssid);
    if (!length) {
        wbuf_release(ic->ic_osdev, wbuf);
        return -EINVAL;
    }

    wbuf_set_pktlen(wbuf, length);
    /* Callback to allow OS layer to copy assoc/reassoc frame (Vista requirement) */
    IEEE80211_DELIVER_EVENT_MLME_ASSOC_REQ(vap, wbuf);

    return ieee80211_send_mgmt(vap,ni,wbuf,false);
}

int
wlan_send_addts(wlan_if_t vaphandle, u_int8_t *macaddr, ieee80211_tspec_info *tsinfo)
{
    struct ieee80211vap *vap = vaphandle;
    struct ieee80211_node *ni;
    struct ieee80211_action_mgt_args addts_args;
    struct ieee80211_action_mgt_buf  addts_buf;
    struct ieee80211_tsinfo_bitmap *tsflags;
    struct ieee80211_wme_tspec *tspec;

    ni = ieee80211_find_txnode(vap, macaddr, WLAN_MGMT_HANDLER_ID);
    if (ni == NULL) {
        IEEE80211_DPRINTF(vap, IEEE80211_MSG_OUTPUT,
                          "%s: could not send ADDTS, no node found for %s\n",
                          __func__, ether_sprintf(macaddr));
        return -EINVAL;
    }

    /*
     * ieee80211_action_mgt_args is a generic structure. TSPEC IE
     * is filled in the buf area.
     */
    addts_args.category = IEEE80211_ACTION_CAT_WMM_QOS;
    addts_args.action   = IEEE80211_WMM_QOS_ACTION_SETUP_REQ;
    addts_args.arg1     = IEEE80211_WMM_QOS_DIALOG_SETUP; /* dialogtoken */
    addts_args.arg2     = 0; /* status code */
    addts_args.arg3     = sizeof(struct ieee80211_wme_tspec);
    tspec = (struct ieee80211_wme_tspec *) &addts_buf.buf;
    tsflags = (struct ieee80211_tsinfo_bitmap *) &(tspec->ts_tsinfo);
    tsflags->direction = tsinfo->direction;
    tsflags->psb = tsinfo->psb;
    tsflags->dot1Dtag = tsinfo->dot1Dtag;
    tsflags->tid = tsinfo->tid;
    tsflags->reserved3 = tsinfo->aggregation;
    tsflags->one = tsinfo->acc_policy_edca;
    tsflags->zero = tsinfo->acc_policy_hcca;
    tsflags->reserved1 = tsinfo->traffic_type;
    tsflags->reserved2 = tsinfo->ack_policy;
#if AH_UNALIGNED_SUPPORTED
    __put16(&tspec->ts_nom_msdu[0], tsinfo->norminal_msdu_size);
    __put16(&tspec->ts_max_msdu[0], tsinfo->max_msdu_size);
    __put32(&tspec->ts_min_svc[0], tsinfo->min_srv_interval);
    __put32(&tspec->ts_max_svc[0], tsinfo->max_srv_interval);
    __put32(&tspec->ts_inactv_intv[0], tsinfo->inactivity_interval);
    __put32(&tspec->ts_susp_intv[0], tsinfo->suspension_interval);
    __put32(&tspec->ts_start_svc[0], tsinfo->srv_start_time);
    __put32(&tspec->ts_min_rate[0], tsinfo->min_data_rate);
    __put32(&tspec->ts_mean_rate[0], tsinfo->mean_data_rate);
    __put32(&tspec->ts_max_burst[0], tsinfo->max_burst_size);
    __put32(&tspec->ts_min_phy[0], tsinfo->min_phy_rate);
    __put32(&tspec->ts_peak_rate[0], tsinfo->peak_data_rate);
    __put32(&tspec->ts_delay[0], tsinfo->delay_bound);
    __put16(&tspec->ts_surplus[0], tsinfo->surplus_bw);
    __put16(&tspec->ts_medium_time[0], 0);
#else
    *((u_int16_t *) &tspec->ts_nom_msdu) = tsinfo->norminal_msdu_size;
    *((u_int16_t *) &tspec->ts_max_msdu) = tsinfo->max_msdu_size;
    *((u_int32_t *) &tspec->ts_min_svc) = tsinfo->min_srv_interval;
    *((u_int32_t *) &tspec->ts_max_svc) = tsinfo->max_srv_interval;
    *((u_int32_t *) &tspec->ts_inactv_intv) = tsinfo->inactivity_interval;
    *((u_int32_t *) &tspec->ts_susp_intv) = tsinfo->suspension_interval;
    *((u_int32_t *) &tspec->ts_start_svc) = tsinfo->srv_start_time;
    *((u_int32_t *) &tspec->ts_min_rate) = tsinfo->min_data_rate;
    *((u_int32_t *) &tspec->ts_mean_rate) = tsinfo->mean_data_rate;
    *((u_int32_t *) &tspec->ts_max_burst) = tsinfo->max_burst_size;
    *((u_int32_t *) &tspec->ts_min_phy) = tsinfo->min_phy_rate;
    *((u_int32_t *) &tspec->ts_peak_rate) = tsinfo->peak_data_rate;
    *((u_int32_t *) &tspec->ts_delay) = tsinfo->delay_bound;
    *((u_int16_t *) &tspec->ts_surplus) = tsinfo->surplus_bw;
    *((u_int16_t *) &tspec->ts_medium_time) = 0;
#endif

    ieee80211_send_action(ni, &addts_args, &addts_buf);
    ieee80211_free_node(ni, WLAN_MGMT_HANDLER_ID);    /* reclaim node */
    return 0;
}

