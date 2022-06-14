#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <string.h>
#include <stdio.h>
#include <net/if_arp.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/stat.h>
#include <acfg_types.h>
#include <acfg_api_pvt.h>
#include <linux/un.h>
#include <acfg_security.h>
#include <acfg_wsupp_types.h>
#include <acfg_misc.h>

#define MAX_SIZE    300
#define SUPP_FIELD  3
#define CMD_LENGTH  255
#define MAX_CIPHER_SIZE 128
#define MAX_CONF_FILE_NAME_LEN 128
/* Increament the  acfg_hapd_param_list  index in acfg_set_hapd_config_params() */
#define CHECK_AND_INCREMENT(i)	 if( ++i > ACFG_MAX_HAPD_CONFIG_PARAM ) { \
        acfg_log_errstr("%s: hostapd config array overflow\n", __func__);\
        return QDF_STATUS_E_FAILURE;}
#define HAPD_CONF_FILE_UPDATE 0

uint32_t acfg_wlan_app_iface_down(acfg_wlan_profile_vap_params_t *vap_params);

/* Extern declarations */
extern int wsupp_status_init;
extern int acfg_ctrl_iface_present (uint8_t *ifname, acfg_opmode_t opmode);
extern uint32_t acfg_get_br_name(uint8_t *ifname, char *brname);
extern struct socket_context g_sock_ctx;

void acfg_parse_hapd_param(char *buf, int32_t len,
        acfg_hapd_param_t *hapd_param)
{
    int32_t offset = 0;

    while (offset < len) {
        if (!strncmp(buf + offset, "wps_state=", strlen("wps_state="))) {
            offset += strlen("wps_state=");
            if (!strncmp(buf + offset, "disabled", strlen("disabled"))) {
                hapd_param->wps_state = WPS_FLAG_DISABLED;
                offset += strlen("disabled");
            } else if (!strncmp(buf + offset, "not configured",
                        strlen("not configured")))
            {
                hapd_param->wps_state = WPS_FLAG_UNCONFIGURED;
                offset += strlen("not configured");
            } else if (!strncmp(buf + offset, "configured",
                        strlen("configured")))
            {
                hapd_param->wps_state = WPS_FLAG_CONFIGURED;
                offset += strlen("configured");
            }
        } else {
            while ((*(buf + offset) != '\n') && (offset < len )) {
                offset++;
            }
            offset++;
        }
    }
}

uint32_t
acfg_get_hapd_params(acfg_wlan_profile_vap_params_t *vap_params,
        acfg_hapd_param_t *hapd_param)
{
    char cmd[255], replybuf[1024];
    uint32_t len = sizeof (replybuf);

    memset(replybuf, '\0', sizeof (replybuf));
    acfg_os_snprintf(cmd, sizeof(cmd), "%s", WPA_HAPD_GET_CONFIG_CMD_PREFIX);

    if(acfg_ctrl_req(vap_params->vap_name, cmd, strlen(cmd),
            replybuf, &len, ACFG_OPMODE_HOSTAP) < 0){
        return QDF_STATUS_E_FAILURE;
    }
    acfg_parse_hapd_param(replybuf, len, hapd_param);

    return QDF_STATUS_SUCCESS;
}

void
acfg_get_security_state(acfg_wlan_profile_vap_params_t *vap_params,
        acfg_wlan_profile_vap_params_t * cur_vap_params,
        uint32_t *state)
{
    acfg_wlan_profile_security_params_t sec_params, cur_sec_params;
    uint8_t set_sec = 0, cur_set_sec = 0;
    acfg_hapd_param_t hapd_param;

    memset((char *)&hapd_param, 0, sizeof (acfg_hapd_param_t));

    sec_params = vap_params->security_params;
    cur_sec_params = cur_vap_params->security_params;

    if (ACFG_IS_SEC_ENABLED(sec_params.sec_method)) {
        set_sec = 1;
    }
    if (ACFG_IS_SEC_ENABLED(cur_sec_params.sec_method)) {
        cur_set_sec = 1;
    }
    if (set_sec && cur_set_sec) {
        if (ACFG_SEC_CMP(vap_params, cur_vap_params)) {
            *state = ACFG_SEC_MODIFY_SECURITY_PARAMS;
        } else {
            *state = ACFG_SEC_UNCHANGED;
        }
        if (ACFG_WPS_CMP(vap_params, cur_vap_params))
        {
            *state = ACFG_SEC_RESET_SECURITY;
        }
    } else if (set_sec && !cur_set_sec) {
        *state = ACFG_SEC_SET_SECURITY;
        if (acfg_ctrl_iface_present(vap_params->vap_name,
                    ACFG_OPMODE_HOSTAP) == 1)
        {
            *state = ACFG_SEC_RESET_SECURITY;
        }
    } else if (!set_sec && cur_set_sec) {
        *state = ACFG_SEC_RESET_SECURITY;
    } else if (!set_sec && !cur_set_sec) {
        if (cur_vap_params->default_params_set == 1) {
            *state = ACFG_SEC_SET_SECURITY;
        } else {
            *state = ACFG_SEC_UNCHANGED;
        }
        if (ACFG_IS_VALID_WPS(vap_params->security_params)) {
            if (!ACFG_IS_VALID_WPS(cur_vap_params->security_params))
            {
                *state = ACFG_SEC_SET_SECURITY;
            } else if (ACFG_IS_VALID_WPS(cur_vap_params->security_params))
            {
                int res = 0;
                /*It is open authentication with WPS enabled. Check for
                  any modification*/
                res = acfg_get_open_wep_state(vap_params, cur_vap_params);
                if (res == 1) {
                    *state = ACFG_SEC_RESET_SECURITY;
                } else if (res == 2) {
                    *state = ACFG_SEC_MODIFY_SECURITY_PARAMS;
                } else {
                    *state = ACFG_SEC_UNCHANGED;
                }
            }
        } else {
            if (ACFG_IS_VALID_WPS(cur_vap_params->security_params)) {
                *state = ACFG_SEC_DISABLE_SECURITY;
            }
        }
    }
}

uint32_t
acfg_set_auth_open(acfg_wlan_profile_vap_params_t *vap_params,
        uint32_t state)
{
    int flag = 0;
    uint32_t status = QDF_STATUS_SUCCESS;

    if ((state  == ACFG_SEC_SET_SECURITY) ||
            (state == ACFG_SEC_DISABLE_SECURITY) ||
            (state == ACFG_SEC_DISABLE_SECURITY_CHANGED) ||
            (state == ACFG_SEC_RESET_SECURITY))
    {

        flag |= ACFG_ENCODE_DISABLED;
        flag |= ACFG_ENCODE_OPEN;
        status = acfg_set_enc(vap_params->vap_name, flag, "off");
        if (status != QDF_STATUS_SUCCESS) {
            acfg_log_errstr("Failed to set enc\n");
            return QDF_STATUS_E_FAILURE;
        }
        status = acfg_set_vap_param(vap_params->vap_name,
                ACFG_PARAM_AUTHMODE, 1);
        if (status != QDF_STATUS_SUCCESS) {
            acfg_log_errstr("Failed Set vap param\n");
            return QDF_STATUS_E_FAILURE;
        }
        status = acfg_set_vap_param(vap_params->vap_name,
                ACFG_PARAM_DROPUNENCRYPTED, 0);
        if (status != QDF_STATUS_SUCCESS) {
            acfg_log_errstr("Failed Set vap param\n");
            return QDF_STATUS_E_FAILURE;
        }
    }
    return status;
}

void
acfg_get_cipher_str(acfg_wlan_profile_cipher_meth_e cipher_method,
        char *cipher)
{
    if(cipher_method & ACFG_WLAN_PROFILE_CIPHER_METH_TKIP)
    {
        acfg_os_strlcat(cipher, " TKIP", MAX_CIPHER_SIZE);
    }
    if(cipher_method & ACFG_WLAN_PROFILE_CIPHER_METH_AES)
    {
        acfg_os_strlcat(cipher, " CCMP", MAX_CIPHER_SIZE);
    }
}

uint32_t
acfg_wpa_supp_fill_config_file(FILE *configfile_fp)
{
    char buf[1024], tempbuf[50];

    /*Add ctrl interface*/
    acfg_os_snprintf(tempbuf, sizeof(tempbuf), "%s", CTRL_IFACE_STRING);
    acfg_os_snprintf(buf, sizeof(buf), "%s", tempbuf);
    acfg_os_strlcat(buf, "=", sizeof(buf));
    acfg_os_strlcat(buf, ctrl_wpasupp, sizeof(buf));
    acfg_os_strlcat(buf, "\n", sizeof(buf));

    acfg_os_strlcat(buf, "ap_scan=1\n", sizeof(buf));
    acfg_os_strlcat(buf, "eapol_version=1\n", sizeof(buf));
    if(0 > fprintf(configfile_fp, "%s",buf))
        return QDF_STATUS_E_FAILURE;

    return QDF_STATUS_SUCCESS;
}

uint32_t
acfg_wpa_supp_create_config (acfg_wlan_profile_vap_params_t *vap_params,
        char *conffile)
{
    FILE *configfile_fp;
    uint32_t status = QDF_STATUS_SUCCESS;
    char command[MAX_CMD_LEN] = {0};
    int err = 0;

    acfg_os_strcpy(conffile, WPA_SUPP_CONFFILE_PREFIX, MAX_CONF_FILE_NAME_LEN);
    acfg_os_strlcat(conffile, "-", MAX_CONF_FILE_NAME_LEN);
    acfg_os_strlcat(conffile, (char *)vap_params->vap_name, MAX_CONF_FILE_NAME_LEN);
    acfg_os_strlcat(conffile, ".conf", MAX_CONF_FILE_NAME_LEN);

    /*Remove old conf file before creating new one*/
    acfg_os_snprintf(command, sizeof(command),"rm -rf %s", conffile); 
    err = system(command);
    if(err != 0)
    {
        printf("Not able to remove conf file\n");
        return QDF_STATUS_E_FAILURE;
    }

    configfile_fp = fopen(conffile, "w+");
    if (configfile_fp == NULL) {
        acfg_log_errstr("config file open failed: %s\n", strerror(errno));
        return QDF_STATUS_E_FAILURE;
    }
    status = acfg_wpa_supp_fill_config_file(configfile_fp);
    fclose(configfile_fp);

    return status;
}

uint32_t
acfg_wpa_supp_add_network(acfg_wlan_profile_vap_params_t *vap_params)
{
    char cmd[255], reply[255];
    uint32_t len = sizeof (reply);

    acfg_os_snprintf(cmd, sizeof(cmd), "%s", WPA_ADD_NETWORK_CMD_PREFIX);
    if (acfg_ctrl_req(vap_params->vap_name, cmd,  strlen(cmd),
                reply, &len, ACFG_OPMODE_STA) < 0){
        acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                cmd,
                vap_params->vap_name);
        return QDF_STATUS_E_FAILURE;
    }

    return QDF_STATUS_SUCCESS;
}

uint32_t
acfg_set_roaming(acfg_wlan_profile_vap_params_t *vap_params,
        int value)
{
    return acfg_set_vap_param(vap_params->vap_name,
            ACFG_PARAM_ROAMING,
            value);
}

uint32_t
acfg_set_autoassoc(acfg_wlan_profile_vap_params_t *vap_params,
        int value)
{
    return acfg_set_vap_param(vap_params->vap_name,
            ACFG_PARAM_AUTO_ASSOC,
            value);
}

uint32_t acfg_security_open_or_wep(acfg_wlan_profile_vap_params_t *vap_params)
{
    uint32_t status = QDF_STATUS_E_FAILURE;

    status = acfg_set_roaming(vap_params, 1);
    if(status == QDF_STATUS_SUCCESS){
        status = acfg_set_autoassoc(vap_params, 0);
    }
    if(status == QDF_STATUS_SUCCESS){
        status = acfg_wlan_iface_up(vap_params->vap_name, NULL);
    }
    if(status == QDF_STATUS_SUCCESS){
        status = acfg_wlan_iface_down(vap_params->vap_name, NULL);
    }

    return status;
}

uint32_t
acfg_wpa_supp_disable_network(acfg_wlan_profile_vap_params_t *vap_params)
{
    if(QDF_STATUS_SUCCESS != acfg_wpa_supp_delete(vap_params)){
        return QDF_STATUS_E_FAILURE;
    }
    if(QDF_STATUS_SUCCESS !=acfg_security_open_or_wep(vap_params)){
        return QDF_STATUS_E_FAILURE;
    }

    return QDF_STATUS_SUCCESS;
}

uint32_t
acfg_wpa_supp_enable_network(acfg_wlan_profile_vap_params_t *vap_params)
{
    char cmd[512];
    int8_t id = 0;
    char reply[255] = {0};
    uint32_t len = sizeof (reply);

    acfg_os_snprintf(cmd, sizeof(cmd), "%s %d", WPA_ENABLE_NETWORK_CMD_PREFIX, id);
    if((acfg_ctrl_req(vap_params->vap_name, cmd, strlen(cmd),
                    reply, &len, ACFG_OPMODE_STA) < 0) ||
            strncmp (reply, "OK", strlen("OK"))){
        acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                cmd,
                vap_params->vap_name);
        return QDF_STATUS_E_FAILURE;
    }

    return QDF_STATUS_SUCCESS;
}
#if HAPD_CONF_FILE_UPDATE
static uint32_t
acfg_write_conf_file(char *cmd_list, int cmd_length, char *file_name, int index, int src_idx_start, int wpa_nwrk_cmd)
{
    FILE *fptr;
    char str[1051];
    int src_idx, dst_idx;
    int token_found = 0, i = 0;
    char *cmd;

    fptr = fopen(file_name, "a");
    if(fptr == NULL)
    {
        acfg_print("Error to append to conf file!\n");
        return QDF_STATUS_E_FAILURE;
    }
    if (wpa_nwrk_cmd)
    {
        fprintf (fptr, "\n\nnetwork={");
    }
    for (i = 0; i < index ; i++) {
        memset((char *)str, 0, sizeof (str));
        dst_idx = 0;
        src_idx = src_idx_start;
        token_found = 0;
        cmd = (cmd_list+i*cmd_length);
        while (*(cmd + src_idx) != '\0') {
            if (*(cmd + src_idx) == ' ') {
                if (!token_found) {
                    str[dst_idx++] = '=';
                    token_found = 1;
                } else {
                    str[dst_idx++] = *(cmd + src_idx);
                }
            } else {
                str[dst_idx++] = *(cmd + src_idx);
            }
            src_idx++;
        }

        str[dst_idx] = '\0';
        if (!wpa_nwrk_cmd) {
            fprintf (fptr, "\n%s",str);
        } else {
            fprintf (fptr, "\n        %s",str);
        }
    }
    if (wpa_nwrk_cmd)
    {
        fprintf (fptr, "\n}");
    }
    fclose(fptr);
    return QDF_STATUS_SUCCESS;
}
#endif

#if HAPD_CONF_FILE_UPDATE
uint32_t
acfg_wpa_supp_set_network(acfg_wlan_profile_vap_params_t *vap_params,
        uint32_t flags, uint32_t *enable_network, char *conffile)
#else
uint32_t
acfg_wpa_supp_set_network(acfg_wlan_profile_vap_params_t *vap_params,
        uint32_t flags, uint32_t *enable_network)
#endif
{
    uint8_t disable_supplicant = 0;
    char param[1024];
    char ncmd[20][1280];
    int32_t nindex = 0;
    int8_t id = 0;
    int32_t psk_len = 0;
    char cipher[MAX_CIPHER_SIZE];
    int32_t index = 0;
    char cmd_list[32][CMD_LENGTH];
#if HAPD_CONF_FILE_UPDATE
    int src_idx_start;
#else
    int32_t i = 0, ret = 0;
    char reply[255] = {0};
    uint32_t len = sizeof (reply);
#endif

    *enable_network = 1;
    if (flags & WPA_SUPP_MODIFY_SSID) {
        acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s \"%s\"", WPA_SET_NETWORK_CMD_PREFIX, id,
                "ssid", vap_params->ssid);
        nindex++;
    }
    if (flags & WPA_SUPP_MODIFY_BSSID) {
        acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s", WPA_SET_NETWORK_CMD_PREFIX, id,
                "bssid", vap_params->wds_params.wds_addr);
        nindex++;
    }
    if (flags & WPA_SUPP_MODIFY_SEC_METHOD) {
        if (vap_params->security_params.sec_method ==
                ACFG_WLAN_PROFILE_SEC_METH_WPA) {
            acfg_os_snprintf(param, sizeof(param), "%s", WPA_SUPP_PROTO_WPA);
        } else if ((vap_params->security_params.sec_method ==
                    ACFG_WLAN_PROFILE_SEC_METH_WPA2) ||
                   (vap_params->security_params.sec_method ==
                    ACFG_WLAN_PROFILE_SEC_METH_WPA3) ||
                   (vap_params->security_params.sec_method ==
                    ACFG_WLAN_PROFILE_SEC_METH_WPA2WPA3) ||
                   (vap_params->security_params.sec_method ==
                    ACFG_WLAN_PROFILE_SEC_METH_OWE) ||
                   (vap_params->security_params.sec_method ==
                    ACFG_WLAN_PROFILE_SEC_METH_DPP)) {
            acfg_os_snprintf(param, sizeof(param), "%s", WPA_SUPP_PROTO_WPA2);
        } else if (vap_params->security_params.sec_method ==
                ACFG_WLAN_PROFILE_SEC_METH_WPAWPA2) {
            acfg_os_snprintf(param, sizeof(param), "%s %s", WPA_SUPP_PROTO_WPA,
                     WPA_SUPP_PROTO_WPA2);
        } else if (vap_params->security_params.sec_method ==
                ACFG_WLAN_PROFILE_SEC_METH_OPEN) {
            acfg_os_snprintf(param, sizeof(param), "%s", WPA_SUPP_PROTO_OPEN);
        } else if (vap_params->security_params.sec_method ==
                ACFG_WLAN_PROFILE_SEC_METH_WPS) {
        } else {
            disable_supplicant = 1;
        }
        if (vap_params->security_params.wps_flag) {
            disable_supplicant = 0;
        }
        if (disable_supplicant) {
            *enable_network = 0;
            return QDF_STATUS_SUCCESS;
        }
        if ((vap_params->security_params.sec_method ==
                    ACFG_WLAN_PROFILE_SEC_METH_WPA) ||
                (vap_params->security_params.sec_method ==
                 ACFG_WLAN_PROFILE_SEC_METH_WPA2) ||
                (vap_params->security_params.sec_method ==
                 ACFG_WLAN_PROFILE_SEC_METH_WPAWPA2) ||
                (vap_params->security_params.sec_method ==
                 ACFG_WLAN_PROFILE_SEC_METH_WPA3) ||
                (vap_params->security_params.sec_method ==
                 ACFG_WLAN_PROFILE_SEC_METH_WPA2WPA3) ||
                (vap_params->security_params.sec_method ==
                 ACFG_WLAN_PROFILE_SEC_METH_OWE) ||
                (vap_params->security_params.sec_method ==
                 ACFG_WLAN_PROFILE_SEC_METH_DPP))
        {
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s",
                     WPA_SET_NETWORK_CMD_PREFIX, id, "proto", param);
            nindex++;
        }
        if ((vap_params->security_params.sec_method ==
                    ACFG_WLAN_PROFILE_SEC_METH_WPA) ||
                (vap_params->security_params.sec_method ==
                 ACFG_WLAN_PROFILE_SEC_METH_WPA2) ||
                (vap_params->security_params.sec_method ==
                 ACFG_WLAN_PROFILE_SEC_METH_WPAWPA2))
        {

            psk_len = strlen(vap_params->security_params.psk);
            acfg_os_snprintf(param, sizeof(param), "%s",
                     vap_params->security_params.psk);
            if (psk_len <= WPA_PSK_ASCII_LEN_MAX &&
                    psk_len >= WPA_PSK_ASCII_LEN_MIN)
            {
                acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s \"%s\"",
                        WPA_SET_NETWORK_CMD_PREFIX, id, "psk", param);
                nindex++;
            } else if (strlen(param) == WPA_PSK_HEX_LEN) {
                acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s",
                         WPA_SET_NETWORK_CMD_PREFIX, id,
                         "psk", param);
                nindex++;
            } else {
                acfg_log_errstr("Invalid PSK\n");
            }
            acfg_os_snprintf(param, sizeof(param), "%s", WPA_SUPP_KEYMGMT_WPA_PSK);
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s", WPA_SET_NETWORK_CMD_PREFIX, id,
                     "key_mgmt", param);
            nindex++;

            switch (vap_params->security_params.ieee80211w) {
                case ACFG_WLAN_PROFILE_IEEE80211W_OPTIONAL: /* Notice fallthrough */
                case ACFG_WLAN_PROFILE_IEEE80211W_REQUIRED:
                    acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s %s", WPA_SET_NETWORK_CMD_PREFIX, id,
                             "key_mgmt", param, "WPA-PSK-SHA256");
                    nindex++;
                    break;
                default:
                    break;
            }

            /* Fill cmd to set the ieee80211w value */
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %d", WPA_SET_NETWORK_CMD_PREFIX, id,
                    "ieee80211w", vap_params->security_params.ieee80211w);
            nindex++;

        } else if ((vap_params->security_params.sec_method ==
                    ACFG_WLAN_PROFILE_SEC_METH_WPA3))
        {
            if (strlen(vap_params->security_params.sae_password)) {
                psk_len = strlen(vap_params->security_params.sae_password);
                acfg_os_snprintf(param, sizeof(param), "%s", vap_params->security_params.sae_password);
            } else {
                psk_len = strlen(vap_params->security_params.psk);
                acfg_os_snprintf(param, sizeof(param), "%s", vap_params->security_params.psk);
            }
            /* Set psk param */
            if (psk_len <= WPA_PSK_ASCII_LEN_MAX &&
                psk_len >= WPA_PSK_ASCII_LEN_MIN) {
                acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s \"%s\"",
                         WPA_SET_NETWORK_CMD_PREFIX, id,
                         "psk", param);
                nindex++;
            } else if (strlen(param) == WPA_PSK_HEX_LEN) {
                acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s",
                         WPA_SET_NETWORK_CMD_PREFIX, id,
                         "psk", param);
                nindex++;
            } else {
                acfg_log_errstr("Invalid PSK\n");
                return QDF_STATUS_E_FAILURE;
            }
            /* Set key_mgmt params */
            acfg_os_snprintf(param, sizeof(param), "%s", WPA_SUPP_KEYMGMT_SAE);
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s",
                     WPA_SET_NETWORK_CMD_PREFIX, id, "key_mgmt", param);
            nindex++;

            /* Set ieee80211w params */
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %d", WPA_SET_NETWORK_CMD_PREFIX, id,
                     "ieee80211w", 2);
            nindex++;

            /* Set sae_pwe params */
            if(vap_params->security_params.sae_pwe) {
                char sae_pwe[30]={0};
                acfg_os_snprintf(sae_pwe, sizeof(sae_pwe), "SET sae_pwe %d", vap_params->security_params.sae_pwe); 
                if ((acfg_ctrl_req(vap_params->vap_name, sae_pwe,
                                   strlen(sae_pwe), reply, &len, ACFG_OPMODE_STA) < 0) ||
                    strncmp (reply, "OK", strlen("OK"))) {
                    acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                                    sae_pwe, vap_params->vap_name);
                    return QDF_STATUS_E_FAILURE;
                }
            }

            if (strlen(vap_params->security_params.sae_groups)) {
                char sae_group[30] = {0};
                snprintf(sae_group, sizeof(sae_group), "SET sae_groups %s", vap_params->security_params.sae_groups);
                /* Set sae_groups */
                if ((acfg_ctrl_req(vap_params->vap_name, sae_group,
                                   strlen(sae_group), reply, &len, ACFG_OPMODE_STA) < 0) ||
                    strncmp (reply, "OK", strlen("OK"))) {
                    acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                                    sae_group, vap_params->vap_name);
                    return QDF_STATUS_E_FAILURE;
                }
            }

        } else if (vap_params->security_params.sec_method ==
                   ACFG_WLAN_PROFILE_SEC_METH_WPA2WPA3) {
            if (strlen(vap_params->security_params.sae_password)) {
                psk_len = strlen(vap_params->security_params.sae_password);
                acfg_os_snprintf(param, sizeof(param), "%s", vap_params->security_params.sae_password);
            } else {
                psk_len = strlen(vap_params->security_params.psk);
                acfg_os_snprintf(param, sizeof(param), "%s", vap_params->security_params.psk);
            }
            if (psk_len <= WPA_PSK_ASCII_LEN_MAX &&
                psk_len >= WPA_PSK_ASCII_LEN_MIN) {
                acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s \"%s\"",
                         WPA_SET_NETWORK_CMD_PREFIX, id, "psk", param);
                nindex++;
            } else if (strlen(param) == WPA_PSK_HEX_LEN) {
                acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s",
                         WPA_SET_NETWORK_CMD_PREFIX, id,
                         "psk", param);
                nindex++;
            } else {
                acfg_log_errstr("Invalid PSK\n");
                return QDF_STATUS_E_FAILURE;
            }
            /* Set key_mgmt params */
            switch (vap_params->security_params.ieee80211w) {
                case ACFG_WLAN_PROFILE_IEEE80211W_OPTIONAL:
                    acfg_os_snprintf(param, sizeof(param), "%s", "WPA-PSK WPA-PSK-SHA256 SAE");
                    break;
                case ACFG_WLAN_PROFILE_IEEE80211W_REQUIRED:
                    acfg_os_snprintf(param, sizeof(param), "%s", "WPA-PSK-SHA256 SAE");
                    break;
                default:
                    acfg_log_errstr("ieee80211w value should be either 1 or 2\n");
                    return QDF_STATUS_E_FAILURE;
            }

            acfg_os_snprintf(ncmd[nindex], sizeof(param), "%s %d %s %s",
                     WPA_SET_NETWORK_CMD_PREFIX, id, "key_mgmt", param);
            nindex++;
            /* Set ieee80211w params */
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %d", WPA_SET_NETWORK_CMD_PREFIX, id,
                     "ieee80211w", vap_params->security_params.ieee80211w);
            nindex++;

            /* Set sae_pwe params */
            if(vap_params->security_params.sae_pwe) {
                char sae_pwe[30]={0};
                acfg_os_snprintf(sae_pwe, sizeof(sae_pwe), "SET sae_pwe %d", vap_params->security_params.sae_pwe);
                if ((acfg_ctrl_req(vap_params->vap_name, sae_pwe,
                                   strlen(sae_pwe), reply, &len, ACFG_OPMODE_STA) < 0) ||
                    strncmp (reply, "OK", strlen("OK"))) {
                    acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                                    sae_pwe, vap_params->vap_name);
                    return QDF_STATUS_E_FAILURE;
                }
            }

            if (strlen(vap_params->security_params.sae_groups)) {
                char sae_group[30] = {0};
                acfg_os_snprintf(sae_group, sizeof(sae_group), "SET sae_groups %s", vap_params->security_params.sae_groups);
                /* Set sae_groups */
                if ((acfg_ctrl_req(vap_params->vap_name, sae_group,
                                   strlen(sae_group), reply, &len, ACFG_OPMODE_STA) < 0) ||
                    strncmp (reply, "OK", strlen("OK"))) {
                    acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                                    sae_group, vap_params->vap_name);
                    return QDF_STATUS_E_FAILURE;
                }
            }

        } else if ((vap_params->security_params.sec_method ==
                    ACFG_WLAN_PROFILE_SEC_METH_OWE)) {
            /* Set key_mgmt params */
            acfg_os_snprintf(param, sizeof(param), "%s", WPA_SUPP_KEYMGMT_OWE);
            acfg_os_snprintf(ncmd[nindex], sizeof(param), "%s %d %s %s",
                     WPA_SET_NETWORK_CMD_PREFIX, id, "key_mgmt", param);
            nindex++;

            /* Set ieee80211w params */
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %d", WPA_SET_NETWORK_CMD_PREFIX, id,
                     "ieee80211w", 2);
            nindex++;

            if (strlen(vap_params->security_params.owe_groups)) {
                acfg_os_snprintf(param, sizeof(param), "%s", vap_params->security_params.owe_groups);
                acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s",
                         WPA_SET_NETWORK_CMD_PREFIX, id, "owe_group", param);
                nindex++;
            }
        } else if ((vap_params->security_params.sec_method ==
                    ACFG_WLAN_PROFILE_SEC_METH_DPP)) {
            /* Set key_mgmt params */
            acfg_os_snprintf(param, sizeof(param), "%s", WPA_SUPP_KEYMGMT_DPP);
            acfg_os_snprintf(ncmd[nindex], sizeof(param), "%s %d %s %s",
                     WPA_SET_NETWORK_CMD_PREFIX, id, "key_mgmt", param);
            nindex++;

            /* Set ieee80211w params */
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %d", WPA_SET_NETWORK_CMD_PREFIX, id,
                     "ieee80211w", 1);
            nindex++;

            if (strlen(vap_params->security_params.dpp_connector)) {
                acfg_os_snprintf(param, sizeof(param), "%s", vap_params->security_params.dpp_connector);
                acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s \"%s\"",
                         WPA_SET_NETWORK_CMD_PREFIX, id, "dpp_connector", param);
                nindex++;
            }
            if (strlen(vap_params->security_params.dpp_csign)) {
                acfg_os_snprintf(param, sizeof(param), "%s", vap_params->security_params.dpp_csign);
                acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s",
                         WPA_SET_NETWORK_CMD_PREFIX, id, "dpp_csign", param);
                nindex++;
            }
            if (strlen(vap_params->security_params.dpp_netaccesskey)) {
                acfg_os_snprintf(param, sizeof(param), "%s", vap_params->security_params.dpp_netaccesskey);
                acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s",
                         WPA_SET_NETWORK_CMD_PREFIX, id, "dpp_netaccesskey", param);
                nindex++;
            }
            acfg_os_snprintf(cmd_list[index], sizeof(cmd_list[index]), "%s %s %d",
                    WPA_SET_CMD_PREFIX, "update_config", 1);
            index++;
            acfg_os_snprintf(cmd_list[index], sizeof(cmd_list[index]), "%s %s %d",
                    WPA_SET_CMD_PREFIX, "dpp_config_processing", 2);
            index++;
        } else if ((vap_params->security_params.sec_method ==
                  ACFG_WLAN_PROFILE_SEC_METH_OPEN)) {
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s",
                     WPA_SET_NETWORK_CMD_PREFIX, id,
                     "key_mgmt", "NONE");
            nindex++;
        } else if (ACFG_IS_SEC_WEP(vap_params->security_params)) {
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s",
                     WPA_SET_NETWORK_CMD_PREFIX, id,
                     "key_mgmt", "NONE");
            nindex++;
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s",
                    WPA_SET_NETWORK_CMD_PREFIX, id,
                    "wep_key0", vap_params->security_params.wep_key0);
            nindex++;
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s",
                    WPA_SET_NETWORK_CMD_PREFIX, id,
                    "wep_key1", vap_params->security_params.wep_key1);
            nindex++;
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s",
                    WPA_SET_NETWORK_CMD_PREFIX, id,
                    "wep_key2", vap_params->security_params.wep_key2);
            nindex++;
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s",
                    WPA_SET_NETWORK_CMD_PREFIX, id,
                    "wep_key3", vap_params->security_params.wep_key3);
            nindex++;
            acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %d",
                     WPA_SET_NETWORK_CMD_PREFIX, id,
                     "wep_tx_keyidx",
                     vap_params->security_params.wep_key_defidx);
            nindex++;
        }
    }
    if (ACFG_IS_SEC_ENABLED(vap_params->security_params.sec_method) && (flags & WPA_SUPP_MODIFY_CIPHER)) {
        //Set pairwise ciphers
        memset (cipher, '\0', sizeof (cipher));
        if (vap_params->security_params.cipher_method == ACFG_WLAN_PROFILE_CIPHER_METH_INVALID) {
            vap_params->security_params.cipher_method = vap_params->security_params.g_cipher_method;
        }
        acfg_get_cipher_str(vap_params->security_params.cipher_method,
                cipher);
        acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s", WPA_SET_NETWORK_CMD_PREFIX,
                 id, "pairwise", cipher);
        nindex++;

        //Set group ciphers
        memset (cipher, '\0', sizeof (cipher));
        if (vap_params->security_params.g_cipher_method == ACFG_WLAN_PROFILE_CIPHER_METH_INVALID) {
            vap_params->security_params.g_cipher_method = vap_params->security_params.cipher_method;
        }
        acfg_get_cipher_str(vap_params->security_params.g_cipher_method,
                cipher);
        acfg_os_snprintf(ncmd[nindex], sizeof(ncmd), "%s %d %s %s", WPA_SET_NETWORK_CMD_PREFIX,
                 id, "group", cipher);
        nindex++;
    }
    //Set WPS related parameters
    {
        acfg_os_snprintf(cmd_list[index], sizeof(cmd_list[index]), "%s %s %d",
                WPA_SET_CMD_PREFIX, "update_config", 0);
        index++;
        if(vap_params->security_params.wps_device_name[0] != 0)
        {
            acfg_os_snprintf(cmd_list[index], sizeof(cmd_list[index]), "%s %s %s",
                     WPA_SET_CMD_PREFIX, "device_name",
                     vap_params->security_params.wps_device_name);
            index++;
        }
        if(vap_params->security_params.wps_model_name[0] != 0)
        {
            acfg_os_snprintf(cmd_list[index], sizeof(cmd_list[index]), "%s %s %s",
                     WPA_SET_CMD_PREFIX, "model_name",
                     vap_params->security_params.wps_model_name);
            index++;
        }
        if(vap_params->security_params.wps_manufacturer[0] != 0)
        {
            acfg_os_snprintf(cmd_list[index], sizeof(cmd_list[index]), "%s %s %s",
                     WPA_SET_CMD_PREFIX, "manufacturer",
                     vap_params->security_params.wps_manufacturer);
            index++;
        }
        if(vap_params->security_params.wps_model_number[0] != 0)
        {
            acfg_os_snprintf(cmd_list[index], sizeof(cmd_list[index]), "%s %s %s",
                     WPA_SET_CMD_PREFIX, "model_number",
                     vap_params->security_params.wps_model_number);
            index++;
        }
        if(vap_params->security_params.wps_serial_number[0] != 0)
        {
            acfg_os_snprintf(cmd_list[index], sizeof(cmd_list[index]), "%s %s %s",
                     WPA_SET_CMD_PREFIX, "serial_number",
                     vap_params->security_params.wps_serial_number);
            index++;
        }
        if(vap_params->security_params.wps_device_type[0] != 0)
        {
            acfg_os_snprintf(cmd_list[index], sizeof(cmd_list[index]), "%s %s %s",
                     WPA_SET_CMD_PREFIX, "device_type",
                     vap_params->security_params.wps_device_type);
            index++;
        }
        acfg_os_snprintf(cmd_list[index], sizeof(cmd_list[index]), "%s %s %s",
                 WPA_SET_CMD_PREFIX, "os_version",
                 "01020300");
        index++;
        if(vap_params->security_params.wps_config_methods[0] != 0)
        {
            if(index <= CMD_LENGTH) {
                ret = acfg_os_snprintf(cmd_list[index],sizeof(cmd_list[index]), "%s %s %s", WPA_SET_CMD_PREFIX, "config_methods",
                    vap_params->security_params.wps_config_methods);
		if ((ret < 0) || (ret >= (int)sizeof(cmd_list[index]))) {
		    acfg_log_errstr("%s:%d Failed snprintf\n",__func__,__LINE__);
		    return QDF_STATUS_E_FAILURE;
		}
                index++;
            }
            else
                return QDF_STATUS_E_FAILURE;
        }
    }
#if !HAPD_CONF_FILE_UPDATE
    for (i = 0; i < nindex; i++) {
        if((acfg_ctrl_req(vap_params->vap_name, ncmd[i],
                        strlen(ncmd[i]), reply, &len, ACFG_OPMODE_STA) < 0) ||
                strncmp (reply, "OK", strlen("OK"))) {
            acfg_log_errstr("%s: ncmd --> %s failed for %s\n", __func__,
                    ncmd[i],
                    vap_params->vap_name);
            return QDF_STATUS_E_FAILURE;
        }
    }
    for (i = 0; i < index; i++) {
        if((acfg_ctrl_req(vap_params->vap_name, cmd_list[i],
                        strlen(cmd_list[i]), reply, &len, ACFG_OPMODE_STA) < 0) ||
                strncmp (reply, "OK", strlen("OK"))) {
            acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                    cmd_list[i],
                    vap_params->vap_name);
            return QDF_STATUS_E_FAILURE;
        }
    }
#else
    src_idx_start = sizeof("SET ") - 1;
    if(QDF_STATUS_SUCCESS != acfg_write_conf_file((char *)cmd_list, CMD_LENGTH, conffile, index, src_idx_start, 0))
        return QDF_STATUS_E_FAILURE;

    src_idx_start = sizeof("SET_NETWORK 0 ") - 1;
    if(QDF_STATUS_SUCCESS != acfg_write_conf_file((char *)ncmd, 1024, conffile, nindex, src_idx_start, 1))
        return QDF_STATUS_E_FAILURE;
#endif
    return QDF_STATUS_SUCCESS;
}

uint32_t
acfg_wpa_supp_interface_remove(uint8_t *ifname )
{
    char cmd[255];
    char reply[255] = {0};
    uint32_t len = sizeof (reply);

    acfg_os_snprintf(cmd, sizeof(cmd), "%s %s",
             WPA_INTERFACE_REMOVE_CMD_PREFIX, ifname);
    if((acfg_ctrl_req((uint8_t*)ACFG_GLOBAL_CTRL_IFACE, cmd, strlen(cmd),
                    reply,
                    &len, ACFG_OPMODE_STA) < 0) ||
            strncmp (reply, "OK", strlen("OK"))){
        acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                cmd,
                ifname);
        return QDF_STATUS_E_FAILURE;
    }

    return QDF_STATUS_SUCCESS;
}


uint32_t
acfg_wpa_supp_add_interface(acfg_wlan_profile_vap_params_t *vap_params,
        int8_t force_enable, int8_t *sec)
{
    char  driver_name[32], cmd[255];
    uint32_t modify_flags = 0, enable_network = 0;
    char conffile[128];//255 changed to 128,
    char reply[255] = {0};
    uint32_t len = sizeof (reply);
    char ctrl_iface[ACFG_CTRL_IFACE_LEN];//25 changed value 30 defined in macro
    uint32_t status = QDF_STATUS_SUCCESS;

    status = acfg_wpa_supp_create_config(vap_params, conffile);
    if(status != QDF_STATUS_SUCCESS){
        goto fail;
    }
    printf("----%s ----- %d-----\n",__func__,__LINE__);

    modify_flags = (WPA_SUPP_MODIFY_SSID |
            WPA_SUPP_MODIFY_SEC_METHOD);

    modify_flags |= WPA_SUPP_MODIFY_CIPHER;

    if (vap_params->wds_params.enabled == 1 &&
            vap_params->wds_params.wds_addr[0] != 0)
    {
        modify_flags |= WPA_SUPP_MODIFY_BSSID;
    }

    if (!g_sock_ctx.cfg80211) {
        acfg_os_strcpy(driver_name, WPA_DRIVER_ATHR, sizeof(driver_name));
    } else {
        acfg_os_strcpy(driver_name, WPA_DRIVER_NL80211, sizeof(driver_name));
    }
#if HAPD_CONF_FILE_UPDATE
    if(acfg_wpa_supp_set_network(vap_params, modify_flags,
                &enable_network, conffile) != QDF_STATUS_SUCCESS){
        goto fail;
    }
#endif

    acfg_os_snprintf(ctrl_iface, sizeof(ctrl_iface), "%s", ctrl_wpasupp);
    if (vap_params->bridge[0] != 0) {
        acfg_os_snprintf(cmd, sizeof(cmd), "%s %s\t%s\t%s\t%s\t%d\t%s",
                 WPA_INTERFACE_ADD_CMD_PREFIX,
                 vap_params->vap_name, conffile, driver_name, ctrl_iface, 0,
                 vap_params->bridge);
    } else {
        acfg_os_snprintf(cmd, sizeof(cmd), "%s %s\t%s\t%s",
                 WPA_INTERFACE_ADD_CMD_PREFIX,
                 vap_params->vap_name, conffile, driver_name);
    }

    if((acfg_ctrl_req((uint8_t *)ACFG_GLOBAL_CTRL_IFACE, cmd, strlen(cmd),
                reply, &len, ACFG_OPMODE_STA) < 0) ||
            strncmp (reply, "OK", strlen("OK"))) {
        acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                cmd,
                vap_params->vap_name);
        goto fail;
    }
#if !HAPD_CONF_FILE_UPDATE
    status = acfg_wpa_supp_add_network(vap_params);
    if(status != QDF_STATUS_SUCCESS){
        goto fail;
    }

    if(acfg_wpa_supp_set_network(vap_params, modify_flags,
                &enable_network) != QDF_STATUS_SUCCESS){
        goto fail;
    }
#endif

    if (enable_network || force_enable) {
        status = acfg_wpa_supp_enable_network(vap_params);
        if(status != QDF_STATUS_SUCCESS){
            goto fail;
        }
	printf("----%s ----- %d-----\n",__func__,__LINE__);
        *sec = 1;
    } else {
        status = acfg_wpa_supp_disable_network(vap_params);
        if(status != QDF_STATUS_SUCCESS){
            goto fail;
        }
	printf("----%s ----- %d-----\n",__func__,__LINE__);
        *sec = 0;
    }
    return status;
fail:
    return QDF_STATUS_E_FAILURE;
}

uint32_t
acfg_wpa_supp_delete(acfg_wlan_profile_vap_params_t *vap_params)
{
    char cmd[512];
    int8_t id = 0;
    char reply[255] = {0};
    uint32_t len = sizeof (reply);

    if (acfg_ctrl_iface_present(vap_params->vap_name,
                ACFG_OPMODE_STA) == -1) {
        return QDF_STATUS_SUCCESS;
    }

    memset(cmd, '\0', sizeof (cmd));
    acfg_os_snprintf(cmd, sizeof(cmd), "%s %s %s", WPA_SET_CMD_PREFIX, "ap_scan", "0");
    if((acfg_ctrl_req(vap_params->vap_name, cmd,
                strlen(cmd), reply, &len, ACFG_OPMODE_STA) < 0) ||
            strncmp (reply, "OK", strlen("OK"))){
        acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                cmd,
                vap_params->vap_name);
        return QDF_STATUS_E_FAILURE;
    }

    memset(cmd, '\0', sizeof (cmd));
    acfg_os_snprintf(cmd, sizeof(cmd), "%s %d", WPA_DISABLE_NETWORK_CMD_PREFIX, id);
    if((acfg_ctrl_req(vap_params->vap_name, cmd,
                strlen(cmd), reply, &len, ACFG_OPMODE_STA) < 0) ||
            strncmp (reply, "OK", strlen("OK"))){
        acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                cmd,
                vap_params->vap_name);
        return QDF_STATUS_E_FAILURE;
    }

    acfg_os_snprintf(cmd, sizeof(cmd), "%s %d", WPA_REMOVE_NETWORK_CMD_PREFIX, id);
    if((acfg_ctrl_req(vap_params->vap_name, cmd,
                strlen(cmd), reply, &len, ACFG_OPMODE_STA) < 0) ||
            strncmp (reply, "OK", strlen("OK"))){
        acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                cmd,
                vap_params->vap_name);
        return QDF_STATUS_E_FAILURE;
    }

    if(QDF_STATUS_SUCCESS != acfg_wpa_supp_interface_remove(vap_params->vap_name))
        return QDF_STATUS_E_FAILURE;

    return QDF_STATUS_SUCCESS;
}

uint32_t
acfg_wpa_supp_modify(acfg_wlan_profile_vap_params_t *vap_params,
        acfg_wlan_profile_vap_params_t *cur_vap_params,
        int8_t *sec)
{
    uint32_t state = ACFG_SEC_UNCHANGED;

    if(ACFG_IS_WPS_WEP_ENABLED(vap_params->security_params)) {
        fprintf(stderr, "configuring wep: Disabling wps\n");
        if (acfg_ctrl_iface_present(vap_params->vap_name,
                    ACFG_OPMODE_STA) == 1)
        {
            if(QDF_STATUS_SUCCESS != acfg_wpa_supp_disable_network(vap_params))
                return QDF_STATUS_E_FAILURE;
            if(QDF_STATUS_SUCCESS != acfg_set_auth_open(vap_params,
                        ACFG_SEC_DISABLE_SECURITY))
                return QDF_STATUS_E_FAILURE;
            acfg_rem_wps_config_file(vap_params->vap_name);
        }
        *sec = 0;
        return QDF_STATUS_SUCCESS;
    }
    acfg_get_security_state (vap_params, cur_vap_params, &state);
    if (ACFG_IS_SEC_ENABLED(vap_params->security_params.sec_method) &&
            ACFG_IS_SEC_ENABLED(cur_vap_params->security_params.sec_method) &&
            strcmp(vap_params->bridge, cur_vap_params->bridge))
    {
        state = ACFG_SEC_SET_SECURITY;
    }
    if (strcmp(vap_params->bridge, cur_vap_params->bridge) &&
            ACFG_IS_VALID_WPS(vap_params->security_params))
    {
        state = ACFG_SEC_RESET_SECURITY;
    }
    if (state == ACFG_SEC_UNCHANGED) {
        if (ACFG_IS_SEC_ENABLED(vap_params->security_params.sec_method) ||
                vap_params->security_params.wps_flag) {
            *sec = 1;
        } else {
            *sec = 0;
        }
        return QDF_STATUS_SUCCESS;
    }
    if(QDF_STATUS_SUCCESS != acfg_set_auth_open(vap_params, state)){
        return QDF_STATUS_E_FAILURE;
    }
    if(QDF_STATUS_SUCCESS != acfg_wpa_supp_disable_network(vap_params)){
        return QDF_STATUS_E_FAILURE;
    }

    if (state == ACFG_SEC_DISABLE_SECURITY) {
        /* Clear optional IEs(includes RSN IE) while changing security to open */
        acfg_set_vap_param(vap_params->vap_name, ACFG_PARAM_CLR_APPOPT_IE, 0);
        *sec = 0;
        return QDF_STATUS_SUCCESS;
    }

    if(QDF_STATUS_SUCCESS != acfg_wpa_supp_add_interface(vap_params, 1, sec)){
        return QDF_STATUS_E_FAILURE;
    }
    if (vap_params->security_params.wps_flag) {
        acfg_wps_cred_t wps_cred;
        if (acfg_get_wps_config(vap_params->vap_name, &wps_cred) >= 0) {
            //Security param is modified. So remove the wps configuration
            acfg_rem_wps_config_file(vap_params->vap_name);
        }
    }

    return QDF_STATUS_SUCCESS;
}
uint32_t
acfg_set_hapd_config_params(acfg_wlan_profile_vap_params_t *vap_params)
{
    int index = 0;
    char replybuf[255] = {0};
    uint32_t len = sizeof (replybuf);
    char acfg_hapd_param_list[ACFG_MAX_HAPD_CONFIG_PARAM+1][1051];//changed 1024 to 1051
    char cmd[255];
    char cipher[MAX_CIPHER_SIZE];
#if HAPD_CONF_FILE_UPDATE
    char file_name[30];
    int src_idx_start;
#else
    int i;
#endif
    index = 0;
    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]),
             "SET ssid %s", vap_params->ssid);
    index++;

    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]),
             "SET send_probe_response %d", 0);
    index++;

    if (vap_params->bridge[0]) {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                sizeof(acfg_hapd_param_list[index]),
                "SET bridge %s", vap_params->bridge);
	CHECK_AND_INCREMENT(index);
    }
    if (ACFG_IS_SEC_WEP(vap_params->security_params))
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),"SET wep_key0 %s",
                 vap_params->security_params.wep_key0);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wep_key1 %s",
                 vap_params->security_params.wep_key1);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wep_key2 %s",
                 vap_params->security_params.wep_key2);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wep_key3 %s",
                 vap_params->security_params.wep_key3);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wep_default_key %d",
                 vap_params->security_params.wep_key_defidx);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET auth_algs 1");
        CHECK_AND_INCREMENT(index);
    }
    if ((vap_params->security_params.sec_method ==
                ACFG_WLAN_PROFILE_SEC_METH_OPEN) &&
            (vap_params->security_params.cipher_method ==
             ACFG_WLAN_PROFILE_CIPHER_METH_NONE))
    {
        acfg_os_snprintf(cmd, sizeof(cmd), "CLEAR_WEP");
        if((acfg_ctrl_req (vap_params->vap_name,
                cmd,
                strlen(cmd),
                replybuf, &len,
                        ACFG_OPMODE_HOSTAP) < 0) ||
                strncmp (replybuf, "OK", strlen("OK"))){
            acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                    cmd,
                    vap_params->vap_name);
            return QDF_STATUS_E_FAILURE;
        }
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),"SET auth_algs 1");
        CHECK_AND_INCREMENT(index);
    }

    if ((vap_params->security_params.sec2_radius_param.radius_ip[0] != 0) &&
            (vap_params->security_params.sec2_radius_param.radius_port != 0))
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET auth_server_addr %s",
                 vap_params->security_params.sec2_radius_param.radius_ip);
        CHECK_AND_INCREMENT(index);

        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),"SET auth_server_port %d",
                 vap_params->security_params.sec2_radius_param.radius_port);
        CHECK_AND_INCREMENT(index);

        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET auth_server_shared_secret %s",
                 vap_params->security_params.sec2_radius_param.shared_secret);
        CHECK_AND_INCREMENT(index);
    }
    if ((vap_params->security_params.sec1_radius_param.radius_ip[0] != 0) &&
            (vap_params->security_params.sec1_radius_param.radius_port != 0))
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET auth_server_addr %s",
                 vap_params->security_params.sec1_radius_param.radius_ip);
        CHECK_AND_INCREMENT(index);

        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET auth_server_port %d",
                 vap_params->security_params.sec1_radius_param.radius_port);
        CHECK_AND_INCREMENT(index);

        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET auth_server_shared_secret %s",
                 vap_params->security_params.sec1_radius_param.shared_secret);
        CHECK_AND_INCREMENT(index);
    }
    if ((vap_params->security_params.pri_radius_param.radius_ip[0] != 0) &&
            (vap_params->security_params.pri_radius_param.radius_port != 0))
    {
      acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET auth_server_addr %s",
                vap_params->security_params.pri_radius_param.radius_ip);
        CHECK_AND_INCREMENT(index);

        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET auth_server_port %d",
                 vap_params->security_params.pri_radius_param.radius_port);
        CHECK_AND_INCREMENT(index);

        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET auth_server_shared_secret %s",
                 vap_params->security_params.pri_radius_param.shared_secret);
        CHECK_AND_INCREMENT(index);
    }
    if (vap_params->security_params.radius_retry_primary_interval != 0)
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET radius_retry_primary_interval %d",
                 vap_params->security_params.radius_retry_primary_interval);
	CHECK_AND_INCREMENT(index);
    }
    if ((vap_params->security_params.sec_method !=
                ACFG_WLAN_PROFILE_SEC_METH_WPA_EAP) &&
            (vap_params->security_params.sec_method !=
             ACFG_WLAN_PROFILE_SEC_METH_WPA2_EAP) &&
            (vap_params->security_params.sec_method !=
             ACFG_WLAN_PROFILE_SEC_METH_SUITEB_192))
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET eap_server 1");
        CHECK_AND_INCREMENT(index);
    }

    if ((vap_params->security_params.sec_method ==
                ACFG_WLAN_PROFILE_SEC_METH_WPA) ||
            (vap_params->security_params.sec_method ==
             ACFG_WLAN_PROFILE_SEC_METH_WPA2) ||
            (vap_params->security_params.sec_method ==
             ACFG_WLAN_PROFILE_SEC_METH_WPAWPA2))
    {
        if(strlen (vap_params->security_params.psk) <= (ACFG_MAX_PSK_LEN - 2)) {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "SET wpa_passphrase %s",
                     vap_params->security_params.psk);
            CHECK_AND_INCREMENT(index);
        } else if (strlen(vap_params->security_params.psk) ==
                ACFG_MAX_PSK_LEN - 1)
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "SET wpa_psk %s",
                     vap_params->security_params.psk);
            CHECK_AND_INCREMENT(index);
        }

        acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET wpa_key_mgmt %s", "WPA-PSK");

        /* If ieee80211w is enabled, overwrite the default wpa_key_mgmt frame */
        switch (vap_params->security_params.ieee80211w) {
            case ACFG_WLAN_PROFILE_IEEE80211W_OPTIONAL:
                if(vap_params->security_params.sha256) {
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET wpa_key_mgmt %s %s", "WPA-PSK", "WPA-PSK-SHA256");
                    CHECK_AND_INCREMENT(index);
                }
                break;
            case ACFG_WLAN_PROFILE_IEEE80211W_REQUIRED:
                acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET wpa_key_mgmt %s", "WPA-PSK-SHA256");
                CHECK_AND_INCREMENT(index);
                break;
            default:
                break;
        }

        acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]),
                "SET ieee80211w %d", vap_params->security_params.ieee80211w);
        CHECK_AND_INCREMENT(index);
        if (vap_params->security_params.ieee80211w > 0) {

            if (vap_params->security_params.assoc_sa_query_max_timeout > 0) {
                acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET assoc_sa_query_max_timeout %d",
                        vap_params->security_params.assoc_sa_query_max_timeout);
                CHECK_AND_INCREMENT(index);
            }

            if (vap_params->security_params.assoc_sa_query_retry_timeout > 0) {
                acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET assoc_sa_query_retry_timeout %d",
                        vap_params->security_params.assoc_sa_query_retry_timeout);
                CHECK_AND_INCREMENT(index);
            }

            /* Set the group_mgmt_cipher value */
            acfg_log_errstr("Group mgmt cipher = %d\n", vap_params->security_params.group_mgmt_cipher);
            switch (vap_params->security_params.group_mgmt_cipher) {
                case ACFG_WLAN_PROFILE_GRP_MGMT_CIPHER_AES_128_CMAC:
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]),
                            "SET group_mgmt_cipher %s", "AES-128-CMAC");
                    break;
                case ACFG_WLAN_PROFILE_GRP_MGMT_CIPHER_BIP_GMAC_128:
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]),
                            "SET group_mgmt_cipher %s", "BIP-GMAC-128");
                    break;
                case ACFG_WLAN_PROFILE_GRP_MGMT_CIPHER_BIP_GMAC_256:
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET group_mgmt_cipher %s",
                            "BIP-GMAC-256");
                    break;
                case ACFG_WLAN_PROFILE_GRP_MGMT_CIPHER_BIP_CMAC_256:
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET group_mgmt_cipher %s",
                            "BIP-CMAC-256");
                    break;
                default:
                    break;
            }
            if (vap_params->security_params.group_mgmt_cipher >
                    ACFG_WLAN_PROFILE_GRP_MGMT_CIPHER_INVALID) {
                CHECK_AND_INCREMENT(index);
            }
        }

    } else if ((vap_params->security_params.sec_method ==
                ACFG_WLAN_PROFILE_SEC_METH_WPA3) ||
                (vap_params->security_params.sec_method ==
                ACFG_WLAN_PROFILE_SEC_METH_WPA2WPA3))
    {
        if (vap_params->security_params.sec_method ==
            ACFG_WLAN_PROFILE_SEC_METH_WPA3) {
            if (strlen(vap_params->security_params.sae_password)) {
               if (strlen(vap_params->security_params.sae_password) <= (ACFG_MAX_PSK_LEN - 2)) {
                   acfg_os_snprintf(acfg_hapd_param_list[index],
                            sizeof(acfg_hapd_param_list[index]), "SET sae_password %s",
                            vap_params->security_params.sae_password);
                   CHECK_AND_INCREMENT(index);
               }
               else {
                   acfg_log_errstr("%s: Exceeded max PSK key length\n", __func__);
                   return QDF_STATUS_E_FAILURE;
               }
            } else if(strlen(vap_params->security_params.psk)) {
               if (strlen(vap_params->security_params.psk) <= (ACFG_MAX_PSK_LEN - 2)) {
                  acfg_os_snprintf(acfg_hapd_param_list[index],
                           sizeof(acfg_hapd_param_list[index]), "SET wpa_passphrase %s",
                           vap_params->security_params.psk);
                  CHECK_AND_INCREMENT(index);
               } else if (strlen(vap_params->security_params.psk) ==
                      ACFG_MAX_PSK_LEN - 1)
               {
                  acfg_os_snprintf(acfg_hapd_param_list[index],
                           sizeof(acfg_hapd_param_list[index]), "SET wpa_psk %s",
                           vap_params->security_params.psk);
                  CHECK_AND_INCREMENT(index);
               }
            }
            /* set sae_pwe param */
            if(vap_params->security_params.sae_pwe) {
                acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET sae_pwe %d", vap_params->security_params.sae_pwe);
                CHECK_AND_INCREMENT(index);
            }
            switch (vap_params->security_params.ieee80211w) {
                case ACFG_WLAN_PROFILE_IEEE80211W_OPTIONAL:
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET wpa_key_mgmt %s", "SAE");
                    CHECK_AND_INCREMENT(index);
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET sae_require_mfp %d", 1);
                    CHECK_AND_INCREMENT(index);
                    break;
                case ACFG_WLAN_PROFILE_IEEE80211W_REQUIRED:
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET wpa_key_mgmt %s", "SAE");
                    CHECK_AND_INCREMENT(index);
                    break;
                default:
                    break;
            }
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET ieee80211w %d", vap_params->security_params.ieee80211w);
            CHECK_AND_INCREMENT(index);
        }
        else if (vap_params->security_params.sec_method ==
                 ACFG_WLAN_PROFILE_SEC_METH_WPA2WPA3) {
            if (strlen(vap_params->security_params.sae_password)) {
                if (strlen(vap_params->security_params.sae_password) <= (ACFG_MAX_PSK_LEN - 2)) {
                    acfg_os_snprintf(acfg_hapd_param_list[index],
                             sizeof(acfg_hapd_param_list[index]), "SET sae_password %s",
                             vap_params->security_params.sae_password);
                    CHECK_AND_INCREMENT(index);
                }
                else {
                    acfg_log_errstr("%s: Exceeded max PSK key length\n", __func__);
                    return QDF_STATUS_E_FAILURE;
                }
                if(strlen(vap_params->security_params.psk) == 0) {
                   acfg_os_snprintf(acfg_hapd_param_list[index],
                            sizeof(acfg_hapd_param_list[index]), "SET wpa_passphrase %s",
                            vap_params->security_params.sae_password);
                   CHECK_AND_INCREMENT(index);
                }
            }
            if(strlen(vap_params->security_params.psk)) {
               if (strlen(vap_params->security_params.psk) <= (ACFG_MAX_PSK_LEN - 2)) {
                  acfg_os_snprintf(acfg_hapd_param_list[index],
                           sizeof(acfg_hapd_param_list[index]), "SET wpa_passphrase %s",
                           vap_params->security_params.psk);
                  CHECK_AND_INCREMENT(index);
               } else if (strlen(vap_params->security_params.psk) ==
                      ACFG_MAX_PSK_LEN - 1)
               {
                  acfg_os_snprintf(acfg_hapd_param_list[index],
                           sizeof(acfg_hapd_param_list[index]), "SET wpa_psk %s",
                           vap_params->security_params.psk);
                  CHECK_AND_INCREMENT(index);
               }
            }
            /* set sae_pwe param */
            if(vap_params->security_params.sae_pwe) {
                acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET sae_pwe %d", vap_params->security_params.sae_pwe);
                CHECK_AND_INCREMENT(index);
            }
            switch (vap_params->security_params.ieee80211w) {
                case ACFG_WLAN_PROFILE_IEEE80211W_OPTIONAL:
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET wpa_key_mgmt %s", "WPA-PSK SAE");
                    CHECK_AND_INCREMENT(index);
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET sae_require_mfp %d", 1);
                    CHECK_AND_INCREMENT(index);
                    break;
                case ACFG_WLAN_PROFILE_IEEE80211W_REQUIRED:
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET wpa_key_mgmt %s", "WPA-PSK-SHA256 SAE");
                    CHECK_AND_INCREMENT(index);
                    break;
                default:
                    break;
            }
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET ieee80211w %d", vap_params->security_params.ieee80211w);
            CHECK_AND_INCREMENT(index);
        }
        if(strlen(vap_params->security_params.sae_groups)) {
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET sae_groups %s", vap_params->security_params.sae_groups);
            CHECK_AND_INCREMENT(index);
        }

    } else if (vap_params->security_params.sec_method ==
               ACFG_WLAN_PROFILE_SEC_METH_OWE) {
        switch (vap_params->security_params.ieee80211w) {
            case ACFG_WLAN_PROFILE_IEEE80211W_OPTIONAL:
                acfg_log_errstr("%s: ieee80211w should always be 2 in OWE\n", __func__);
                break;
            case ACFG_WLAN_PROFILE_IEEE80211W_REQUIRED:
                if(strlen (vap_params->security_params.owe_transition_ifname)) {
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET owe_transition_ifname %s", vap_params->security_params.owe_transition_ifname);
                    CHECK_AND_INCREMENT(index);
                }
                if(strlen (vap_params->security_params.owe_transition_bssid)) {
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET owe_transition_bssid %s", vap_params->security_params.owe_transition_bssid);
                    CHECK_AND_INCREMENT(index);
                }
                if(strlen (vap_params->security_params.owe_transition_ssid)) {
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET owe_transition_ssid \"%s\"", vap_params->security_params.owe_transition_ssid);
                    CHECK_AND_INCREMENT(index);
                }
                acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET wpa_key_mgmt %s", "OWE");
                CHECK_AND_INCREMENT(index);
                break;
            default:
                break;
        }
        acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET ieee80211w %d", vap_params->security_params.ieee80211w);
        CHECK_AND_INCREMENT(index);
        if(strlen(vap_params->security_params.owe_groups)) {
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET owe_groups %s", vap_params->security_params.owe_groups);
            CHECK_AND_INCREMENT(index);
        }

    } else if ((vap_params->security_params.sec_method ==
               ACFG_WLAN_PROFILE_SEC_METH_DPP) ||
               (vap_params->security_params.sec_method ==
               ACFG_WLAN_PROFILE_SEC_METH_DPP_WPA2WPA3) ||
               (vap_params->security_params.sec_method ==
               ACFG_WLAN_PROFILE_SEC_METH_DPP_WPA3)) {
        acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]),
                 "SET dtim_period 1");
        CHECK_AND_INCREMENT(index);
        if(strlen (vap_params->security_params.dpp_connector)) {
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET dpp_connector %s", vap_params->security_params.dpp_connector);
            CHECK_AND_INCREMENT(index);
        }
        if(strlen(vap_params->security_params.dpp_csign)) {
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET dpp_csign %s", vap_params->security_params.dpp_csign);
            CHECK_AND_INCREMENT(index);
        }
        if(strlen(vap_params->security_params.dpp_netaccesskey)) {
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET dpp_netaccesskey %s", vap_params->security_params.dpp_netaccesskey);
            CHECK_AND_INCREMENT(index);
        }

        if ((vap_params->security_params.sec_method ==
               ACFG_WLAN_PROFILE_SEC_METH_DPP_WPA2WPA3) ||
               (vap_params->security_params.sec_method ==
               ACFG_WLAN_PROFILE_SEC_METH_DPP_WPA3)) {
            if (strlen(vap_params->security_params.sae_password)) {
               if (strlen(vap_params->security_params.sae_password) <= (ACFG_MAX_PSK_LEN - 2)) {
                   acfg_os_snprintf(acfg_hapd_param_list[index],
                            sizeof(acfg_hapd_param_list[index]), "SET sae_password %s",
                            vap_params->security_params.sae_password);
                   CHECK_AND_INCREMENT(index);
               }
               else {
                   acfg_log_errstr("%s: Exceeded max PSK key length\n", __func__);
                   return QDF_STATUS_E_FAILURE;
               }
            } else if(strlen(vap_params->security_params.psk)) {
               if (strlen(vap_params->security_params.psk) <= (ACFG_MAX_PSK_LEN - 2)) {
                  acfg_os_snprintf(acfg_hapd_param_list[index],
                           sizeof(acfg_hapd_param_list[index]), "SET wpa_passphrase %s",
                           vap_params->security_params.psk);
                  CHECK_AND_INCREMENT(index);
               } else if (strlen(vap_params->security_params.psk) ==
                      ACFG_MAX_PSK_LEN - 1)
               {
                  acfg_os_snprintf(acfg_hapd_param_list[index],
                           sizeof(acfg_hapd_param_list[index]), "SET wpa_psk %s",
                           vap_params->security_params.psk);
                  CHECK_AND_INCREMENT(index);
               }
            }
        }

        if (vap_params->security_params.sec_method ==
               ACFG_WLAN_PROFILE_SEC_METH_DPP) {
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET wpa_key_mgmt %s", "DPP");
            CHECK_AND_INCREMENT(index);
            if(vap_params->security_params.ieee80211w == 1) {
                acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET ieee80211w %d", 1);
                CHECK_AND_INCREMENT(index);
            }
        } else if (vap_params->security_params.sec_method ==
               ACFG_WLAN_PROFILE_SEC_METH_DPP_WPA3) {
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET wpa_key_mgmt %s", "DPP SAE");
            CHECK_AND_INCREMENT(index);
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET ieee80211w %d", 2);
            CHECK_AND_INCREMENT(index);

        } else if (vap_params->security_params.sec_method &
               ACFG_WLAN_PROFILE_SEC_METH_DPP_WPA2WPA3) {
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET wpa_key_mgmt %s", "DPP SAE WPA-PSK");
            CHECK_AND_INCREMENT(index);
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET ieee80211w %d", 1);
            CHECK_AND_INCREMENT(index);
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET sae_require_mfp %d", 1);
            CHECK_AND_INCREMENT(index);
        }

    } else if (vap_params->security_params.sec_method ==
               ACFG_WLAN_PROFILE_SEC_METH_SUITEB_192) {
        acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET wpa_key_mgmt %s", "WPA-EAP-SUITE-B-192");
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET ieee80211w %d", 2);
        CHECK_AND_INCREMENT(index);
    } else if (vap_params->security_params.sec_method ==
               ACFG_WLAN_PROFILE_SEC_METH_OPEN) {
        if(strlen (vap_params->security_params.owe_transition_ifname)) {
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET owe_transition_ifname %s", vap_params->security_params.owe_transition_ifname);
            CHECK_AND_INCREMENT(index);
        }
        if(strlen (vap_params->security_params.owe_transition_bssid)) {
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET owe_transition_bssid %s", vap_params->security_params.owe_transition_bssid);
            CHECK_AND_INCREMENT(index);
        }
        if(strlen (vap_params->security_params.owe_transition_ssid)) {
            acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET owe_transition_ssid \"%s\"", vap_params->security_params.owe_transition_ssid);
            CHECK_AND_INCREMENT(index);
        }
    } else if ((vap_params->security_params.sec_method ==
                ACFG_WLAN_PROFILE_SEC_METH_WPA_EAP) ||
            (vap_params->security_params.sec_method ==
             ACFG_WLAN_PROFILE_SEC_METH_WPA2_EAP))
    {
        acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET wpa_key_mgmt %s", "WPA-EAP");
        CHECK_AND_INCREMENT(index);

        switch (vap_params->security_params.ieee80211w) {
            case ACFG_WLAN_PROFILE_IEEE80211W_OPTIONAL:
                if(vap_params->security_params.sha256) {
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET wpa_key_mgmt %s %s", "WPA-EAP", "WPA-EAP-SHA256");
                    CHECK_AND_INCREMENT(index);
                }
                break;
            case ACFG_WLAN_PROFILE_IEEE80211W_REQUIRED:
                acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET wpa_key_mgmt %s", "WPA-EAP-SHA256");
                CHECK_AND_INCREMENT(index);
                break;
            default:
                break;
        }

        acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET ieee80211w %d", vap_params->security_params.ieee80211w);
        CHECK_AND_INCREMENT(index);

        if (vap_params->security_params.ieee80211w > 0) {

            if (vap_params->security_params.assoc_sa_query_max_timeout > 0) {
                acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET assoc_sa_query_max_timeout %d",
                        vap_params->security_params.assoc_sa_query_max_timeout);
                CHECK_AND_INCREMENT(index);
            }

            if (vap_params->security_params.assoc_sa_query_retry_timeout > 0) {
                acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET assoc_sa_query_retry_timeout %d",
                        vap_params->security_params.assoc_sa_query_retry_timeout);
                CHECK_AND_INCREMENT(index);
            }

            /* Set the group_mgmt_cipher value */
            acfg_log_errstr("Group mgmt cipher = %d\n", vap_params->security_params.group_mgmt_cipher);
            switch (vap_params->security_params.group_mgmt_cipher) {
                case ACFG_WLAN_PROFILE_GRP_MGMT_CIPHER_AES_128_CMAC:
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET group_mgmt_cipher %s",
                             "AES-128-CMAC");
                    break;
                case ACFG_WLAN_PROFILE_GRP_MGMT_CIPHER_BIP_GMAC_128:
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET group_mgmt_cipher %s",
                            "BIP-GMAC-128");
                    break;
                case ACFG_WLAN_PROFILE_GRP_MGMT_CIPHER_BIP_GMAC_256:
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list), "SET group_mgmt_cipher %s",
                            "BIP-GMAC-256");
                    break;
                case ACFG_WLAN_PROFILE_GRP_MGMT_CIPHER_BIP_CMAC_256:
                    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list),"SET group_mgmt_cipher %s",
                            "BIP-CMAC-256");
                    break;
                default:
                    break;
            }
            if (vap_params->security_params.group_mgmt_cipher >
                    ACFG_WLAN_PROFILE_GRP_MGMT_CIPHER_INVALID) {
                CHECK_AND_INCREMENT(index);
            }
        }

        acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list), "SET ieee8021x 1");
        CHECK_AND_INCREMENT(index);
    }

    memset (cipher, '\0', sizeof (cipher));
    acfg_get_cipher_str(vap_params->security_params.cipher_method,
            cipher);

    if ((strncmp(cipher , " TKIP", 5) == 0) && (strlen(cipher) == 5))
    {
        acfg_log_errstr("%s: TKIP only configuration not allowed, use mixed mode\n", __func__);
        return QDF_STATUS_E_FAILURE;
    }

    if ((vap_params->security_params.sec_method ==
                ACFG_WLAN_PROFILE_SEC_METH_WPA) ||
            (vap_params->security_params.sec_method ==
             ACFG_WLAN_PROFILE_SEC_METH_WPA_EAP))
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wpa %d", 1);
        CHECK_AND_INCREMENT(index);

        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wpa_pairwise %s",
                 cipher);
        CHECK_AND_INCREMENT(index);

    } else if ((vap_params->security_params.sec_method ==
                ACFG_WLAN_PROFILE_SEC_METH_WPA2) ||
            (vap_params->security_params.sec_method ==
             ACFG_WLAN_PROFILE_SEC_METH_WPA2_EAP) ||
            (vap_params->security_params.sec_method ==
             ACFG_WLAN_PROFILE_SEC_METH_WPA3) ||
            (vap_params->security_params.sec_method ==
             ACFG_WLAN_PROFILE_SEC_METH_WPA2WPA3) ||
            (vap_params->security_params.sec_method ==
             ACFG_WLAN_PROFILE_SEC_METH_OWE) ||
            (vap_params->security_params.sec_method ==
             ACFG_WLAN_PROFILE_SEC_METH_DPP_WPA2WPA3) ||
            (vap_params->security_params.sec_method ==
             ACFG_WLAN_PROFILE_SEC_METH_DPP_WPA3) ||
            (vap_params->security_params.sec_method ==
             ACFG_WLAN_PROFILE_SEC_METH_DPP))
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wpa %d", 2);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wpa_pairwise %s",
                 cipher);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET rsn_pairwise %s",
                 cipher);
        CHECK_AND_INCREMENT(index);
    } else if (vap_params->security_params.sec_method ==
            ACFG_WLAN_PROFILE_SEC_METH_SUITEB_192)
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wpa %d", 2);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET ieee8021x %d", 1);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET eapol_key_index_workaround %d", 1);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wpa_pairwise %s",
                 "GCMP-256");
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET rsn_pairwise %s",
                 "GCMP-256");
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]), "SET group_mgmt_cipher %s",
                 "BIP-GMAC-256");
        CHECK_AND_INCREMENT(index);
    } else if (vap_params->security_params.sec_method ==
            ACFG_WLAN_PROFILE_SEC_METH_WPAWPA2)
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wpa %d", 3);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                sizeof(acfg_hapd_param_list[index]), "SET wpa_pairwise %s",
                cipher);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET rsn_pairwise %s",
                 cipher);
        CHECK_AND_INCREMENT(index);
    } else {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wpa %d", 0);
        CHECK_AND_INCREMENT(index);
    }
    if (vap_params->security_params.wps_flag) {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),"SET wps_state %d",
                 vap_params->security_params.wps_flag);
        CHECK_AND_INCREMENT(index);
        if(vap_params->security_params.wps_config_methods[0] != 0)
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET config_methods %s",
                     vap_params->security_params.wps_config_methods);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.wps_device_type[0] != 0)
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "SET device_type %s",
                     vap_params->security_params.wps_device_type);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.wps_manufacturer[0] != 0)
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "SET manufacturer %s",
                     vap_params->security_params.wps_manufacturer);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.wps_model_name[0] != 0)
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "SET model_name %s",
                     vap_params->security_params.wps_model_name);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.wps_model_number[0] != 0)
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "SET model_number %s",
                     vap_params->security_params.wps_model_number);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.wps_serial_number[0] != 0)
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET serial_number %s",
                     vap_params->security_params.wps_serial_number);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.wps_device_name[0] != 0)
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "SET device_name %s",
                     vap_params->security_params.wps_device_name);
            CHECK_AND_INCREMENT(index);
        }
        if (vap_params->security_params.wps_upnp_iface[0] != 0) {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "SET upnp_iface %s",
                     vap_params->security_params.wps_upnp_iface);
            CHECK_AND_INCREMENT(index);
        }
        if (vap_params->security_params.wps_friendly_name[0] != 0) {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET friendly_name %s",
                     vap_params->security_params.wps_friendly_name);
            CHECK_AND_INCREMENT(index);
        }
        if (vap_params->security_params.wps_man_url[0] != 0) {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET manufacturer_url %s",
                     vap_params->security_params.wps_man_url);
            CHECK_AND_INCREMENT(index);
        }
        if (vap_params->security_params.wps_model_desc[0] != 0) {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET model_description %s",
                     vap_params->security_params.wps_model_desc);
            CHECK_AND_INCREMENT(index);
        }
        if (vap_params->security_params.wps_upc[0] != 0) {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "SET upc %s",
                     vap_params->security_params.wps_upc);
            CHECK_AND_INCREMENT(index);
        }
        if (vap_params->security_params.wps_pbc_in_m1) {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "SET pbc_in_m1 %d",
                    vap_params->security_params.wps_pbc_in_m1);
            CHECK_AND_INCREMENT(index);
        }

        if (vap_params->security_params.wps_rf_bands[0] != 0) {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET wps_rf_bands %s",
                     vap_params->security_params.wps_rf_bands);
            CHECK_AND_INCREMENT(index);
        }

    } else {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wps_state %d",
                 vap_params->security_params.wps_flag);
        CHECK_AND_INCREMENT(index);
    }
    if ((vap_params->security_params.sec2_acct_server_param.acct_ip[0] != 0) &&
            (vap_params->security_params.sec2_acct_server_param.acct_port != 0))
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET acct_server_addr %s",
                 vap_params->security_params.sec2_acct_server_param.acct_ip);
        CHECK_AND_INCREMENT(index);

        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET acct_server_port %d",
                 vap_params->security_params.sec2_acct_server_param.acct_port);
        CHECK_AND_INCREMENT(index);

        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET acct_server_shared_secret %s",
                 vap_params->security_params.sec2_acct_server_param.shared_secret);
        CHECK_AND_INCREMENT(index);
    }
    if ((vap_params->security_params.sec1_acct_server_param.acct_ip[0] != 0) &&
            (vap_params->security_params.sec1_acct_server_param.acct_port != 0))
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET acct_server_addr %s",
                 vap_params->security_params.sec1_acct_server_param.acct_ip);
        CHECK_AND_INCREMENT(index);

        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET acct_server_port %d",
                 vap_params->security_params.sec1_acct_server_param.acct_port);
        CHECK_AND_INCREMENT(index);

        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET acct_server_shared_secret %s",
                 vap_params->security_params.sec1_acct_server_param.shared_secret);
        CHECK_AND_INCREMENT(index);
    }
    if ((vap_params->security_params.pri_acct_server_param.acct_ip[0] != 0) &&
            (vap_params->security_params.pri_acct_server_param.acct_port != 0))
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET acct_server_addr %s",
                 vap_params->security_params.pri_acct_server_param.acct_ip);
        CHECK_AND_INCREMENT(index);

        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET acct_server_port %d",
                 vap_params->security_params.pri_acct_server_param.acct_port);
        CHECK_AND_INCREMENT(index);

        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET acct_server_shared_secret %s",
                 vap_params->security_params.pri_acct_server_param.shared_secret);
        CHECK_AND_INCREMENT(index);
    }
    if ((vap_params->security_params.hs_iw_param.hs_enabled == 1) &&
            (vap_params->security_params.hs_iw_param.iw_enabled == 1))
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET hs20 %d",
                 vap_params->security_params.hs_iw_param.hs_enabled);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET interworking %d",
                 vap_params->security_params.hs_iw_param.iw_enabled);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET access_network_type %d",
                 vap_params->security_params.hs_iw_param.network_type);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET internet %d",
                 vap_params->security_params.hs_iw_param.internet);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET asra %d",
                 vap_params->security_params.hs_iw_param.asra);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET esr %d",
                 vap_params->security_params.hs_iw_param.esr);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET uesa %d",
                 vap_params->security_params.hs_iw_param.uesa);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET venue_group %d",
                 vap_params->security_params.hs_iw_param.venue_group);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET venue_type %d",
                 vap_params->security_params.hs_iw_param.venue_type);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET hessid %s",
                 vap_params->security_params.hs_iw_param.hessid);
        CHECK_AND_INCREMENT(index);
        if(vap_params->security_params.hs_iw_param.roaming_consortium[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET roaming_consortium %s",
                     vap_params->security_params.hs_iw_param.roaming_consortium);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.roaming_consortium2[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET roaming_consortium %s",
                     vap_params->security_params.hs_iw_param.roaming_consortium2);
            CHECK_AND_INCREMENT(index);
        }
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET venue_name %s",
                 vap_params->security_params.hs_iw_param.venue_name);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET manage_p2p %d",
                 vap_params->security_params.hs_iw_param.manage_p2p);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET disable_dgaf %d",
                 vap_params->security_params.hs_iw_param.disable_dgaf);
        CHECK_AND_INCREMENT(index);
        if(vap_params->security_params.hs_iw_param.network_auth_type[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET network_auth_type %s",
                     vap_params->security_params.hs_iw_param.network_auth_type);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.ipaddr_type_availability[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET ipaddr_type_availability %s",
                     vap_params->security_params.hs_iw_param.ipaddr_type_availability);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.nai_realm_list[0][0] != '\0')
        {
            uint8_t nai_realam_index;

            for(nai_realam_index = 0; nai_realam_index < vap_params->num_nai_realm_data; nai_realam_index++)
            {
                if(vap_params->security_params.hs_iw_param.nai_realm_list[nai_realam_index][0] != '\0')
                {
                    acfg_os_snprintf(acfg_hapd_param_list[index],
                             sizeof(acfg_hapd_param_list[index]),
                             "SET nai_realm %s",
                             vap_params->security_params.hs_iw_param.nai_realm_list[nai_realam_index]);
                    CHECK_AND_INCREMENT(index);
                }
            }
        }
        if(vap_params->security_params.hs_iw_param.anqp_3gpp_cellular_network[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET anqp_3gpp_cell_net %s",
                     vap_params->security_params.hs_iw_param.anqp_3gpp_cellular_network);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.domain_name_list[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "SET domain_name %s",
                     vap_params->security_params.hs_iw_param.domain_name_list);
            CHECK_AND_INCREMENT(index);
        }
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET gas_frag_limit %d",
                 vap_params->security_params.hs_iw_param.gas_frag_limit);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET gas_comeback_delay %d",
                 vap_params->security_params.hs_iw_param.gas_comeback_delay);
        CHECK_AND_INCREMENT(index);
        if(vap_params->security_params.hs_iw_param.qos_map_set[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "SET qos_map_set %s",
                     vap_params->security_params.hs_iw_param.qos_map_set);
            CHECK_AND_INCREMENT(index);
        }
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET proxy_arp %d",
                 vap_params->security_params.hs_iw_param.proxy_arp);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET osen %d",
                 vap_params->security_params.hs_iw_param.osen);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET anqp_domain_id %d",
                 vap_params->security_params.hs_iw_param.anqp_domain_id);
        CHECK_AND_INCREMENT(index);
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET hs20_deauth_req_timeout %d",
                 vap_params->security_params.hs_iw_param.hs20_deauth_req_timeout);
        CHECK_AND_INCREMENT(index);
        if(vap_params->security_params.hs_iw_param.hs20_operator_friendly_name[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET hs20_oper_friendly_name %s",
                     vap_params->security_params.hs_iw_param.hs20_operator_friendly_name);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.hs20_wan_metrics[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list),
                     "SET hs20_wan_metrics %s",
                     vap_params->security_params.hs_iw_param.hs20_wan_metrics);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.hs20_conn_capab[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET hs20_conn_capab %s",
                     vap_params->security_params.hs_iw_param.hs20_conn_capab);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.hs20_operating_class[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET hs20_operating_class %s",
                     vap_params->security_params.hs_iw_param.hs20_operating_class);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.hs20_icon[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),"SET hs20_icon %s",
                     vap_params->security_params.hs_iw_param.hs20_icon);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.osu_ssid[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),"SET osu_ssid %s",
                     vap_params->security_params.hs_iw_param.osu_ssid);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.osu_server_uri[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET osu_server_uri %s",
                     vap_params->security_params.hs_iw_param.osu_server_uri);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.osu_friendly_name[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET osu_friendly_name %s",
                     vap_params->security_params.hs_iw_param.osu_friendly_name);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.osu_nai[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "SET osu_nai %s",
                     vap_params->security_params.hs_iw_param.osu_nai);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.osu_method_list[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET osu_method_list %s",
                     vap_params->security_params.hs_iw_param.osu_method_list);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.osu_icon[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),"SET osu_icon %s",
                    vap_params->security_params.hs_iw_param.osu_icon);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.osu_service_desc[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET osu_service_desc %s",
                     vap_params->security_params.hs_iw_param.osu_service_desc);
            CHECK_AND_INCREMENT(index);
        }
        if(vap_params->security_params.hs_iw_param.subscr_remediation_url[0] != '\0')
        {
            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]),
                     "SET subscr_remediation_url %s",
                     vap_params->security_params.hs_iw_param.subscr_remediation_url);
            CHECK_AND_INCREMENT(index);
        }
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET subscr_remediation_method %d",
                 vap_params->security_params.hs_iw_param.subscr_remediation_method);
        CHECK_AND_INCREMENT(index);
    }
#if !HAPD_CONF_FILE_UPDATE
    for (i = 0; i < index ; i++) {
        if((acfg_ctrl_req (vap_params->vap_name,
                        acfg_hapd_param_list[i],
                        strlen(acfg_hapd_param_list[i]),
                        replybuf, &len,
                        ACFG_OPMODE_HOSTAP) < 0) ||
                strncmp (replybuf, "OK", strlen("OK"))){
            acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                    acfg_hapd_param_list[i],
                    vap_params->vap_name);
            return QDF_STATUS_E_FAILURE;
        }
    }
#else
    memset((char *)file_name, 0, sizeof (file_name));
    acfg_os_snprintf(file_name, sizeof(file_name), "/var/run/hostapd-%s.conf", vap_params->vap_name);

    src_idx_start = sizeof("SET ") - 1;
    if(QDF_STATUS_SUCCESS != acfg_write_conf_file((char *)acfg_hapd_param_list, 1051, file_name, index, src_idx_start, 0))
        return QDF_STATUS_E_FAILURE;
#endif
    return QDF_STATUS_SUCCESS;
}

static void
acfg_update_hapd_params(acfg_wlan_profile_vap_params_t *vap_params, acfg_hapd_param_t *hapd_params)
{
    acfg_wlan_profile_radio_params_t *radio_params = vap_params->radio_params;
    uint8_t chan = radio_params->chan;
    uint8_t band = radio_params->chan_band;
    int shortgi;
    FILE * htcap;
    FILE * vhtcap;
    int htcap_size = 0;
    int vhtcap_size = 0;
    char htcap_file[40];
    char vhtcap_file[40];
    char *htcap_buffer;
    char *vhtcap_buffer;
    int ch, ret = 0;

    memset((char *)hapd_params, 0, sizeof (acfg_hapd_param_t));
    memset((char *)htcap_file, 0, sizeof (htcap_file));
    memset((char *)vhtcap_file, 0, sizeof (vhtcap_file));
    memset((char *)hapd_params->ht_capabs, 0, sizeof (hapd_params->ht_capabs));
    memset((char *)hapd_params->vht_capabs, 0, sizeof (hapd_params->vht_capabs));

    htcap_buffer = hapd_params->ht_capabs;
    vhtcap_buffer = hapd_params->vht_capabs;

    ret = snprintf(htcap_file, sizeof(htcap_file), "/sys/class/net/%s/cfg80211_htcaps", vap_params->vap_name);
    if ((ret < 0) || (ret >= (int)sizeof(htcap_file))) {
        acfg_print("Failed snprintf\n");
        return;
    }
    ret = snprintf(vhtcap_file, sizeof(vhtcap_file), "/sys/class/net/%s/cfg80211_vhtcaps", vap_params->vap_name);
    if ((ret < 0) || (ret >= (int)sizeof(vhtcap_file))) {
        acfg_print("Failed snprintf\n");
        return;
    }
    htcap = (FILE*) fopen(htcap_file, "r");
    if(htcap == NULL)
    {
        acfg_print("Error to read htcap file!\n");
        return;
    }
    for (htcap_size = 0; (((ch = fgetc(htcap)) != EOF) && (ch != '\n') && (htcap_size < HTCAP_FILE_SIZE)); htcap_size++) {
        htcap_buffer[htcap_size] = ch;
    }
    htcap_buffer[htcap_size] = '\0';
    fclose(htcap);

    vhtcap = (FILE*) fopen(vhtcap_file, "r");
    if(vhtcap == NULL)
    {
        acfg_print("Error to read vhtcap file!\n");
        return;
    }

    for (vhtcap_size = 0; (((ch = fgetc(vhtcap)) != EOF) && (ch != '\n') && (vhtcap_size < VHTCAP_FILE_SIZE)); vhtcap_size++) {
        vhtcap_buffer[vhtcap_size] = ch;
    }
    vhtcap_buffer[vhtcap_size] = '\0';
    fclose(vhtcap);
    shortgi = (vap_params->shortgi == (uint8_t)-1) ? 0 : vap_params->shortgi;

    switch (vap_params->phymode) {
        case ACFG_PHYMODE_AUTO:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"any", sizeof(hapd_params->hw_mode));
            break;
        case ACFG_PHYMODE_11B:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"g", sizeof(hapd_params->hw_mode));
            break;
        case ACFG_PHYMODE_11G:
        case ACFG_PHYMODE_FH:
        case ACFG_PHYMODE_TURBO_G:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"g", sizeof(hapd_params->hw_mode));
            break;
        case ACFG_PHYMODE_11A:
        case ACFG_PHYMODE_TURBO_A:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            break;
        case ACFG_PHYMODE_11NG_HT20:
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT20]"), "[HT20]");
            htcap_size = htcap_size + sizeof("[HT20]") - 1;
            if (shortgi) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-20]"), "[SHORT-GI-20]");
                htcap_size = htcap_size + sizeof("[SHORT-GI-20]") - 1;
            }
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"g", sizeof(hapd_params->hw_mode));
            hapd_params->ieee80211n = 1;
            break;
        case ACFG_PHYMODE_11NG_HT40:
            if ((chan == 1) || (chan == 2)|| (chan == 3)|| (chan == 4) ||
                    (chan == 5) || (chan == 6)|| (chan == 7)) {
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40+]"), "[HT40+]");
            htcap_size = htcap_size + sizeof("[HT40+]") - 1;
            } else if ((chan == 8) || (chan == 9)|| (chan == 10)|| (chan == 11) ||
                    (chan == 12) || (chan == 13)) {
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40-]"), "[HT40-]");
            htcap_size = htcap_size + sizeof("[HT40-]" - 1);
            } else {
                acfg_print("invalid channel\n");
            }
            if (shortgi) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-40]"), "[SHORT-GI-40]");
                htcap_size = htcap_size + sizeof("[SHORT-GI-40]") - 1;
            }
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"g", sizeof(hapd_params->hw_mode));
            hapd_params->ieee80211n = 1;
            break;
        case ACFG_PHYMODE_11NG_HT40PLUS:
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40+]"), "[HT40+]");
            htcap_size = htcap_size + sizeof("[HT40+]") - 1;
            if (shortgi) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-40]"), "[SHORT-GI-40]");
                htcap_size = htcap_size + sizeof("[SHORT-GI-40]") - 1;
            }
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"g", sizeof(hapd_params->hw_mode));
            hapd_params->ieee80211n = 1;
            break;
        case ACFG_PHYMODE_11NG_HT40MINUS:
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40-]"), "[HT40-]");
            htcap_size = htcap_size + sizeof("[HT40-]") - 1;

            if (shortgi) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-40]"), "[SHORT-GI-40]");
                htcap_size = htcap_size + sizeof("[SHORT-GI-40]") - 1;
            }
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"g", sizeof(hapd_params->hw_mode));
            hapd_params->ieee80211n = 1;
            break;
        case ACFG_PHYMODE_11NA_HT20:
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT20]"), "[HT20]");
            htcap_size = htcap_size + sizeof("[HT20]" - 1);
            if (shortgi) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-20]"), "[SHORT-GI-20]");
                htcap_size = htcap_size + sizeof("[SHORT-GI-20]") - 1;
            }
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            hapd_params->ieee80211n = 1;
            break;
        case ACFG_PHYMODE_11NA_HT40:
            if ((chan == 36) || (chan == 44) || (chan == 52) || (chan == 60) ||
                    (chan == 100) || (chan == 108) || (chan == 116) || (chan == 124) ||
                    (chan == 132) || (chan == 140) || (chan == 149) || (chan == 157)) {
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40+]"), "[HT40+]");
            htcap_size = htcap_size + sizeof("[HT40+]") - 1;
            } else if ((chan == 40) || (chan == 48) || (chan == 56) || (chan == 64) ||
                        (chan == 104) || (chan == 112) || (chan == 120) || (chan == 128) ||
                        (chan == 136) || (chan == 144) || (chan == 153) || (chan == 161)) {
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40-]"), "[HT40-]");
            htcap_size = htcap_size + sizeof("[HT40-]") - 1;
            } else {
                acfg_print("invalid channel\n");
            }
            if (shortgi) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-40]"), "[SHORT-GI-40]");
                htcap_size = htcap_size + sizeof("[SHORT-GI-40]") - 1;
            }
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            hapd_params->ieee80211n = 1;
            break;
        case ACFG_PHYMODE_11NA_HT40PLUS:
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40+]"), "[HT40+]");
            htcap_size = htcap_size + sizeof("[HT40+]" - 1);
            if (shortgi) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-40]"), "[SHORT-GI-40]");
                htcap_size = htcap_size + sizeof("[SHORT-GI-40]") - 1;
            }
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            hapd_params->ieee80211n = 1;
            break;
        case ACFG_PHYMODE_11NA_HT40MINUS:
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40-]"), "[HT40-]");
            htcap_size = htcap_size + sizeof("[HT40-]" - 1);
            if (shortgi) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-40]"), "[SHORT-GI-40]");
                htcap_size = htcap_size + sizeof("[SHORT-GI-40]") - 1;
            }
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            hapd_params->ieee80211n = 1;
            break;
        case ACFG_PHYMODE_11AC_VHT20:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            if (shortgi) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-20]"), "[SHORT-GI-20]");
                htcap_size = htcap_size + sizeof("[SHORT-GI-20]") - 1;
            }
            hapd_params->ieee80211ac = 1;
            hapd_params->ieee80211n = 1;
            hapd_params->vht_oper_chwidth = 0;
            hapd_params->vht_oper_centr_freq_seg0_idx = chan;
            break;
        case ACFG_PHYMODE_11AC_VHT40PLUS:
        case ACFG_PHYMODE_11AC_VHT40MINUS:
        case ACFG_PHYMODE_11AC_VHT40:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            hapd_params->ieee80211ac = 1;
            hapd_params->ieee80211n = 1;
            hapd_params->vht_oper_chwidth = 0;
            if ((chan == 36) || (chan == 40)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 38;
            } else if ((chan == 44) || (chan == 48)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 46;
            } else if ((chan == 52) || (chan == 56)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 54;
            } else if ((chan == 60) || (chan == 64)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 62;
            } else if ((chan == 100) || (chan == 104)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 102;
            } else if ((chan == 108) || (chan == 112)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 110;
            } else if ((chan == 116) || (chan == 120)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 118;
            } else if ((chan == 124) || (chan == 128)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 126;
            } else if ((chan == 132) || (chan == 136)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 134;
            } else if ((chan == 140) || (chan == 144)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 142;
            } else if ((chan == 149) || (chan == 153)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 151;
            } else if ((chan == 157) || (chan == 161)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 159;
            } else {
                acfg_print("invalid channel\n");
            }
            if ((chan == 36) || (chan == 44) || (chan == 52) || (chan == 60) ||
                    (chan == 100) || (chan == 108) || (chan == 116) || (chan == 124) ||
                    (chan == 132) || (chan == 140) || (chan == 149) || (chan == 157)) {
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40+]"), "[HT40+]");
            htcap_size = htcap_size + sizeof("[HT40+]") - 1;
            } else if ((chan == 40) || (chan == 48) || (chan == 56) || (chan == 64) ||
                        (chan == 104) || (chan == 112) || (chan == 120) || (chan == 128) ||
                        (chan == 136) || (chan == 144) || (chan == 153) || (chan == 161)) {
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40-]"), "[HT40-]");
            htcap_size = htcap_size + sizeof("[HT40-]") - 1;
            } else {
                acfg_print("invalid channel\n");
            }
            if (shortgi) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-40]"), "[SHORT-GI-40]");
                htcap_size = htcap_size + sizeof("[SHORT-GI-40]") - 1;
            }

            break;
        case ACFG_PHYMODE_11AC_VHT80:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            hapd_params->ieee80211ac = 1;
            hapd_params->ieee80211n = 1;
            hapd_params->vht_oper_chwidth = 1;
            if ((chan == 36) || (chan == 40)|| (chan == 44) || (chan == 48)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 42;
            } else if ((chan == 52) || (chan == 56) || (chan == 60)|| (chan == 64)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 58;
            } else if ((chan == 100) || (chan == 104) || (chan == 108)|| (chan == 112)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 106;
            } else if ((chan == 116) || (chan == 120) || (chan == 124)|| (chan == 128)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 122;
            } else if ((chan == 132) || (chan == 136) || (chan == 140)|| (chan == 144)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 138;
            } else if ((chan == 149) || (chan == 153) || (chan == 157)|| (chan == 161)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 155;
            } else {
                acfg_print("invalid channel\n");
            }
            if ((chan == 36) || (chan == 44) || (chan == 52) || (chan == 60) ||
                    (chan == 100) || (chan == 108) || (chan == 116) || (chan == 124) ||
                    (chan == 132) || (chan == 140) || (chan == 149) || (chan == 157)) {
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40+]"), "[HT40+]");
            htcap_size = htcap_size + sizeof("[HT40+]") - 1;
            } else if ((chan == 40) || (chan == 48) || (chan == 56) || (chan == 64) ||
                        (chan == 104) || (chan == 112) || (chan == 120) || (chan == 128) ||
                        (chan == 136) || (chan == 144) || (chan == 153) || (chan == 161)) {
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40-]"), "[HT40-]");
            htcap_size = htcap_size + sizeof("[HT40-]") - 1;
            } else {
                acfg_print("invalid channel\n");
            }
            if (shortgi) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-40]"), "[SHORT-GI-40]");
                htcap_size = htcap_size + sizeof("[SHORT-GI-40]") - 1;
            }

            break;
        case ACFG_PHYMODE_11AC_VHT80_80:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            break;
        case ACFG_PHYMODE_11AC_VHT160:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            hapd_params->ieee80211ac = 1;
            hapd_params->ieee80211n = 1;
            hapd_params->vht_oper_chwidth = 2;
            if ((chan == 36) || (chan == 40) || (chan == 44)|| (chan == 48) ||
                    (chan == 52) || (chan == 56)|| (chan == 60)|| (chan == 64)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 50;
            } else if ((chan == 100) || (chan == 104) || (chan == 108)|| (chan == 112) ||
                    (chan == 116) || (chan == 120) || (chan == 124)|| (chan == 128)) {
                hapd_params->vht_oper_centr_freq_seg0_idx = 114;
            } else {
                acfg_print("invalid channel\n");
            }
            if ((chan == 36) || (chan == 44) || (chan == 52) || (chan == 60) ||
                    (chan == 100) || (chan == 108) || (chan == 116) || (chan == 124) ||
                    (chan == 132) || (chan == 140) || (chan == 149) || (chan == 157)) {
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40+]"), "[HT40+]");
            htcap_size = htcap_size + sizeof("[HT40+]") - 1;
            } else if ((chan == 40) || (chan == 48) || (chan == 56) || (chan == 64) ||
                        (chan == 104) || (chan == 112) || (chan == 120) || (chan == 128) ||
                        (chan == 136) || (chan == 144) || (chan == 153) || (chan == 161)) {
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40-]"), "[HT40-]");
            htcap_size = htcap_size + sizeof("[HT40-]") - 1;
            } else {
                acfg_print("invalid channel\n");
            }
            if (shortgi) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-40]"), "[SHORT-GI-40]");
                htcap_size = htcap_size + sizeof("[SHORT-GI-40]") - 1;
            }
            break;
        case ACFG_PHYMODE_11AXG_HE20:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"g", sizeof(hapd_params->hw_mode));
            snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT20]"), "[HT20]");
            htcap_size = htcap_size + sizeof("[HT20]" - 1);
            if (shortgi) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-20]"), "[SHORT-GI-20]");
                htcap_size = htcap_size + sizeof("[SHORT-GI-20]") - 1;
            }
            hapd_params->ieee80211n = 1;
            hapd_params->ieee80211ac = 1;
            break;
        case ACFG_PHYMODE_11AXG_HE40PLUS:
        case ACFG_PHYMODE_11AXG_HE40MINUS:
        case ACFG_PHYMODE_11AXG_HE40:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"g", sizeof(hapd_params->hw_mode));
            if ((chan == 1) || (chan == 2) || (chan == 3) || (chan == 4) ||
                (chan == 5) || (chan == 6) || (chan == 7)) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40+]"), "[HT40+]");
                htcap_size = htcap_size + sizeof("[HT40+]") - 1;
            } else if ((chan == 8) || (chan == 9) || (chan == 10) || (chan == 11) ||
                       (chan == 12) || (chan == 13)) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40-]"), "[HT40-]");
                htcap_size = htcap_size + sizeof("[HT40-]") - 1;
            } else {
                acfg_print("invalid channel\n");
            }
            if (shortgi) {
                snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-40]"), "[SHORT-GI-40]");
                htcap_size = htcap_size + sizeof("[SHORT-GI-40]") - 1;
            }
            hapd_params->ieee80211n = 1;
            hapd_params->ieee80211ac = 1;
            break;
        case ACFG_PHYMODE_11AXA_HE20:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            if (band != 3) {
                if (shortgi) {
                    snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-20]"), "[SHORT-GI-20]");
                    htcap_size = htcap_size + sizeof("[SHORT-GI-20]") - 1;
                }
                hapd_params->ieee80211ac = 1;
                hapd_params->ieee80211n = 1;
                hapd_params->vht_oper_chwidth = 0;
                hapd_params->vht_oper_centr_freq_seg0_idx = chan;
            } else
                hapd_params->op_class = 131;
            break;
        case ACFG_PHYMODE_11AXA_HE40PLUS:
        case ACFG_PHYMODE_11AXA_HE40MINUS:
        case ACFG_PHYMODE_11AXA_HE40:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            if (band != 3) {
                hapd_params->ieee80211ac = 1;
                hapd_params->ieee80211n = 1;
                hapd_params->vht_oper_chwidth = 0;
                if ((chan == 36) || (chan == 40)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 38;
                } else if ((chan == 44) || (chan == 48)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 46;
                } else if ((chan == 52) || (chan == 56)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 54;
                } else if ((chan == 60) || (chan == 64)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 62;
                } else if ((chan == 100) || (chan == 104)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 102;
                } else if ((chan == 108) || (chan == 112)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 110;
                } else if ((chan == 116) || (chan == 120)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 118;
                } else if ((chan == 124) || (chan == 128)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 126;
                } else if ((chan == 132) || (chan == 136)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 134;
                } else if ((chan == 140) || (chan == 144)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 142;
                } else if ((chan == 149) || (chan == 153)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 151;
                } else if ((chan == 157) || (chan == 161)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 159;
                } else {
                    acfg_print("invalid channel\n");
                }
                if ((chan == 36) || (chan == 44) || (chan == 52) || (chan == 60) ||
                    (chan == 100) || (chan == 108) || (chan == 116) || (chan == 124) ||
                    (chan == 132) || (chan == 140) || (chan == 149) || (chan == 157)) {
                    snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40+]"), "[HT40+]");
                    htcap_size = htcap_size + sizeof("[HT40+]") - 1;
                } else if ((chan == 40) || (chan == 48) || (chan == 56) || (chan == 64) ||
                        (chan == 104) || (chan == 112) || (chan == 120) || (chan == 128) ||
                        (chan == 136) || (chan == 144) || (chan == 153) || (chan == 161)) {
                    snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40-]"), "[HT40-]");
                    htcap_size = htcap_size + sizeof("[HT40-]") - 1;
                } else {
                    acfg_print("invalid channel\n");
                }
                if (shortgi) {
                    snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-40]"), "[SHORT-GI-40]");
                    htcap_size = htcap_size + sizeof("[SHORT-GI-40]") - 1;
                }
            } else
                hapd_params->op_class = 132;
            break;
        case ACFG_PHYMODE_11AXA_HE80:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            if (band != 3) {
                hapd_params->ieee80211ac = 1;
                hapd_params->ieee80211n = 1;
                hapd_params->vht_oper_chwidth = 1;
                if ((chan == 36) || (chan == 40)|| (chan == 44) || (chan == 48)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 42;
                } else if ((chan == 52) || (chan == 56) || (chan == 60)|| (chan == 64)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 58;
                } else if ((chan == 100) || (chan == 104) || (chan == 108)|| (chan == 112)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 106;
                } else if ((chan == 116) || (chan == 120) || (chan == 124)|| (chan == 128)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 122;
                } else if ((chan == 132) || (chan == 136) || (chan == 140)|| (chan == 144)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 138;
                } else if ((chan == 149) || (chan == 153) || (chan == 157)|| (chan == 161)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 155;
                } else {
                    acfg_print("invalid channel\n");
                }
                if ((chan == 36) || (chan == 44) || (chan == 52) || (chan == 60) ||
                    (chan == 100) || (chan == 108) || (chan == 116) || (chan == 124) ||
                    (chan == 132) || (chan == 140) || (chan == 149) || (chan == 157)) {
                    snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40+]"), "[HT40+]");
                    htcap_size = htcap_size + sizeof("[HT40+]") - 1;
                } else if ((chan == 40) || (chan == 48) || (chan == 56) || (chan == 64) ||
                        (chan == 104) || (chan == 112) || (chan == 120) || (chan == 128) ||
                        (chan == 136) || (chan == 144) || (chan == 153) || (chan == 161)) {
                    snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40-]"), "[HT40-]");
                    htcap_size = htcap_size + sizeof("[HT40-]") - 1;
                } else {
                    acfg_print("invalid channel\n");
                }
                if (shortgi) {
                    snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-40]"), "[SHORT-GI-40]");
                    htcap_size = htcap_size + sizeof("[SHORT-GI-40]") - 1;
                }
            } else
                hapd_params->op_class = 133;
            break;
        case ACFG_PHYMODE_11AXA_HE160:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            if (band != 3) {
                hapd_params->ieee80211ac = 1;
                hapd_params->ieee80211n = 1;
                hapd_params->vht_oper_chwidth = 2;
                if ((chan == 36) || (chan == 40) || (chan == 44)|| (chan == 48) ||
                    (chan == 52) || (chan == 56)|| (chan == 60)|| (chan == 64)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 50;
                } else if ((chan == 100) || (chan == 104) || (chan == 108)|| (chan == 112) ||
                    (chan == 116) || (chan == 120) || (chan == 124)|| (chan == 128)) {
                    hapd_params->vht_oper_centr_freq_seg0_idx = 114;
                } else {
                    acfg_print("invalid channel\n");
                }
                if ((chan == 36) || (chan == 44) || (chan == 52) || (chan == 60) ||
                    (chan == 100) || (chan == 108) || (chan == 116) || (chan == 124) ||
                    (chan == 132) || (chan == 140) || (chan == 149) || (chan == 157)) {
                    snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40+]"), "[HT40+]");
                    htcap_size = htcap_size + sizeof("[HT40+]") - 1;
                } else if ((chan == 40) || (chan == 48) || (chan == 56) || (chan == 64) ||
                        (chan == 104) || (chan == 112) || (chan == 120) || (chan == 128) ||
                        (chan == 136) || (chan == 144) || (chan == 153) || (chan == 161)) {
                    snprintf(&(htcap_buffer[htcap_size]), sizeof("[HT40-]"), "[HT40-]");
                    htcap_size = htcap_size + sizeof("[HT40-]") - 1;
                } else {
                    acfg_print("invalid channel\n");
                }
                if (shortgi) {
                    snprintf(&(htcap_buffer[htcap_size]), sizeof("[SHORT-GI-40]"), "[SHORT-GI-40]");
                    htcap_size = htcap_size + sizeof("[SHORT-GI-40]") - 1;
                }
            } else
                hapd_params->op_class = 134;
            break;
        case ACFG_PHYMODE_11AXA_HE80_80:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"a", sizeof(hapd_params->hw_mode));
            if (band != 3) {
                hapd_params->ieee80211n = 1;
                hapd_params->ieee80211ac = 1;
            } else
                hapd_params->op_class = 132;
            break;
        default:
            acfg_os_strcpy((char *)hapd_params->hw_mode, (char *)"any", sizeof(hapd_params->hw_mode));
            break;
    }

    return;
}
#if !HAPD_CONF_FILE_UPDATE
static uint32_t
acfg_set_hapd_radio_params(acfg_wlan_profile_vap_params_t *vap_params)
{
    acfg_hapd_param_t hapd_params;
    int index = 0, i;
    char replybuf[255] = {0};
    uint32_t len = sizeof (replybuf);
    char acfg_hapd_param_list[ACFG_MAX_HAPD_CONFIG_PARAM+1][1051];//changed 1024 to 1051

    acfg_update_hapd_params(vap_params, &hapd_params);
    if (vap_params->radio_params->chan == 0) {
    snprintf(acfg_hapd_param_list[index],
            sizeof(acfg_hapd_param_list[index]),
            "SET channel auto");
    } else {
    snprintf(acfg_hapd_param_list[index],
            sizeof(acfg_hapd_param_list[index]),
            "SET channel %d",vap_params->radio_params->chan);
    }
    CHECK_AND_INCREMENT(index);
    snprintf(acfg_hapd_param_list[index],
            sizeof(acfg_hapd_param_list[index]),
            "SET ieee80211n %d",hapd_params.ieee80211n);
    CHECK_AND_INCREMENT(index);
    snprintf(acfg_hapd_param_list[index],
            sizeof(acfg_hapd_param_list[index]),
            "SET ieee80211ac %d",hapd_params.ieee80211ac);
    CHECK_AND_INCREMENT(index);
    snprintf(acfg_hapd_param_list[index],
            sizeof(acfg_hapd_param_list[index]),
            "SET vht_oper_chwidth %d",hapd_params.vht_oper_chwidth);
    CHECK_AND_INCREMENT(index);
    snprintf(acfg_hapd_param_list[index],
            sizeof(acfg_hapd_param_list[index]),
            "SET vht_oper_centr_freq_seg0_idx %d",hapd_params.vht_oper_centr_freq_seg0_idx);
    CHECK_AND_INCREMENT(index);
    snprintf(acfg_hapd_param_list[index],
            sizeof(acfg_hapd_param_list[index]),
            "SET ht_capab %s",hapd_params.ht_capabs);
    CHECK_AND_INCREMENT(index);
    snprintf(acfg_hapd_param_list[index],
            sizeof(acfg_hapd_param_list[index]),
            "SET vht_capab %s",hapd_params.vht_capabs);
    CHECK_AND_INCREMENT(index);
    snprintf(acfg_hapd_param_list[index],
            sizeof(acfg_hapd_param_list[index]),
            "SET hw_mode %s",hapd_params.hw_mode);
    CHECK_AND_INCREMENT(index);
    snprintf(acfg_hapd_param_list[index],
            sizeof(acfg_hapd_param_list[index]),
            "SET op_class %d",hapd_params.op_class);
    CHECK_AND_INCREMENT(index);
    snprintf(acfg_hapd_param_list[index],
            sizeof(acfg_hapd_param_list[index]),
            "SET wmm_enabled %d",1);
    CHECK_AND_INCREMENT(index);
    for (i = 0; i < index; i++) {
        if((acfg_ctrl_req (vap_params->vap_name,
                        acfg_hapd_param_list[i],
                        strlen(acfg_hapd_param_list[i]),
                        replybuf, &len,
                        ACFG_OPMODE_HOSTAP) < 0) ||
                strncmp (replybuf, "OK", strlen("OK"))){
            acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                    acfg_hapd_param_list[i],
                    vap_params->vap_name);
            return QDF_STATUS_E_FAILURE;
        }
    }
    return QDF_STATUS_SUCCESS;
}
#else
static uint32_t
acfg_create_hostapd_conf_file(acfg_wlan_profile_vap_params_t *vap_params)
{
    char file_name[30];
    FILE *fptr;
    acfg_hapd_param_t hapd_params;
    char command[MAX_CMD_LEN] = {0};
    int err = 0;

    memset((char *)file_name, 0, sizeof (file_name));
    snprintf(file_name, sizeof(file_name), "/var/run/hostapd-%s.conf", vap_params->vap_name);

    /*Remove old conf file before creating new one*/
    if(0 > snprintf(command, sizeof(command),"rm -rf %s", file_name)) {
        return QDF_STATUS_E_FAILURE;
    }
    err = system(command);
    if(err != 0)
    {
        printf("Not able to remove conf file\n");
        return QDF_STATUS_E_FAILURE;
    }

    acfg_update_hapd_params(vap_params, &hapd_params);

    fptr = fopen(file_name, "w");
    if(fptr == NULL)
    {
        acfg_print("Error to create hostapd conf file!\n");
        return QDF_STATUS_E_FAILURE;
    }
    fprintf (fptr, "\ndriver=nl80211");
    fprintf (fptr, "\ninterface=%s",vap_params->vap_name);
    if (vap_params->radio_params->chan == 0) {
        fprintf (fptr, "\nchannel=auto");
    } else {
        fprintf (fptr, "\nchannel=%d",vap_params->radio_params->chan);
    fprintf (fptr, "\nieee80211n=%d",hapd_params.ieee80211n);
    fprintf (fptr, "\nieee80211ac=%d",hapd_params.ieee80211ac);
    fprintf (fptr, "\nvht_oper_chwidth=%d",hapd_params.vht_oper_chwidth);
    fprintf (fptr, "\nvht_oper_centr_freq_seg0_idx=%d",hapd_params.vht_oper_centr_freq_seg0_idx);
    }
    fprintf (fptr, "\nht_capab=%s",hapd_params.ht_capabs);
    fprintf (fptr, "\nvht_capab=%s",hapd_params.vht_capabs);
    fprintf (fptr, "\nhw_mode=%s",hapd_params.hw_mode);
    fprintf (fptr, "\nop_class=%d",hapd_params.op_class);
    fprintf (fptr, "\nwmm_enabled=%d",1);
    fprintf (fptr, "\nctrl_interface=/var/run/hostapd");
    fclose(fptr);
    return QDF_STATUS_SUCCESS;
}
#endif

uint32_t
acfg_hostapd_add_bss(acfg_wlan_profile_vap_params_t *vap_params, int8_t *sec)
{
    char buffer[4096];
    char replybuf[255] = {0};
    uint32_t len = sizeof (replybuf);

    if(ACFG_IS_WPS_WEP_ENABLED(vap_params->security_params)) {
        *sec = 0;
        return QDF_STATUS_SUCCESS;
    }
    /*Add bss for both open and security mode*/

    if (!g_sock_ctx.cfg80211) {
        acfg_os_snprintf(buffer, sizeof(buffer), "ADD %s %s %s",
                vap_params->vap_name, ctrl_hapd, WPA_DRIVER_ATHEROS);
    } else {
#if HAPD_CONF_FILE_UPDATE
        if (acfg_create_hostapd_conf_file(vap_params) != QDF_STATUS_SUCCESS) {
            return QDF_STATUS_E_FAILURE;
        }
        if(QDF_STATUS_SUCCESS != acfg_set_hapd_config_params(vap_params)){
            acfg_log_errstr("%s: Failed to configure security for %s\n", __func__,
                    vap_params->vap_name);
            return QDF_STATUS_E_FAILURE;
        }
        acfg_os_snprintf(buffer, sizeof(buffer), "ADD bss_config=%s:/var/run/hostapd-%s.conf",
                vap_params->vap_name, vap_params->vap_name);
#else
        acfg_os_snprintf(buffer, sizeof(buffer), "ADD %s %s %s",
                vap_params->vap_name, ctrl_hapd, WPA_DRIVER_NL80211);
#endif
    }
    if((acfg_ctrl_req ((uint8_t *)ACFG_HAPD_GLOBAL_CTRL_IFACE,
                    buffer, strlen(buffer),
                    replybuf, &len,
                    ACFG_OPMODE_HOSTAP) < 0) ||
            strncmp (replybuf, "OK", strlen("OK"))){
        acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                buffer,
                vap_params->vap_name);
        return QDF_STATUS_E_FAILURE;
    }

#if !HAPD_CONF_FILE_UPDATE
    if(QDF_STATUS_SUCCESS != acfg_set_hapd_config_params(vap_params)){
        acfg_log_errstr("%s: Failed to configure security for %s\n", __func__,
                vap_params->vap_name);
        return QDF_STATUS_E_FAILURE;
    }
    if (acfg_set_hapd_radio_params(vap_params) != QDF_STATUS_SUCCESS) {
        return QDF_STATUS_E_FAILURE;
    }
#endif
    *sec = 1;
    return QDF_STATUS_SUCCESS;
}

int8_t
acfg_check_reset(acfg_wlan_profile_vap_params_t *vap_params,
        acfg_wlan_profile_vap_params_t *cur_vap_params)
{
    int8_t ret = 0;

    if (strcmp(vap_params->security_params.wps_upnp_iface,
                vap_params->security_params.wps_upnp_iface))
    {
        ret = 1;
    }
    if (strcmp(vap_params->bridge, cur_vap_params->bridge)) {
        ret = 1;
    }
    if (ACFG_SEC_CMP_RADIUS(vap_params->security_params,
                cur_vap_params->security_params))
    {
        ret = 1;
    }
    return ret;
}


uint32_t
acfg_hostapd_modify_bss(acfg_wlan_profile_vap_params_t *vap_params,
        acfg_wlan_profile_vap_params_t *cur_vap_params,
        int8_t *sec)
{
    char buffer[4096];
    int index = 0, i;
    char replybuf[255] = {0};
    uint32_t len = sizeof (replybuf);
    char acfg_hapd_param_list[32][512];
    uint32_t state = ACFG_SEC_UNCHANGED;
    uint32_t status = QDF_STATUS_E_FAILURE;

    if(ACFG_IS_WPS_WEP_ENABLED(vap_params->security_params)) {
        fprintf(stderr, "configuring wep: Disabling wps\n");
        if (acfg_ctrl_iface_present(vap_params->vap_name,
                    ACFG_OPMODE_HOSTAP) == 1)
        {
            if (QDF_STATUS_SUCCESS != acfg_hostapd_delete_bss(vap_params)) {
                acfg_log_errstr("hostapd delete error\n");
                return QDF_STATUS_E_FAILURE;
            }
            status = acfg_set_auth_open(vap_params, ACFG_SEC_DISABLE_SECURITY);
            if(status != QDF_STATUS_SUCCESS){
                return QDF_STATUS_E_FAILURE;
            }
            acfg_rem_wps_config_file(vap_params->vap_name);
        }
        *sec = 0;
        return QDF_STATUS_SUCCESS;
    }
    acfg_get_security_state (vap_params, cur_vap_params, &state);
    if (ACFG_IS_SEC_ENABLED(vap_params->security_params.sec_method) &&
            ACFG_IS_SEC_ENABLED(cur_vap_params->security_params.sec_method) &&
            strcmp(vap_params->bridge, cur_vap_params->bridge) &&
            (acfg_ctrl_iface_present(vap_params->vap_name,
                                     ACFG_OPMODE_HOSTAP) == 1))
    {
        if (QDF_STATUS_SUCCESS != acfg_hostapd_delete_bss(vap_params)) {
            acfg_log_errstr("Hostapd delete error\n");
            return QDF_STATUS_E_FAILURE;
        }
        if(QDF_STATUS_SUCCESS != acfg_set_auth_open(vap_params, state)){
            return QDF_STATUS_E_FAILURE;
        }
        state = ACFG_SEC_SET_SECURITY;
    }
    if (strcmp(vap_params->bridge, cur_vap_params->bridge) &&
            ACFG_IS_VALID_WPS(vap_params->security_params))
    {
        state = ACFG_SEC_RESET_SECURITY;
    }
    /*If there is a SSID mismatch, delete and ADD bss again to
     * make hostapd aware of new SSID */
    if ((cur_vap_params->vap_name[0] != '\0') && (!ACFG_STR_MATCH(vap_params->ssid, cur_vap_params->ssid))) {
        state = ACFG_SEC_RESET_SECURITY;
    }
    if (state == ACFG_SEC_UNCHANGED) {
        if (ACFG_IS_SEC_ENABLED(vap_params->security_params.sec_method)) {
            *sec = 1;
            return QDF_STATUS_SUCCESS;
        } else {
            if (vap_params->security_params.wps_flag) {
                *sec = 1;
                return QDF_STATUS_SUCCESS;
            }
            *sec = 0;
            return QDF_STATUS_SUCCESS;
        }
    }
    if(QDF_STATUS_SUCCESS != acfg_set_auth_open(vap_params, state)){
        return QDF_STATUS_E_FAILURE;
    }
    if (state == ACFG_SEC_MODIFY_SECURITY_PARAMS) {
        if (acfg_check_reset(vap_params, cur_vap_params)) {
            state = ACFG_SEC_RESET_SECURITY;
        }
        if (vap_params->security_params.wps_flag) {
            acfg_wps_cred_t wps_cred;

            state = ACFG_SEC_RESET_SECURITY;
            if(acfg_get_wps_config(vap_params->vap_name, &wps_cred) >= 0){
                if ( wps_cred.wps_state == WPS_FLAG_CONFIGURED) {
                    //Security param is modified. So remove the wps configuration
                    acfg_rem_wps_config_file(vap_params->vap_name);
                }
            }
        }
    }

    if ((state == ACFG_SEC_SET_SECURITY) ||
            (state == ACFG_SEC_MODIFY_SECURITY_PARAMS) ||
            (state == ACFG_SEC_RESET_SECURITY))
    {
        if (state == ACFG_SEC_RESET_SECURITY) {
            /*
             * Try to Delete current BSS before setting new
             * security configuration. acfg_hostapd_delete_bss
             * may return failure if no bss is running.
             */
            if(QDF_STATUS_SUCCESS != acfg_hostapd_delete_bss(vap_params)) {
                fprintf(stderr, "%s: Hostapd delete error\n", __func__);
            }
            state = ACFG_SEC_SET_SECURITY;
        }
        if (state == ACFG_SEC_SET_SECURITY) {
            if (!g_sock_ctx.cfg80211) {
                snprintf(buffer, sizeof(buffer), "ADD %s %s %s",
                        vap_params->vap_name, ctrl_hapd, WPA_DRIVER_ATHEROS);
            } else {
#if HAPD_CONF_FILE_UPDATE
                if (acfg_create_hostapd_conf_file(vap_params) != QDF_STATUS_SUCCESS) {
                    return QDF_STATUS_E_FAILURE;
                }
                if(QDF_STATUS_SUCCESS != acfg_set_hapd_config_params(vap_params)){
                    acfg_log_errstr("%s: Failed to configure security for %s\n", __func__,
                            vap_params->vap_name);
                    return QDF_STATUS_E_FAILURE;
                }
                snprintf(buffer, sizeof(buffer), "ADD bss_config=%s:/var/run/hostapd-%s.conf",
                        vap_params->vap_name, vap_params->vap_name);
#else
                snprintf(buffer, sizeof(buffer), "ADD %s %s %s",
                        vap_params->vap_name, ctrl_hapd, WPA_DRIVER_NL80211);
#endif
            }
            if((acfg_ctrl_req ((uint8_t *)ACFG_HAPD_GLOBAL_CTRL_IFACE,
                    buffer, strlen(buffer),
                    replybuf, &len,
                            ACFG_OPMODE_HOSTAP) < 0) ||
                    strncmp (replybuf, "OK", strlen("OK"))){
                return QDF_STATUS_E_FAILURE;
            }
        }
#if !HAPD_CONF_FILE_UPDATE
        if(QDF_STATUS_SUCCESS != acfg_set_hapd_config_params(vap_params)){
            return QDF_STATUS_E_FAILURE;
        }
        if (acfg_set_hapd_radio_params(vap_params) != QDF_STATUS_SUCCESS) {
            return QDF_STATUS_E_FAILURE;
        }
#endif
        if (state == ACFG_SEC_MODIFY_SECURITY_PARAMS) {
            snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "RELOAD");
            index++;
        }

        if (ACFG_IS_SEC_ENABLED(vap_params->security_params.sec_method))
            *sec = 1;
        else
            *sec = 0;
    }

    if (state == ACFG_SEC_DISABLE_SECURITY) {
        if(QDF_STATUS_SUCCESS != acfg_hostapd_delete_bss(vap_params)) {
            acfg_log_errstr("Hostapd delete error\n");
            return QDF_STATUS_E_FAILURE;
        }
        if(QDF_STATUS_SUCCESS != acfg_set_auth_open(vap_params, state)){
            return QDF_STATUS_E_FAILURE;
        }
        *sec = 0;
        return QDF_STATUS_SUCCESS;
    }
    for (i = 0; i < index; i++) {
        if((acfg_ctrl_req (vap_params->vap_name,
                        acfg_hapd_param_list[i],
                        strlen(acfg_hapd_param_list[i]),
                        replybuf, &len,
                        ACFG_OPMODE_HOSTAP) < 0) ||
                strncmp (replybuf, "OK", strlen("OK"))){
            acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                    acfg_hapd_param_list[i],
                    vap_params->vap_name);
            return QDF_STATUS_E_FAILURE;
        }
    }
    return QDF_STATUS_SUCCESS;
}

uint32_t
acfg_hostapd_delete_bss(acfg_wlan_profile_vap_params_t *vap_params)
{
    char buffer[4096];
    char replybuf[255] = {0};
    uint32_t reply_len = sizeof(replybuf);

    acfg_os_snprintf(buffer, sizeof(buffer), "REMOVE %s", vap_params->vap_name);
    if((acfg_ctrl_req ((uint8_t *)ACFG_HAPD_GLOBAL_CTRL_IFACE,
            buffer, strlen(buffer),
                    replybuf, &reply_len,
                    ACFG_OPMODE_HOSTAP) < 0) ||
            strncmp (replybuf, "OK", strlen("OK"))){
        return QDF_STATUS_E_FAILURE;
    }

    return QDF_STATUS_SUCCESS;
}

uint32_t
acfg_set_security(acfg_wlan_profile_vap_params_t *vap_params,
        acfg_wlan_profile_vap_params_t *cur_vap_params,
        uint8_t action,
        int8_t *sec)
{
    uint32_t status = QDF_STATUS_SUCCESS;
    if (vap_params->opmode == ACFG_OPMODE_STA) {
        if (action == PROFILE_CREATE) {
            if(ACFG_IS_WPS_WEP_ENABLED(vap_params->security_params)) {
                return status;
            }
            status = acfg_wpa_supp_add_interface(vap_params, 0, sec);
            if(status != QDF_STATUS_SUCCESS){
                acfg_log_errstr("%s: Failed to ADD %s with security\n", __func__,
                        vap_params->vap_name);
                return QDF_STATUS_E_FAILURE;
            }
            if (*sec == 0) {
                status = acfg_set_auth_open(vap_params, ACFG_SEC_DISABLE_SECURITY);
            }
        } else if (action  == PROFILE_MODIFY) {
            status = acfg_wpa_supp_modify(vap_params, cur_vap_params, sec);
            if(status != QDF_STATUS_SUCCESS){
                acfg_log_errstr("%s: Failed to MODIFY %s with security\n", __func__,
                        vap_params->vap_name);
            }
        } else if (action == PROFILE_DELETE) {
            status = acfg_wpa_supp_delete(vap_params);
            if(status != QDF_STATUS_SUCCESS){
                acfg_log_errstr("%s: Failed to DEL %s with security\n", __func__,
                        vap_params->vap_name);
            }
        }
    }

    if (vap_params->opmode == ACFG_OPMODE_HOSTAP) {
        if (action == PROFILE_CREATE) {
            status = acfg_hostapd_add_bss(vap_params, sec);
            if(status != QDF_STATUS_SUCCESS){
                acfg_log_errstr("%s: Failed to ADD bss %s with security\n", __func__,
                        vap_params->vap_name);
                return QDF_STATUS_E_FAILURE;
            }
            if(*sec == 0){
                status = acfg_set_auth_open(vap_params, ACFG_SEC_DISABLE_SECURITY);
            }
        } else if (action  == PROFILE_MODIFY) {
            status = acfg_hostapd_modify_bss(vap_params, cur_vap_params, sec);
            if(status != QDF_STATUS_SUCCESS){
                acfg_log_errstr("%s: Failed to MODIFY bss %s with security\n", __func__,
                        vap_params->vap_name);
            }
        } else if (action == PROFILE_DELETE) {
            status = acfg_hostapd_delete_bss(vap_params);
            if(status != QDF_STATUS_SUCCESS){
                acfg_log_errstr("%s: Failed to DEL bss %s with security\n", __func__,
                        vap_params->vap_name);
            }
        }
    }
    return status;
}

int
acfg_get_supplicant_param(acfg_wlan_profile_vap_params_t *vap_params,
        enum wpa_supp_param_type param)
{
    char cmd[255], replybuf[255] = {0};
    uint32_t len = sizeof (replybuf);
    uint32_t id = 0;

    if (acfg_ctrl_iface_present(vap_params->vap_name,
                ACFG_OPMODE_STA) == -1)
    {
        return 0;
    }

    switch (param) {
        case ACFG_WPA_PROTO:
            acfg_os_snprintf(cmd, sizeof(cmd), "%s %d %s",
                     WPA_GET_NETWORK_CMD_PREFIX, id, "proto");

            if(acfg_ctrl_req(vap_params->vap_name, cmd, strlen(cmd),
                        replybuf, &len, ACFG_OPMODE_STA) < 0){
                acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                        cmd,
                        vap_params->vap_name);
                return -1;
            }

            if (strncmp(replybuf, WPA_SUPP_PROTO_WPA, 3) == 0) {
                return ACFG_WLAN_PROFILE_SEC_METH_WPA;
            } else if (strncmp(replybuf, WPA_SUPP_PROTO_WPA2, 4) == 0) {
                return ACFG_WLAN_PROFILE_SEC_METH_WPA2;
            }
            break;

        case ACFG_WPA_KEY_MGMT:
            break;

        case ACFG_WPA_UCAST_CIPHER:
            acfg_os_snprintf(cmd, sizeof(cmd), "%s %d %s", WPA_GET_NETWORK_CMD_PREFIX,
                    id, "pairwise");

            if(acfg_ctrl_req(vap_params->vap_name, cmd, strlen(cmd),
                        replybuf, &len, ACFG_OPMODE_STA) < 0){
                acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                        cmd,
                        vap_params->vap_name);
                return -1;
            }
            if (strncmp(replybuf, WPA_SUPP_PAIRWISE_CIPHER_TKIP, 4) == 0) {
                return ACFG_WLAN_PROFILE_CIPHER_METH_TKIP;
            } else if (strncmp(replybuf, WPA_SUPP_PAIRWISE_CIPHER_CCMP, 4)
                    == 0) {
                return ACFG_WLAN_PROFILE_CIPHER_METH_AES;
            }
            break;

        case ACFG_WPA_MCAST_CIPHER:
            break;

        default:
            return -1;
    }
    return 0;
}

void
acfg_parse_hapd_get_params(acfg_wlan_profile_vap_params_t *vap_params, char *buf,
        int32_t len)
{
    int32_t offset = 0;
    int8_t wpa = 0;

    while (offset < len) {
        if (!strncmp(buf + offset, "wps_state=", strlen("wps_state="))) {

            offset += strlen("wps_state=");

            if (!strncmp(buf + offset, "disabled", strlen("disabled"))) {
                offset += strlen("disabled");
            } else if (!strncmp(buf + offset, "not configured",
                        strlen("not configured"))) {
                offset += strlen("not configured");
            } else if (!strncmp(buf + offset, "configured",
                        strlen("configured"))) {
                vap_params->security_params.sec_method =
                    ACFG_WLAN_PROFILE_SEC_METH_WPS;
                offset += strlen("configured");
            }
            offset++;
        } else if (!strncmp(buf + offset, "key_mgmt=", strlen("key_mgmt="))) {
            offset += strlen("key_mgmt=");
            if (!strncmp (buf + offset, "WPA-PSK", strlen("WPA-PSK"))) {
                wpa = 1;
                offset += strlen("WPA-PSK");
            }
            offset++;
        } else if (!strncmp (buf + offset, "group_cipher=",
                    strlen("group_cipher=")))
        {
            offset += strlen("group_cipher=");
            if (!strncmp(buf + offset, "CCMP", strlen ("CCMP"))) {
                offset += strlen ("CCMP");
            } else if (!strncmp(buf + offset, "TKIP", strlen ("TKIP"))) {
                offset += strlen ("TKIP");
            }
            offset++;

        } else if (!strncmp(buf + offset, "rsn_pairwise_cipher=",
                    strlen("rsn_pairwise_cipher=")))
        {
            offset += strlen("rsn_pairwise_cipher=");
            if (wpa == 1) {
                vap_params->security_params.sec_method =
                    ACFG_WLAN_PROFILE_SEC_METH_WPA2;
            }

            if (!strncmp(buf + offset, "CCMP ", strlen("CCMP "))) {
                vap_params->security_params.cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_AES;

                offset += strlen("CCMP ");

                if (!strncmp(buf + offset, "TKIP ", strlen("TKIP "))) {
                    offset += strlen("TKIP ");
                }

                offset++;
            } else if (!strncmp(buf + offset, "TKIP ", strlen("TKIP "))) {
                vap_params->security_params.cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_TKIP;

                offset += strlen("TKIP ");

                if (!strncmp(buf + offset, "CCMP ", strlen("CCMP "))) {
                    offset += strlen("CCMP ");
                }

                offset++;
            }
        } else if (!strncmp(buf + offset, "wpa_pairwise_cipher=",
                    strlen("wpa_pairwise_cipher=")))
        {
            vap_params->security_params.sec_method =
                ACFG_WLAN_PROFILE_SEC_METH_WPA;
            offset += strlen("wpa_pairwise_cipher=");

            if (!strncmp(buf + offset, "CCMP ", strlen("CCMP "))) {
                vap_params->security_params.cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_AES;

                offset += strlen("CCMP ");

                if (!strncmp(buf + offset, "TKIP ", strlen("TKIP "))) {
                    offset += strlen("TKIP ");
                }

                offset++;
            } else if (!strncmp(buf + offset, "TKIP ", strlen("TKIP "))) {
                vap_params->security_params.cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_TKIP;

                offset += strlen("TKIP ");

                if (!strncmp(buf + offset, "CCMP ", strlen("CCMP "))) {
                    offset += strlen("CCMP ");
                }

                offset++;
            }
        } else {
            while ((*(buf + offset) != '\n') && (offset < len )) {
                offset++;
            }
            offset++;
        }
    }
}

uint32_t
acfg_get_hapd_param(acfg_wlan_profile_vap_params_t *vap_params)
{
    char cmd[255], replybuf[1024];
    uint32_t len = sizeof (replybuf);

    memset(replybuf, '\0', sizeof (replybuf));
    acfg_os_snprintf(cmd, sizeof(cmd), "%s", WPA_HAPD_GET_CONFIG_CMD_PREFIX);

    if(acfg_ctrl_req(vap_params->vap_name, cmd, strlen(cmd),
                replybuf, &len, ACFG_OPMODE_HOSTAP) < 0){
        acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                cmd,
                vap_params->vap_name);
        return QDF_STATUS_E_FAILURE;
    }

    acfg_parse_hapd_get_params(vap_params, replybuf, len);

    return QDF_STATUS_SUCCESS;
}

int
acfg_get_wpa_params(acfg_wlan_profile_vap_params_t *vap_params)
{
    uint32_t wpa_sec_method, ucast_cipher;

    if (vap_params->opmode == ACFG_OPMODE_STA) {
        if (acfg_ctrl_iface_present(vap_params->vap_name,
                    ACFG_OPMODE_STA) == -1)
        {
            return 0;
        }

        wpa_sec_method = acfg_get_supplicant_param(vap_params, ACFG_WPA_PROTO);
        if ((wpa_sec_method == ACFG_WLAN_PROFILE_SEC_METH_WPA) ||
                (wpa_sec_method == ACFG_WLAN_PROFILE_SEC_METH_WPA2))
        {
            vap_params->security_params.sec_method = wpa_sec_method;
            ucast_cipher = acfg_get_supplicant_param(vap_params,
                    ACFG_WPA_UCAST_CIPHER);
            vap_params->security_params.cipher_method = ucast_cipher;
        }
    } else if (vap_params->opmode == ACFG_OPMODE_HOSTAP) {
        if(QDF_STATUS_SUCCESS != acfg_get_hapd_param(vap_params)){
            return QDF_STATUS_E_FAILURE;
        }
    }
    return 0;
}

uint32_t
acfg_set_wps(uint8_t *ifname, enum acfg_wsupp_set_type type,
             int8_t *str)
{
    char cmd[255], replybuf[255] = {0};
    uint32_t len;

    switch (type) {
        case ACFG_WSUPP_SET_UUID:
            acfg_os_snprintf(cmd, sizeof(cmd), "%s %s %s",
                     WPA_SET_CMD_PREFIX, "uuid", str);
            break;
        case ACFG_WSUPP_SET_DEVICE_NAME:
            acfg_os_snprintf(cmd, sizeof(cmd), "%s %s %s",
                     WPA_SET_CMD_PREFIX, "device_name", str);
            break;
        case ACFG_WSUPP_SET_MANUFACTURER:
            acfg_os_snprintf(cmd, sizeof(cmd), "%s %s %s",
                     WPA_SET_CMD_PREFIX, "manufacturer", str);
            break;
        case ACFG_WSUPP_SET_MODEL_NAME:
            acfg_os_snprintf(cmd, sizeof(cmd), "%s %s %s",
                     WPA_SET_CMD_PREFIX, "model_name", str);
            break;
        case ACFG_WSUPP_SET_MODEL_NUMBER:
            acfg_os_snprintf(cmd, sizeof(cmd), "%s %s %s",
                     WPA_SET_CMD_PREFIX, "model_number", str);
            break;
        case ACFG_WSUPP_SET_SERIAL_NUMBER:
            acfg_os_snprintf(cmd, sizeof(cmd), "%s %s %s",
                     WPA_SET_CMD_PREFIX, "serial_number", str);
            break;
        case ACFG_WSUPP_SET_DEVICE_TYPE:
            acfg_os_snprintf(cmd, sizeof(cmd), "%s %s %s",
                     WPA_SET_CMD_PREFIX, "device_type", str);
            break;
        case ACFG_WSUPP_SET_OS_VERSION:
            acfg_os_snprintf(cmd, sizeof(cmd), "%s %s %s",
                     WPA_SET_CMD_PREFIX, "os_version", str);
            break;
        case ACFG_WSUPP_SET_CONFIG_METHODS:
            acfg_os_snprintf(cmd, sizeof(cmd), "%s %s %s", WPA_SET_CMD_PREFIX,
                    "config_methods", str);
            break;
        default:
            return QDF_STATUS_E_FAILURE;
    }
    len = sizeof(replybuf);
    if((acfg_ctrl_req(ifname, cmd, strlen(cmd),
                    replybuf, &len, ACFG_OPMODE_STA) < 0) ||
            strncmp (replybuf, "OK", strlen("OK"))){
        acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                cmd,
                ifname);
        return QDF_STATUS_E_FAILURE;
    }
    return QDF_STATUS_SUCCESS;
}

static int
hex2num(int8_t c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}


int
hwaddr_aton(char  *txt, uint8_t *addr)
{
    int i;

    for (i = 0; i < 6; i++) {
        int a, b;
        if (*txt == '\0') {
            return -1;
        }
        a = hex2num(*txt++);
        if (a < 0)
            return -1;
        if (*txt == '\0') {
            if (i < 6) {
                return -1;
            }
        }
        b = hex2num(*txt++);
        if (b < 0)
            return -1;
        *addr++ = (a << 4) | b;
        if (i < 5 && *txt != ':')
            return -1;
        txt++;
    }
    return 0;
}


uint32_t
acfg_set_wps_mode(char *radio_name, uint8_t *ifname,
        enum acfg_wsupp_wps_type wps,
        char *params1, int8_t *params2)
{
    char cmd[255];
    char bssid[24];
    uint8_t macaddr[6];
    acfg_opmode_t opmode;
    char reply[255];
    uint32_t len = sizeof (reply);
    int8_t sec;

    if (acfg_get_opmode(ifname, &opmode) != QDF_STATUS_SUCCESS) {
        acfg_log_errstr("%s: Opmode get failed for %s\n", __func__, ifname);
        return QDF_STATUS_E_FAILURE;
    }
    memset(reply, '\0', sizeof (reply)); //Initialize the string
    if (opmode == ACFG_OPMODE_STA) {
        /* Check if the network is set in wpa_supplicant */
        if (acfg_ctrl_iface_present (ifname, opmode) == -1) {
            acfg_wlan_profile_vap_params_t vap_params;

            acfg_os_strcpy((char *)vap_params.vap_name, (char *)ifname, sizeof(vap_params.vap_name));
            acfg_os_strcpy(vap_params.radio_name, radio_name, sizeof(vap_params.radio_name));
            if(QDF_STATUS_SUCCESS != acfg_wpa_supp_add_interface(&vap_params, 1, &sec))
            {
                return QDF_STATUS_E_FAILURE;
            }
        }
    }

    if (hwaddr_aton(params1, macaddr)) {
        acfg_os_strcpy (bssid, "any", sizeof(bssid));
    } else {
        acfg_os_strcpy (bssid, params1, sizeof(bssid));
    }

    switch (wps) {
        case ACFG_WSUPP_WPS_PBC:
            if (opmode == ACFG_OPMODE_STA) {
                acfg_os_snprintf(cmd, sizeof(cmd), "%s %s",
                         WPA_WPS_PBC_CMD_PREFIX, bssid);
                if((acfg_ctrl_req(ifname, cmd, strlen(cmd), reply,
                                &len, ACFG_OPMODE_STA) < 0) ||
                        strncmp (reply, "OK", strlen("OK"))){
                    acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                            cmd,
                            ifname);
                    return QDF_STATUS_E_FAILURE;
                }
            } else if (opmode == ACFG_OPMODE_HOSTAP) {
                acfg_os_snprintf(cmd, sizeof(cmd), "%s", WPA_WPS_PBC_CMD_PREFIX);
                if((acfg_ctrl_req(ifname, cmd, strlen(cmd), reply,
                        &len, ACFG_OPMODE_HOSTAP) < 0) ||
                        strncmp (reply, "OK", strlen("OK"))){
                    acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                            cmd,
                            ifname);
                    return QDF_STATUS_E_FAILURE;
                }
            }
            break;

        case ACFG_WSUPP_WPS_PIN:
            acfg_os_snprintf(cmd, sizeof(cmd), "%s %s",
                     WPA_WPS_CHECK_PIN_CMD_PREFIX, params2);
            len = sizeof (reply);

            if (opmode == ACFG_OPMODE_STA) {
                if(acfg_ctrl_req(ifname, cmd, strlen(cmd), reply,
                        &len, ACFG_OPMODE_STA) < 0){
                    acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                            cmd,
                            ifname);
                    return QDF_STATUS_E_FAILURE;
                }
            } else if (opmode == ACFG_OPMODE_HOSTAP) {
                if(acfg_ctrl_req(ifname, cmd, strlen(cmd), reply,
                        &len, ACFG_OPMODE_HOSTAP) < 0){
                    acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                            cmd,
                            ifname);
                    return QDF_STATUS_E_FAILURE;
                }
            }

            if (!strncmp(reply, "FAIL-CHECKSUM", strlen("FAIL-CHECKSUM")) ||
                    !strncmp(reply, "FAIL", strlen("FAIL"))) {
                acfg_log_errstr("%s: Invalid pin\n", __func__);
                return QDF_STATUS_E_FAILURE;
            }

            memset(reply, '\0', sizeof (reply));
            len = sizeof (reply);

            if (opmode == ACFG_OPMODE_STA) {

                acfg_os_snprintf(cmd, sizeof(cmd), "%s %s %s", WPA_WPS_PIN_CMD_PREFIX,
                        bssid, params2);
                if(acfg_ctrl_req(ifname, cmd, strlen(cmd), reply, &len,
                        ACFG_OPMODE_STA) < 0){
                    acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                            cmd,
                            ifname);
                    return QDF_STATUS_E_FAILURE;
                }

            } else if (opmode == ACFG_OPMODE_HOSTAP) {
                acfg_os_snprintf(cmd, sizeof(cmd), "%s %s %s %d",
                         WPA_WPS_AP_PIN_CMD_PREFIX,  "set",
                         params2, WPS_TIMEOUT);
                if(acfg_ctrl_req(ifname, cmd, strlen(cmd), reply, &len,
                        ACFG_OPMODE_HOSTAP) < 0){
                    acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                            cmd,
                            ifname);
                    return QDF_STATUS_E_FAILURE;
                }
            }
            break;

        default:
            return QDF_STATUS_E_INVAL;
    }
    return QDF_STATUS_SUCCESS;
}

uint32_t acfg_wps_cancel(uint8_t *ifname)
{
    char cmd[255];
    uint32_t len = 0;
    acfg_opmode_t opmode;

    if (acfg_get_opmode(ifname, &opmode) != QDF_STATUS_SUCCESS) {
        acfg_log_errstr("%s: Opmode get failed\n", __func__);
        return QDF_STATUS_E_FAILURE;
    }

    acfg_os_snprintf(cmd, sizeof(cmd), "%s", WPA_WPS_CANCEL_CMD_PREFIX);
    if (opmode == ACFG_OPMODE_STA) {
        if(acfg_ctrl_req(ifname, cmd, strlen(cmd),
                    NULL, &len, ACFG_OPMODE_STA) < 0){
            acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                    cmd,
                    ifname);
            return QDF_STATUS_E_FAILURE;
        }
    } else if (opmode == ACFG_OPMODE_HOSTAP) {
        if(acfg_ctrl_req(ifname, cmd, strlen(cmd),
                    NULL, &len, ACFG_OPMODE_HOSTAP) < 0){
            acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                    cmd,
                    ifname);
            return QDF_STATUS_E_FAILURE;
        }
    }
    return QDF_STATUS_SUCCESS;
}

uint32_t acfg_set_hapd_wps_params(acfg_wlan_profile_vap_params_t *vap_params)
{
    char acfg_hapd_param_list[ACFG_MAX_HAPD_CONFIG_PARAM][512];
    int index = 0, i;
    char replybuf[255] = {0};
    uint32_t len = sizeof (replybuf);

    acfg_os_snprintf(acfg_hapd_param_list[index], sizeof(acfg_hapd_param_list[index]),
             "SET ssid %s", vap_params->ssid);
    index++;
    if (ACFG_IS_SEC_WEP(vap_params->security_params))
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wep_key0 %s",
                 vap_params->security_params.wep_key0);
        index++;
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wep_key1 %s",
                 vap_params->security_params.wep_key1);
        index++;
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wep_key2 %s",
                 vap_params->security_params.wep_key2);
        index++;
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wep_key3 %s",
                 vap_params->security_params.wep_key3);
        index++;
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET wep_default_key %d",
                 vap_params->security_params.wep_key_defidx);
        index++;
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET auth_algs 0");
        index++;
    }
    if ((vap_params->security_params.sec_method ==
                ACFG_WLAN_PROFILE_SEC_METH_OPEN) &&
            (vap_params->security_params.cipher_method ==
             ACFG_WLAN_PROFILE_CIPHER_METH_NONE))
    {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET auth_algs 0");
        index++;
    }
    acfg_os_snprintf(acfg_hapd_param_list[index],
             sizeof(acfg_hapd_param_list[index]), "SET wps_state %d",
             vap_params->security_params.wps_flag);
    index++;
    if (vap_params->security_params.wps_flag) {
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]),
                 "SET upnp_iface %s", "br0");
        index++;
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET config_methods %s",
                 "\"label virtual_display virtual_push_button keypad\"");
        index++;
        acfg_os_snprintf(acfg_hapd_param_list[index],
                 sizeof(acfg_hapd_param_list[index]), "SET device_typee %s",
                "6-0050F204-1");
        index++;

    }

    for (i = 0; i < index ; i++) {
        if((acfg_ctrl_req (vap_params->vap_name,
                        acfg_hapd_param_list[i],
                        strlen(acfg_hapd_param_list[i]),
                        replybuf, &len,
                        ACFG_OPMODE_HOSTAP) < 0) ||
                strncmp (replybuf, "OK", strlen("OK"))){
            acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                    acfg_hapd_param_list[i],
                    vap_params->vap_name);
            return QDF_STATUS_E_FAILURE;
        }
    }
    return QDF_STATUS_SUCCESS;
}

int
acfg_get_open_wep_state(acfg_wlan_profile_vap_params_t *vap_params,
        acfg_wlan_profile_vap_params_t *cur_vap_params)
{
    if (ACFG_VAP_CMP_SSID(vap_params, cur_vap_params)) {
        return 2;
    }
    return 0;
}

uint32_t
acfg_config_security(acfg_wlan_profile_vap_params_t *vap_params)
{
    acfg_opmode_t opmode;

    if (acfg_get_opmode(vap_params->vap_name, &opmode) != QDF_STATUS_SUCCESS) {
        acfg_log_errstr("%s: Opmode get failed for %s\n", __func__,
                vap_params->vap_name);
        return QDF_STATUS_E_FAILURE;
    }
    acfg_get_br_name(vap_params->vap_name, vap_params->bridge);

    if (acfg_ctrl_iface_present(vap_params->vap_name,
                opmode) == 1)
    {
        char buffer[4096];
        char replybuf[255] = {0};
        uint32_t len = sizeof (replybuf);
        char acfg_hapd_param_list[32][512];
        int index = 0, i;

        if (opmode == ACFG_OPMODE_HOSTAP) {
            if(QDF_STATUS_SUCCESS != acfg_set_auth_open(vap_params,
                        ACFG_SEC_DISABLE_SECURITY)){
                return QDF_STATUS_E_FAILURE;
            }
            if(QDF_STATUS_SUCCESS != acfg_hostapd_delete_bss(vap_params)) {
                acfg_log_errstr("%s: Failed to DEL bss %s\n", __func__,
                        vap_params->vap_name);
                return QDF_STATUS_E_FAILURE;
            }
            if (!g_sock_ctx.cfg80211) {
                acfg_os_snprintf(buffer, sizeof(buffer), "ADD %s %s %s",
                        vap_params->vap_name, ctrl_hapd, WPA_DRIVER_ATHEROS);
            } else {
#if HAPD_CONF_FILE_UPDATE
                if (acfg_create_hostapd_conf_file(vap_params) != QDF_STATUS_SUCCESS) {
                    return QDF_STATUS_E_FAILURE;
                }
                if(QDF_STATUS_SUCCESS != acfg_set_hapd_config_params(vap_params)){
                    acfg_log_errstr("%s: Failed to configure security for %s\n", __func__,
                            vap_params->vap_name);
                    return QDF_STATUS_E_FAILURE;
                }
                acfg_os_snprintf(buffer, sizeof(buffer), "ADD bss_config=%s:/var/run/hostapd-%s.conf",
                        vap_params->vap_name, vap_params->vap_name);
#else
                acfg_os_snprintf(buffer, sizeof(buffer), "ADD %s %s %s",
                        vap_params->vap_name, ctrl_hapd, WPA_DRIVER_NL80211);
#endif
            }
            if((acfg_ctrl_req ((uint8_t *)ACFG_HAPD_GLOBAL_CTRL_IFACE,
                    buffer, strlen(buffer),
                    replybuf, &len,
                    ACFG_OPMODE_HOSTAP) < 0) ||
                    strncmp(replybuf, "OK", strlen("OK"))){
                acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                        buffer,
                        vap_params->vap_name);
                return QDF_STATUS_E_FAILURE;
            }

            acfg_os_snprintf(acfg_hapd_param_list[index],
                     sizeof(acfg_hapd_param_list[index]), "ENABLE");
            index++;

#if !HAPD_CONF_FILE_UPDATE
            if(QDF_STATUS_SUCCESS != acfg_set_hapd_config_params(vap_params)){
                acfg_log_errstr("%s: Failed to configure security for %s\n", __func__,
                        vap_params->vap_name);
                return QDF_STATUS_E_FAILURE;
            }
            if (acfg_set_hapd_radio_params(vap_params) != QDF_STATUS_SUCCESS) {
                return QDF_STATUS_E_FAILURE;
            }
#endif
            for (i = 0; i < index; i++) {
                if((acfg_ctrl_req (vap_params->vap_name,
                                acfg_hapd_param_list[i],
                                strlen(acfg_hapd_param_list[i]),
                                replybuf, &len,
                                ACFG_OPMODE_HOSTAP) < 0) ||
                        strncmp(replybuf, "OK", strlen("OK"))){
                    acfg_log_errstr("%s: cmd --> %s failed for %s\n", __func__,
                            acfg_hapd_param_list[i],
                            vap_params->vap_name);
                    return QDF_STATUS_E_FAILURE;
                }
            }
        }
        wsupp_status_init = 1;
    }
    return QDF_STATUS_SUCCESS;
}

void
acfg_parse_wpa_supplicant(char value[][MAX_SIZE],
        acfg_wlan_profile_vap_params_t *vap_params)
{
    char *cpr = value[0];
    char *sec = value[1];
    char *gr_cpr = value[2];
    char *key_mgmt = value[3];

    if (!(strcmp(key_mgmt, "WPS"))) {
        vap_params->security_params.wps_flag = 1;
    } else {
        vap_params->security_params.wps_flag = 0;
    }

    if (!(strcmp(cpr, "TKIP")) && !(strcmp(sec, "WPA")))  {
        vap_params->security_params.sec_method =
            ACFG_WLAN_PROFILE_SEC_METH_WPA;
        vap_params->security_params.cipher_method =
            ACFG_WLAN_PROFILE_CIPHER_METH_TKIP;
    }
    if (!(strcmp(cpr, "CCMP")) &&
            !(strcmp(sec, "RSN")))  {
        vap_params->security_params.sec_method =
            ACFG_WLAN_PROFILE_SEC_METH_WPA2;
        vap_params->security_params.cipher_method =
            ACFG_WLAN_PROFILE_CIPHER_METH_AES;
    }
    if (!(strcmp(cpr, "CCMP TKIP")) && !(strcmp(sec, "WPA RSN")))  {
        vap_params->security_params.sec_method =
            ACFG_WLAN_PROFILE_SEC_METH_WPAWPA2;
        vap_params->security_params.cipher_method =
            ACFG_WLAN_PROFILE_CIPHER_METH_TKIP |
            ACFG_WLAN_PROFILE_CIPHER_METH_AES;
    }
    if (!(strcmp(cpr, "CCMP TKIP")) && !(strcmp(sec, "RSN")))  {
        vap_params->security_params.sec_method =
            ACFG_WLAN_PROFILE_SEC_METH_WPA2;
        vap_params->security_params.cipher_method =
            ACFG_WLAN_PROFILE_CIPHER_METH_TKIP |
            ACFG_WLAN_PROFILE_CIPHER_METH_AES;
    }

    if (!(strcmp(gr_cpr, "TKIP"))) {
        vap_params->security_params.g_cipher_method =
            ACFG_WLAN_PROFILE_CIPHER_METH_TKIP;
    }
    if (!(strcmp(gr_cpr, "CCMP"))) {
        vap_params->security_params.g_cipher_method =
            ACFG_WLAN_PROFILE_CIPHER_METH_AES;
    }
    if (!(strcmp(gr_cpr, "CCMP TKIP"))) {
        vap_params->security_params.g_cipher_method =
            ACFG_WLAN_PROFILE_CIPHER_METH_TKIP	|
            ACFG_WLAN_PROFILE_CIPHER_METH_AES;
    }

}

uint32_t
acfg_wpa_supplicant_get(acfg_wlan_profile_vap_params_t *vap_params)
{
    struct sockaddr_un local;
    struct sockaddr_un remote;
    int s;
    char buf[MAX_SIZE], str[MAX_SIZE];
    char rcv_buf[SUPP_FIELD][MAX_SIZE];
    int len_local = sizeof(local);
    uint32_t len_remote = sizeof(remote);
    int bind_status = 0, send_status = 0, recv_status = 0;

    memset(rcv_buf, 0,sizeof(rcv_buf));
    memset(buf, 0, sizeof(buf));
    memset(&local, 0, sizeof(local));

    s = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (s < 0) {
        acfg_log_errstr("%s: socket failed: %s\n", __func__, strerror(errno));
        return QDF_STATUS_E_FAILURE;
    }
    local.sun_family = AF_UNIX;
    acfg_os_strcpy(local.sun_path, "/tmp/testwpa", sizeof(local.sun_path));
    memset(&remote, 0, sizeof(remote));
    remote.sun_family = AF_UNIX;
    acfg_os_snprintf(buf, sizeof(buf), "%s%s%s","GET_NETWORK ",vap_params->vap_name,
             " pairwise");
    acfg_os_snprintf(str, sizeof(str), "%s%s", "/var/run/wpa_supplicant/",
             vap_params->vap_name);
    memcpy(remote.sun_path, str, sizeof(remote.sun_path));
    unlink("/tmp/testwpa");

    bind_status = bind(s, (struct sockaddr *)&local, (socklen_t)len_local);
    if (bind_status < 0) {
        acfg_log_errstr("%s: bind Failed: %s\n", __func__, strerror(errno));
        close(s);
        return QDF_STATUS_E_FAILURE;
    }

    send_status = sendto(s, buf, sizeof(buf), 0, (struct sockaddr *)&remote,
            len_remote);
    memset(buf, 0,sizeof(buf));
    if (send_status < 0) {
        acfg_log_errstr("%s: send failed: %s\n", __func__, strerror(errno));
        close(s);
        return QDF_STATUS_E_FAILURE;
    }
    len_remote = sizeof(remote);
    recv_status = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&remote,
            &len_remote);
    if (recv_status < 0) {
        acfg_log_errstr("%s: receive failed: %s\n", __func__, strerror(errno));
        close(s);
        return QDF_STATUS_E_FAILURE;
    }
    acfg_os_strcpy(rcv_buf[0], buf, sizeof(rcv_buf[0]));
    memset(buf, 0, sizeof(buf));

    acfg_os_snprintf(buf, sizeof(buf), "%s%s%s","GET_NETWORK ",vap_params->vap_name, " proto");
    send_status = sendto(s, buf, sizeof(buf), 0, (struct sockaddr *)&remote,
            len_remote);
    memset(buf, 0, sizeof(buf));
    if (send_status < 0) {
        acfg_log_errstr("%s: send failed: %s\n", __func__, strerror(errno));
        close(s);
        return QDF_STATUS_E_FAILURE;
    }
    len_remote = sizeof(remote);
    recv_status = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&remote,
            &len_remote);
    if (recv_status < 0) {
        acfg_log_errstr("%s: receive failed: %s\n", __func__, strerror(errno));
        close(s);
        return QDF_STATUS_E_FAILURE;
    }
    acfg_os_strcpy(rcv_buf[1], buf, sizeof(rcv_buf[1]));
    memset(buf, 0, sizeof(buf));

    acfg_os_snprintf(buf, sizeof(buf), "%s%s%s","GET_NETWORK ",vap_params->vap_name, " group");
    send_status = sendto(s, buf, sizeof(buf), 0, (struct sockaddr *)&remote,
            len_remote);
    memset(buf, 0, sizeof(buf));
    if (send_status < 0) {
        acfg_log_errstr("%s: send failed: %s\n", __func__, strerror(errno));
        close(s);
        return QDF_STATUS_E_FAILURE;
    }
    len_remote = sizeof(remote);
    recv_status = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&remote,
            &len_remote);
    if (recv_status < 0) {
        acfg_log_errstr("%s: receive failed: %s\n", __func__, strerror(errno));
        close(s);
        return QDF_STATUS_E_FAILURE;
    }
    acfg_os_strcpy(rcv_buf[2], buf, sizeof(rcv_buf[2]));
    acfg_parse_wpa_supplicant(rcv_buf, vap_params);
    close(s);

    return QDF_STATUS_SUCCESS;
}

void
acfg_parse_hostapd(char *value, acfg_wlan_profile_vap_params_t *vap_params)
{
    int last;
    char *start, *end, *buf;
    buf = strdup(value);
    if (buf == NULL) {
        return;
    }
    start = buf;
    while (*start != '\0') {
        while (*start == ' ' || *start == '\t') {
            start++;
        }
        if (*start == '\0') {
            break;
        }
        end = start;
        while (*end != ' ' && *end != '\t' && *end != '\0' && *end != '\n') {
            end++;
        }
        last = *end == '\0';
        *end = '\0';
        if (strcmp (start, "wps_state=configured") == 0) {
            vap_params->security_params.wps_flag = 1;
        } else {
            vap_params->security_params.wps_flag = 0;
        }

        if (strcmp (start, "key_mgmt=WPA-PSK") == 0) {
            if (strstr (value, "wpa_pairwise_cipher=TKIP"))  {
                vap_params->security_params.sec_method =
                    ACFG_WLAN_PROFILE_SEC_METH_WPA;
                vap_params->security_params.cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_TKIP;
                vap_params->security_params.g_cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_TKIP;
            } else if((strstr(value, "wpa_pairwise_cipher=CCMP TKIP") &&
                        strstr(value, "rsn_pairwise_cipher=CCMP TKIP")) != 0) {
                vap_params->security_params.sec_method =
                    ACFG_WLAN_PROFILE_SEC_METH_WPAWPA2;
                vap_params->security_params.g_cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_TKIP;
                vap_params->security_params.cipher_method =
                    (ACFG_WLAN_PROFILE_CIPHER_METH_TKIP |
                     ACFG_WLAN_PROFILE_CIPHER_METH_AES);
            } else if (strstr(value, "rsn_pairwise_cipher=CCMP TKIP")) {
                vap_params->security_params.sec_method =
                    ACFG_WLAN_PROFILE_SEC_METH_WPA2;
                vap_params->security_params.g_cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_TKIP;
                vap_params->security_params.cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_TKIP |
                    ACFG_WLAN_PROFILE_CIPHER_METH_AES;
            } else if (strstr(value, "rsn_pairwise_cipher=CCMP")) {
                vap_params->security_params.sec_method =
                    ACFG_WLAN_PROFILE_SEC_METH_WPA2;
                vap_params->security_params.g_cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_AES;
                vap_params->security_params.cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_AES ;
            }
        }
        if (strcmp(start, "key_mgmt=WPA-EAP") == 0) {
            if (strstr(value, "wpa_pairwise_cipher=TKIP") != NULL)  {
                vap_params->security_params.sec_method =
                    ACFG_WLAN_PROFILE_SEC_METH_WPA_EAP;
                vap_params->security_params.cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_TKIP;
                vap_params->security_params.g_cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_TKIP;
            } else if (strstr(value, "rsn_pairwise_cipher=CCMP TKIP")) {
                vap_params->security_params.sec_method =
                    ACFG_WLAN_PROFILE_SEC_METH_WPA2_EAP;
                vap_params->security_params.cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_TKIP |
                    ACFG_WLAN_PROFILE_CIPHER_METH_AES;
                vap_params->security_params.g_cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_TKIP;
            } else if (strstr(value, "rsn_pairwise_cipher=CCMP")) {
                vap_params->security_params.sec_method =
                    ACFG_WLAN_PROFILE_SEC_METH_WPA2_EAP;
                vap_params->security_params.cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_AES;
                vap_params->security_params.g_cipher_method =
                    ACFG_WLAN_PROFILE_CIPHER_METH_AES;
            }
        }
        if (last) {
            break;
        }
        start = end + 1;
    }
    free(buf);//Free locally allocated memory using strdup
}

uint32_t
acfg_hostapd_get(acfg_wlan_profile_vap_params_t *vap_params)
{
    struct sockaddr_un local;
    struct sockaddr_un remote;
    int s;
    char buf[MAX_SIZE], str[MAX_SIZE];
    int len_local = sizeof(local);
    uint32_t len_remote = sizeof(remote);
    memset(buf, 0, sizeof(buf));
    memset(&local, 0, sizeof(local));
    memset(&remote, 0, sizeof(remote));

    int bind_status = 0, send_status = 0, recv_status = 0;
    s = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (s < 0) {
        acfg_log_errstr("%s: socket failed: %s\n", __func__, strerror(errno));
        return QDF_STATUS_E_FAILURE;
    }
    local.sun_family = AF_UNIX;
    acfg_os_strcpy(local.sun_path, "/tmp/testapp", sizeof(local.sun_path));
    remote.sun_family = AF_UNIX;
    acfg_os_strcpy(buf, "GET_CONFIG", sizeof(buf));
    acfg_os_snprintf(str, sizeof(str), "%s%s","/var/run/hostapd/",vap_params->vap_name);
    if (!strcmp(buf, "GET_CONFIG")) {
        memcpy(remote.sun_path, str, sizeof(remote.sun_path));
    }
    unlink("/tmp/testapp");

    bind_status = bind(s, (struct sockaddr *)&local, (socklen_t)len_local);
    if (bind_status < 0) {
        acfg_log_errstr("%s: bind Failed: %s\n", __func__, strerror(errno));
        close(s);
        return QDF_STATUS_E_FAILURE;
    }

    send_status = sendto(s, buf, sizeof(buf), 0, (struct sockaddr *)&remote,
            len_remote);
    if (send_status < 0) {
        acfg_log_errstr("%s: send failed: %s\n", __func__, strerror(errno));
        close(s);
        return QDF_STATUS_E_FAILURE;
    }
    len_remote = sizeof(remote);
    recv_status = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&remote,
            &len_remote);
    if (recv_status < 0) {
        acfg_log_errstr("%s: receive failed: %s\n", __func__, strerror(errno));
        close(s);
        return QDF_STATUS_E_FAILURE;
    }

    acfg_parse_hostapd(buf, vap_params);
    close(s);

    return QDF_STATUS_SUCCESS;
}
