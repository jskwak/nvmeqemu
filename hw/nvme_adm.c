/*
 * Copyright (c) 2011 Intel Corporation
 *
 * by
 *    Maciej Patelczyk <mpatelcz@gkslx007.igk.intel.com>
 *    Krzysztof Wierzbicki <krzysztof.wierzbicki@intel.com>
 *    Patrick Porlan <patrick.porlan@intel.com>
 *    Nisheeth Bhat <nisheeth.bhat@intel.com>
 *    Keith Busch <keith.busch@intel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */


#include <sys/mman.h>
#include "nvme.h"
#include "nvme_debug.h"

static uint32_t adm_cmd_del_sq(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t adm_cmd_alloc_sq(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t adm_cmd_del_cq(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t adm_cmd_alloc_cq(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t adm_cmd_get_log_page(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t adm_cmd_identify(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t adm_cmd_abort(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t adm_cmd_set_features(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t adm_cmd_get_features(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t adm_cmd_async_ev_req(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t adm_cmd_act_fw(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t adm_cmd_dl_fw(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t adm_cmd_format_nvm(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);

static uint32_t aon_cmd_sec_send(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t aon_cmd_sec_recv(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t aon_adm_cmd_create_pd(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t aon_adm_cmd_create_stag(NVMEState *n, NVMECmd *cmd,
    NVMECQE *cqe);
static uint32_t aon_adm_cmd_delete_pd(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t aon_adm_cmd_delete_stag(NVMEState *n, NVMECmd *cmd,
    NVMECQE *cqe);
static uint32_t aon_adm_cmd_create_ns(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t aon_adm_cmd_create_nstag(NVMEState *n, NVMECmd *cmd,
    NVMECQE *cqe);
static uint32_t aon_adm_cmd_delete_ns(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t aon_adm_cmd_delete_nstag(NVMEState *n, NVMECmd *cmd,
    NVMECQE *cqe);
static uint32_t aon_adm_cmd_mod_ns(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);
static uint32_t aon_adm_cmd_inject_err(NVMEState *n, NVMECmd *cmd,
    NVMECQE *cqe);

typedef uint32_t adm_command_func(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe);

static adm_command_func * const adm_cmds_funcs[] = {
    [NVME_ADM_CMD_DELETE_SQ] = adm_cmd_del_sq,
    [NVME_ADM_CMD_CREATE_SQ] = adm_cmd_alloc_sq,
    [NVME_ADM_CMD_GET_LOG_PAGE] = adm_cmd_get_log_page,
    [NVME_ADM_CMD_DELETE_CQ] = adm_cmd_del_cq,
    [NVME_ADM_CMD_CREATE_CQ] = adm_cmd_alloc_cq,
    [NVME_ADM_CMD_IDENTIFY] = adm_cmd_identify,
    [NVME_ADM_CMD_ABORT] = adm_cmd_abort,
    [NVME_ADM_CMD_SET_FEATURES] = adm_cmd_set_features,
    [NVME_ADM_CMD_GET_FEATURES] = adm_cmd_get_features,
    [NVME_ADM_CMD_ASYNC_EV_REQ] = adm_cmd_async_ev_req,
    [NVME_ADM_CMD_ACTIVATE_FW] = adm_cmd_act_fw,
    [NVME_ADM_CMD_DOWNLOAD_FW] = adm_cmd_dl_fw,

    [NVME_ADM_CMD_FORMAT_NVM] = adm_cmd_format_nvm,
    [NVME_ADM_CMD_SECURITY_SEND] = aon_cmd_sec_send,
    [NVME_ADM_CMD_SECURITY_RECV] = aon_cmd_sec_recv,

    [AON_ADM_CMD_CREATE_PD] = aon_adm_cmd_create_pd,
    [AON_ADM_CMD_CREATE_STAG] = aon_adm_cmd_create_stag,
    [AON_ADM_CMD_DELETE_PD] = aon_adm_cmd_delete_pd,
    [AON_ADM_CMD_DELETE_STAG] = aon_adm_cmd_delete_stag,
    [AON_ADM_CMD_CREATE_NAMESPACE] = aon_adm_cmd_create_ns,
    [AON_ADM_CMD_CREATE_NAMESPACE_TAG] = aon_adm_cmd_create_nstag,
    [AON_ADM_CMD_DELETE_NAMESPACE] = aon_adm_cmd_delete_ns,
    [AON_ADM_CMD_DELETE_NAMESPACE_TAG] = aon_adm_cmd_delete_nstag,
    [AON_ADM_CMD_MODIFY_NAMESPACE] = aon_adm_cmd_mod_ns,
    [AON_ADM_CMD_INJECT_ERROR] = aon_adm_cmd_inject_err,
    [NVME_ADM_CMD_LAST] = NULL,
};

uint8_t nvme_admin_command(NVMEState *n, NVMECmd *sqe, NVMECQE *cqe)
{
    uint8_t ret = NVME_SC_DATA_XFER_ERROR;

    NVMEStatusField *sf = (NVMEStatusField *) &cqe->status;
    adm_command_func *f;

    if ((sqe->opcode >= NVME_ADM_CMD_LAST) ||
        (!adm_cmds_funcs[sqe->opcode])) {
        sf->sc = NVME_SC_INVALID_OPCODE;
    } else {
        f = adm_cmds_funcs[sqe->opcode];
        ret = f(n, sqe, cqe);
    }
    return ret;
}

uint32_t adm_check_cqid(NVMEState *n, uint16_t cqid)
{
    /* If queue is allocated dma_addr!=NULL and has the same ID */
    if (cqid > NVME_MAX_QID) {
        return FAIL;
    } else if (n->cq[cqid].dma_addr && n->cq[cqid].id == cqid) {
        return 0;
    } else {
      return FAIL;
    }
}

uint32_t adm_check_sqid(NVMEState *n, uint16_t sqid)
{
    /* If queue is allocated dma_addr!=NULL and has the same ID */
    if (sqid > NVME_MAX_QID) {
        return FAIL;
    } else if (n->sq[sqid].dma_addr && n->sq[sqid].id == sqid) {
        return 0;
    } else {
        return FAIL;
    }
}

static uint16_t adm_get_sq(NVMEState *n, uint16_t sqid)
{
    if (sqid > NVME_MAX_QID) {
        return USHRT_MAX;
    } else if (n->sq[sqid].dma_addr && n->sq[sqid].id == sqid) {
        return sqid;
    } else {
        return USHRT_MAX;
    }
}

static uint16_t adm_get_cq(NVMEState *n, uint16_t cqid)
{
    if (cqid > NVME_MAX_QID) {
        return USHRT_MAX;
    } else if (n->cq[cqid].dma_addr && n->cq[cqid].id == cqid) {
        return cqid;
    } else {
        return USHRT_MAX;
    }

}

/* FIXME: For now allow only empty queue. */
static uint32_t adm_cmd_del_sq(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    /* If something is in the queue then abort all pending messages.
     * TBD: What if there is no space in cq? */
    NVMEAdmCmdDeleteSQ *c = (NVMEAdmCmdDeleteSQ *)cmd;
    NVMEIOCQueue *cq;
    NVMEIOSQueue *sq;
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    uint16_t i;
    sf->sc = NVME_SC_SUCCESS;

    LOG_NORM("%s(): called with QID:%d", __func__, c->qid);

    if (!n) {
        return FAIL;
    }
    /* Log's done to do unit testing */
    LOG_DBG("Delete SQ command for SQID: %u", c->qid);

    if (!security_state_unlocked(n)) {
        LOG_NORM("%s(): invalid security state '%c'", __func__, n->s);
        sf->sc = NVME_SC_CMD_SEQ_ERROR;
        return FAIL;
    }
    if (cmd->opcode != NVME_ADM_CMD_DELETE_SQ) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }

    if (c->qid == 0 || c->qid > NVME_MAX_QID) {
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_INVALID_QUEUE_IDENTIFIER;
        return FAIL;
    } else if (c->nsid != 0) {
        LOG_NORM("%s():Invalid namespace:%d", __func__, c->nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }

    i = adm_get_sq(n, c->qid);
    if (i == USHRT_MAX) {
        LOG_NORM("No such queue: SQ %d", c->qid);
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_INVALID_QUEUE_IDENTIFIER;
        return FAIL;
    }
    sq = &n->sq[i];
    if (sq->tail != sq->head) {
        /* Queue not empty */
    }

    if (sq->cq_id <= NVME_MAX_QID) {
        cq = &n->cq[sq->cq_id];
        if (cq->id > NVME_MAX_QID) {
            /* error */
            sf->sct = NVME_SCT_CMD_SPEC_ERR;
            sf->sc = NVME_INVALID_QUEUE_IDENTIFIER;
            return FAIL;
        }

        if (!cq->usage_cnt) {
            /* error FIXME */
        }

        cq->usage_cnt--;
    }

    sq->id = sq->cq_id = USHRT_MAX;
    sq->head = sq->tail = 0;
    sq->size = 0;
    sq->prio = 0;
    sq->phys_contig = 0;
    sq->dma_addr = 0;

    return 0;
}

static uint32_t adm_cmd_alloc_sq(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEAdmCmdCreateSQ *c = (NVMEAdmCmdCreateSQ *)cmd;
    NVMEIOSQueue *sq;
    uint16_t *mqes;
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    sf->sc = NVME_SC_SUCCESS;

    LOG_NORM("%s(): called", __func__);

    if (!n) {
        return FAIL;
    }

    if (cmd->opcode != NVME_ADM_CMD_CREATE_SQ) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (!security_state_unlocked(n)) {
        LOG_NORM("%s(): invalid security state '%c'", __func__, n->s);
        sf->sc = NVME_SC_CMD_SEQ_ERROR;
        return FAIL;
    }

    /* Log's done to do unit testing */
    LOG_DBG("Create SQ command for QID: %u", c->qid);
    LOG_DBG("Create SQ command with Qsize: %u", c->qsize);
    LOG_DBG("Create SQ command with PC bit: %u", c->pc);
    LOG_DBG("Create SQ command with unique command ID: %u", c->cid);
    LOG_DBG("Create SQ command with PRP1: %lu", c->prp1);
    LOG_DBG("Create SQ command with PRP2: %lu", c->prp2);
    LOG_DBG("Create SQ command is assoc with CQID: %u", c->cqid);

    if (c->qid == 0 || c->qid > NVME_MAX_QID) {
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_INVALID_QUEUE_IDENTIFIER;
        LOG_NORM("%s():Invalid QID:%d in Command", __func__, c->qid);
        return FAIL;
    } else if (c->nsid != 0) {
        LOG_NORM("%s():Invalid namespace id:%d", __func__, c->nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }

    /* Check if IOSQ requested be associated with ACQ */
    if (c->cqid == 0) {
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_COMPLETION_QUEUE_INVALID;
        LOG_NORM("%s(): Invalid cq id: %d association.", __func__, c->cqid);
        return FAIL;
    }

    /* Invalid SQID, exists*/
    if (!adm_check_sqid(n, c->qid)) {
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_INVALID_QUEUE_IDENTIFIER;
        LOG_NORM("%s():SQID:%d in command already allocated/invalid ID",
            __func__, c->qid);
        return FAIL;
    }

    /* Corresponding CQ exists?  if not return error */
    if (adm_check_cqid(n, c->cqid)) {
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_COMPLETION_QUEUE_INVALID;
        LOG_NORM("%s():CQID:%d in command not allocated", __func__, c->cqid);
        return FAIL;
    }
    mqes = (uint16_t *) n->cntrl_reg;

    /* Queue Size */
    if (c->qsize > *mqes) {
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_MAX_QUEUE_SIZE_EXCEEDED;
        LOG_NORM("%s():MQES %u exceeded", __func__, *mqes);
        return FAIL;
    }

    if ((c->pc == 0) && (*(mqes + 0x01) & 0x01)) {
        LOG_NORM("%s(): non-contiguous queue unsupported", __func__);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }

    /* In PRP1 is DMA address. Chapter 5.4, Figure 36 */
    if (c->prp1 == 0) {
        LOG_NORM("%s():PRP1 field is NULL", __func__);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }

    sq = &n->sq[c->qid];
    sq->id = c->qid;
    sq->size = c->qsize + 1;
    sq->phys_contig = c->pc;
    sq->cq_id = c->cqid;
    sq->prio = c->qprio;
    sq->dma_addr = c->prp1;

    QTAILQ_INIT(&sq->cmd_list);

    LOG_DBG("sq->id %d, sq->dma_addr 0x%x, %lu",
        sq->id, (unsigned int)sq->dma_addr,
        (unsigned long int)sq->dma_addr);

    /* Mark CQ as used by this queue. */
    n->cq[adm_get_cq(n, c->cqid)].usage_cnt++;

    return 0;
}

static uint32_t adm_cmd_del_cq(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEAdmCmdDeleteCQ *c = (NVMEAdmCmdDeleteCQ *)cmd;
    NVMEIOCQueue *cq;
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    uint16_t i;
    sf->sc = NVME_SC_SUCCESS;

    LOG_NORM("%s(): called", __func__);

    if (!n) {
        return FAIL;
    }

    /* Log's done to do unit testing */
    LOG_DBG("Delete CQ command for CQID: %u", c->qid);

    if (cmd->opcode != NVME_ADM_CMD_DELETE_CQ) {
        LOG_NORM("%s(): Invalid opcode %x\n", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (!security_state_unlocked(n)) {
        LOG_NORM("%s(): invalid security state '%c'", __func__, n->s);
        sf->sc = NVME_SC_CMD_SEQ_ERROR;
        return FAIL;
    }
    if (c->qid == 0 || c->qid > NVME_MAX_QID) {
        LOG_NORM("%s():Invalid Queue ID %d", __func__, c->qid);
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_INVALID_QUEUE_IDENTIFIER;
        return FAIL;
    } else if (c->nsid != 0) {
        LOG_NORM("%s():Invalid namespace:%d", __func__, c->nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }


    i = adm_get_cq(n, c->qid);
    if (i == USHRT_MAX) {
        LOG_NORM("No such queue: CQ %d", c->qid);
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_INVALID_QUEUE_IDENTIFIER;
        return FAIL;
    }
    cq = &n->cq[i];

    if (cq->tail != cq->head) {
        /* Queue not empty */
        /* error */
    }

    /* Do not allow to delete CQ when some SQ is pointing on it. */
    if (cq->usage_cnt) {
        LOG_ERR("Error. Some sq are still connected to CQ %d", c->qid);
        sf->sc = NVME_SC_INVALID_FIELD;
        return NVME_SC_INVALID_FIELD;
    }
    if (cq->pdid != 0) {
        n->protection_domains[cq->pdid - 1]->usage_count--;
    }

    cq->id = USHRT_MAX;
    cq->head = cq->tail = 0;
    cq->size = 0;
    cq->irq_enabled = 0;
    cq->vector = 0;
    cq->dma_addr = 0;
    cq->phys_contig = 0;

    return 0;
}

static uint32_t adm_cmd_alloc_cq(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEAdmCmdCreateCQ *c = (NVMEAdmCmdCreateCQ *)cmd;
    NVMEIOCQueue *cq;
    uint16_t *mqes;
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    sf->sc = NVME_SC_SUCCESS;

    LOG_NORM("%s(): called", __func__);

    if (!n) {
        return FAIL;
    }
    /* Log's done to do unit testing */
    LOG_DBG("Create CQ command for QID: %u", c->qid);
    LOG_DBG("Create CQ command with Qsize: %u", c->qsize);
    LOG_DBG("Create CQ command with PC bit: %u", c->pc);
    LOG_DBG("Create CQ command with unique command ID: %u", c->cid);
    LOG_DBG("Create CQ command with PRP1: %lu", c->prp1);
    LOG_DBG("Create CQ command with PRP2: %lu", c->prp2);

    if (cmd->opcode != NVME_ADM_CMD_CREATE_CQ) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (!security_state_unlocked(n)) {
        LOG_NORM("%s(): invalid security state '%c'", __func__, n->s);
        sf->sc = NVME_SC_CMD_SEQ_ERROR;
        return FAIL;
    }
    if (c->qid == 0 || c->qid > NVME_MAX_QID) {
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_INVALID_QUEUE_IDENTIFIER;
        LOG_NORM("%s(): invalid qid:%d in Command", __func__, c->qid);
        return FAIL;
    } else if (c->nsid != 0) {
        LOG_NORM("%s():Invalid namespace:%d", __func__, c->nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }

    /* check if CQ exists., If yes return error */
    if (!adm_check_cqid(n, c->qid)) {
        LOG_NORM("%s():Invalid CQ ID %d\n", __func__, c->qid);
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_INVALID_QUEUE_IDENTIFIER;
        return FAIL;
    }

    mqes = (uint16_t *) n->cntrl_reg;

    /* Queue Size */
    if (c->qsize > *mqes) {
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_MAX_QUEUE_SIZE_EXCEEDED;
        LOG_NORM("%s():MQES %u exceeded", __func__, *mqes);
        return FAIL;
    }

    if ((c->pc == 0) && (*(mqes + 0x01) & 0x01)) {
        LOG_ERR("CAP.CQR set to 1");
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }
    /* In PRP1 is DMA address. */
    if (c->prp1 == 0) {
        LOG_NORM("%s():PRP1 address is NULL", __func__);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }

    if (c->iv > n->dev.msix_entries_nr - 1 && IS_MSIX(n)) {
        /* TODO : checks for MSI too */
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_INVALID_INTERRUPT_VECTOR;
        return FAIL;
    }

    cq = &n->cq[c->qid];
    cq->pdid = 0;

    if (cmd->cdw14 != 0 && n->use_aon) {
        /* setting up protection domain cq */
        uint32_t pdid = cmd->cdw14;
        if (pdid == 0 || pdid > n->aon_ctrl_vs->mnpd) {
            LOG_NORM("%s(): Invalid pdid %d", __func__, pdid);
            sf->sc = NVME_AON_INVALID_PROTECTION_DOMAIN_IDENTIFIER;
            sf->sct = NVME_SCT_CMD_SPEC_ERR;
            return FAIL;
        }
        if (n->protection_domains[pdid - 1] == NULL) {
            LOG_NORM("%s(): pdid %d not allocated", __func__, pdid);
            sf->sc = NVME_AON_INVALID_PROTECTION_DOMAIN_IDENTIFIER;
            sf->sct = NVME_SCT_CMD_SPEC_ERR;
            return FAIL;
        }
        cq->pdid = pdid;
        n->protection_domains[pdid - 1]->usage_count++;
    }

    cq->id = c->qid;
    cq->dma_addr = c->prp1;
    cq->irq_enabled = c->ien;
    cq->vector = c->iv;
    cq->phase_tag = 1;

    cq->size = c->qsize + 1;
    cq->phys_contig = c->pc;

    return 0;
}

static uint32_t adm_cmd_fw_log_info(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEFwSlotInfoLog *firmware_info = &(n->fw_slot_log);
    uint32_t len;

    LOG_NORM("%s called", __func__);

    if (((cmd->cdw10 >> 16) & 0xfff) * 4 < sizeof(*firmware_info)) {
        LOG_NORM("%s: not enough memory, needs %ld, has %d bytes.", __func__,
                sizeof(*firmware_info), ((cmd->cdw10 >> 16) & 0xfff) * 4);
        NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
        sf->sc = NVME_SC_INVALID_FIELD;
        return 0;
    }

    len = min(PAGE_SIZE - (cmd->prp1 % PAGE_SIZE), sizeof(*firmware_info));
    nvme_dma_mem_write(cmd->prp1, (uint8_t *)firmware_info, len);
    if (len < sizeof(*firmware_info)) {
        nvme_dma_mem_write(cmd->prp2,
            (uint8_t *)((uint8_t *)firmware_info + len),
            sizeof(*firmware_info) - len);
    }
    return 0;
}

static uint32_t adm_cmd_smart_info(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    uint32_t len;
    time_t current_seconds;
    NVMESmartLog smart_log;

    if (((cmd->cdw10 >> 16) & 0xfff) * 4 < sizeof(smart_log)) {
        LOG_NORM("%s: not enough memory, needs %ld, has %d bytes.", __func__,
                sizeof(smart_log), ((cmd->cdw10 >> 16) & 0xfff) * 4);
        NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
        sf->sc = NVME_SC_INVALID_FIELD;
        return 0;
    }

    memset(&smart_log, 0x0, sizeof(smart_log));
    LOG_NORM("%s called", __func__);
    if (cmd->nsid == 0xffffffff) {
        /* return info for entire device */
        int i;
        uint64_t dur[2] = {0, 0};
        uint64_t duw[2] = {0, 0};
        uint64_t hrc[2] = {0, 0};
        uint64_t hwc[2] = {0, 0};
        uint64_t total_use = 0;
        uint64_t total_size = 0;
        for (i = 0; i < n->num_namespaces; ++i) {
            uint64_t tmp;
            DiskInfo *disk = n->disk[i];
            if (disk == NULL) {
                continue;
            }

            tmp = dur[0];
            dur[0] += disk->data_units_read[0];
            dur[1] += disk->data_units_read[1];
            if (tmp > dur[0]) {
                ++dur[1];
            }

            tmp = duw[0];
            duw[0] += disk->data_units_written[0];
            duw[1] += disk->data_units_written[1];
            if (tmp > duw[0]) {
                ++duw[1];
            }

            tmp = hrc[0];
            hrc[0] += disk->host_read_commands[0];
            hrc[1] += disk->host_read_commands[1];
            if (tmp > hrc[0]) {
                ++hrc[1];
            }

            tmp = hwc[0];
            hwc[0] += disk->host_write_commands[0];
            hwc[1] += disk->host_write_commands[1];
            if (tmp > hwc[0]) {
                ++hwc[1];
            }

            total_size += disk->idtfy_ns.nsze;
            total_use += disk->idtfy_ns.nuse;
        }

        smart_log.data_units_read[0] = dur[0];
        smart_log.data_units_read[1] = dur[1];
        smart_log.data_units_written[0] = duw[0];
        smart_log.data_units_written[1] = duw[1];
        smart_log.host_read_commands[0] = hrc[0];
        smart_log.host_read_commands[1] = hrc[1];
        smart_log.host_write_commands[0] = hwc[0];
        smart_log.host_write_commands[1] = hwc[1];
        smart_log.available_spare = 100 - (uint32_t)((((double)total_use) /
            total_size) * 100);
    } else if (cmd->nsid > 0 && cmd->nsid <= n->num_namespaces) {
        LOG_NORM("getting smart log info for instance:%d nsid:%d",
            n->instance, cmd->nsid);

        DiskInfo *disk = n->disk[cmd->nsid - 1];
        if (disk == NULL) {
            NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
            sf->sc = NVME_SC_INVALID_NAMESPACE;
            return 0;
        }

        smart_log.data_units_read[0] = disk->data_units_read[0];
        smart_log.data_units_read[1] = disk->data_units_read[1];
        smart_log.data_units_written[0] = disk->data_units_written[0];
        smart_log.data_units_written[1] = disk->data_units_written[1];
        smart_log.host_read_commands[0] = disk->host_read_commands[0];
        smart_log.host_read_commands[1] = disk->host_read_commands[1];
        smart_log.host_write_commands[0] = disk->host_write_commands[0];
        smart_log.host_write_commands[1] = disk->host_write_commands[1];
        smart_log.available_spare = 100 - (uint32_t)
            ((((double)disk->idtfy_ns.nuse) / disk->idtfy_ns.nsze) * 100);
    } else {
        NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return 0;
    }
    if (n->injected_available_spare) {
        smart_log.available_spare = n->injected_available_spare;
    }

    smart_log.temperature[0] = n->temperature & 0xff;
    smart_log.temperature[1] = (n->temperature >> 8) & 0xff;
    smart_log.percentage_used = n->percentage_used;

    current_seconds = time(NULL);
    smart_log.power_on_hours[0] = ((current_seconds - n->start_time) / 60) / 60;

    smart_log.available_spare_threshold = NVME_SPARE_THRESH;
    if (smart_log.available_spare <= NVME_SPARE_THRESH) {
        smart_log.critical_warning |= 1 << 0;
    }
    if (n->feature.temperature_threshold <= NVME_TEMPERATURE) {
        smart_log.critical_warning |= 1 << 1;
    }

    len = min(PAGE_SIZE - (cmd->prp1 % PAGE_SIZE), sizeof(smart_log));
    nvme_dma_mem_write(cmd->prp1, (uint8_t *)&smart_log, len);
    if (len < sizeof(smart_log)) {
        nvme_dma_mem_write(cmd->prp2, (uint8_t *)((uint8_t *)&smart_log + len),
            sizeof(smart_log) - len);
    }
    return 0;
}

static uint32_t adm_cmd_get_log_page(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEAdmCmdGetLogPage *c = (NVMEAdmCmdGetLogPage *)cmd;
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    sf->sc = NVME_SC_SUCCESS;

    if (cmd->opcode != NVME_ADM_CMD_GET_LOG_PAGE) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }

    LOG_NORM("%s(): called", __func__);

    switch (c->lid) {
    case NVME_LOG_ERROR_INFORMATION:
        break;
    case NVME_LOG_SMART_INFORMATION:
        return adm_cmd_smart_info(n, cmd, cqe);
        break;
    case NVME_LOG_FW_SLOT_INFORMATION:
        return adm_cmd_fw_log_info(n, cmd, cqe);
        break;
    default:
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_INVALID_LOG_PAGE;
        break;
    }
    return 0;
}

static uint32_t adm_cmd_id_ctrl(NVMEState *n, NVMECmd *cmd)
{
    uint32_t len;
    LOG_NORM("%s(): copying %lu data into addr %#lx",
        __func__, sizeof(*n->idtfy_ctrl), cmd->prp1);

    len = PAGE_SIZE - (cmd->prp1 % PAGE_SIZE);
    nvme_dma_mem_write(cmd->prp1, (uint8_t *) n->idtfy_ctrl, len);
    if (len != sizeof(*(n->idtfy_ctrl))) {
        nvme_dma_mem_write(cmd->prp2,
            (uint8_t *) ((uint8_t *) n->idtfy_ctrl + len),
                (sizeof(*(n->idtfy_ctrl)) - len));
    }
    return 0;
}

/* Needs to be checked if this namespace exists. */
static uint32_t adm_cmd_id_ns(DiskInfo *disk, NVMECmd *cmd)
{
    uint32_t len;
    LOG_NORM("%s(): nsid:%d called", __func__, cmd->nsid);

    if (disk == NULL) {
        uint8_t blank_ns[4096];
        LOG_NORM("%s(): empty slot in namespace, fill 0", __func__);
        memset(blank_ns, 0x0, sizeof(blank_ns));
        len = PAGE_SIZE - (cmd->prp1 % PAGE_SIZE);
        nvme_dma_mem_write(cmd->prp1, blank_ns, len);
        if (len != sizeof(blank_ns)) {
            nvme_dma_mem_write(cmd->prp2, (uint8_t *)(blank_ns + len),
                sizeof(blank_ns) - len);
        }
        return 0;
    }

    LOG_DBG("Current Namespace utilization: %lu",
        disk->idtfy_ns.nuse);

    len = PAGE_SIZE - (cmd->prp1 % PAGE_SIZE);
    nvme_dma_mem_write(cmd->prp1, (uint8_t *) &disk->idtfy_ns, len);
    if (len != sizeof(disk->idtfy_ns)) {
        nvme_dma_mem_write(cmd->prp2, (uint8_t *)((uint8_t *)
            &disk->idtfy_ns + len), (sizeof(disk->idtfy_ns) - len));
    }
    return 0;
}

static uint32_t adm_cmd_identify(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEAdmCmdIdentify *c = (NVMEAdmCmdIdentify *)cmd;
    uint8_t ret;
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    sf->sc = NVME_SC_SUCCESS;

    LOG_NORM("%s(): called", __func__);

    if (cmd->opcode != NVME_ADM_CMD_IDENTIFY) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }

    if (c->prp1 == 0) {
        LOG_NORM("%s(): prp1 is NULL", __func__);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }

    /* Construct some data and copy it to the addr.*/
    if (c->cns == NVME_IDENTIFY_CONTROLLER) {
        if (c->nsid != 0) {
            LOG_NORM("%s(): Invalid namespace id:%d for id controller",
                __func__, c->nsid);
            sf->sc = NVME_SC_INVALID_NAMESPACE;
            return FAIL;
        }
        ret = adm_cmd_id_ctrl(n, cmd);
    } else {
        /* Check for name space */
        if (c->nsid == 0 || (c->nsid > n->idtfy_ctrl->nn)) {
            LOG_NORM("%s(): Invalid namespace id:%d, valid range:1 - %d",
                __func__, c->nsid, n->idtfy_ctrl->nn);
            sf->sc = NVME_SC_INVALID_NAMESPACE;
            return FAIL;
        }
        if (!security_state_unlocked(n)) {
            LOG_NORM("%s(): invalid security state '%c'", __func__, n->s);
            sf->sc = NVME_SC_CMD_SEQ_ERROR;
            return FAIL;
        }
        ret = adm_cmd_id_ns(n->disk[cmd->nsid - 1], cmd);
    }
    if (ret) {
        sf->sc = NVME_SC_INTERNAL;
    }
    return 0;
}


/* 5.1 Abort command
 * The Abort command is used to cancel/abort a specific I/O command previously
 * issued to the Admin or an I/O Submission Queue.Host software may have
 * multiple Abort commands outstanding, subject to the constraints of the
 * Abort Command Limit indicated in the Identify Controller data structure.
 * An abort is a best effort command; the command to abort may have already
 * completed, currently be in execution, or may be deeply queued.
 * It is implementation specific if/when a controller chooses to complete
 * the command with an error (i.e., Requested Command to Abort Not Found)
 * when the command to abort is not found.
*/
static uint32_t adm_cmd_abort(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEAdmCmdAbort *c = (NVMEAdmCmdAbort *)cmd;
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    NVMEIOSQueue *sq;

    sf->sc = NVME_SC_SUCCESS;
    CommandEntry *ce;

    if (cmd->opcode != NVME_ADM_CMD_ABORT) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (!security_state_unlocked(n)) {
        LOG_NORM("%s(): invalid security state '%c'", __func__, n->s);
        sf->sc = NVME_SC_CMD_SEQ_ERROR;
        return FAIL;
    }
    if (c->nsid != 0) {
        LOG_NORM("%s():Invalid namespace id:%d", __func__, c->nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }
    if (c->sqid == 0 || adm_check_sqid(n, c->sqid)) {
        LOG_NORM("Invalid queue:%d to abort", c->sqid);
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        sf->sc = NVME_REQ_CMD_TO_ABORT_NOT_FOUND;
        return FAIL;
    }
    LOG_NORM("%s(): called", __func__);

    sq = &n->sq[c->sqid];
    QTAILQ_FOREACH(ce, &sq->cmd_list, entry) {
        if (ce->cid == c->cmdid) {
            uint16_t aborted_cq_id;
            NVMECQE acqe;
            NVMEIOCQueue *cq;
            NVMEStatusField *aborted_sf = (NVMEStatusField *) &acqe.status;

            aborted_cq_id = sq->cq_id;
            if (adm_check_cqid(n, aborted_cq_id)) {
                LOG_ERR("Abort, submission queue:%d has invalid cq:%d", sq->id,
                    aborted_cq_id);
                sf->sct = NVME_SCT_CMD_SPEC_ERR;
                sf->sc = NVME_REQ_CMD_TO_ABORT_NOT_FOUND;
                return FAIL;
            }
            cq = &n->cq[aborted_cq_id];

            memset(&acqe, 0, sizeof(acqe));
            aborted_sf->p = cq->phase_tag;
            aborted_sf->sc = NVME_SC_ABORT_REQ;

            acqe.sq_id = c->sqid;
            acqe.sq_head = sq->head;
            acqe.command_id = c->cmdid;

            post_cq_entry(n, cq, &acqe);

            QTAILQ_REMOVE(&sq->cmd_list, ce, entry);
            qemu_free(ce);

            LOG_NORM("Abort cmdid:%d on sq:%d success", c->cmdid, sq->id);

            return 0;
        }
    }
    LOG_NORM("Abort failed, could not find corresponding cmdid:%d on sq:%d",
        c->cmdid, sq->id);
    return FAIL;
}

static uint32_t do_features(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEAdmCmdFeatures *sqe = (NVMEAdmCmdFeatures *)cmd;
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    sf->sc = NVME_SC_SUCCESS;

    DiskInfo *disk;
    uint32_t len;
    uint32_t nsid = cmd->nsid;

    switch (sqe->fid) {
    case NVME_FEATURE_ARBITRATION:
        if (sqe->opcode == NVME_ADM_CMD_SET_FEATURES) {
            n->feature.arbitration = sqe->cdw11;
        } else {
            cqe->cmd_specific = n->feature.arbitration;
        }
        break;

    case NVME_FEATURE_POWER_MANAGEMENT:
        if (sqe->opcode == NVME_ADM_CMD_SET_FEATURES) {
            n->feature.power_management = sqe->cdw11;
        } else {
            cqe->cmd_specific = n->feature.power_management;
        }
        break;

    case NVME_FEATURE_LBA_RANGE_TYPE:
        if (nsid > n->num_namespaces || nsid == 0) {
            LOG_NORM("%s(): bad nsid:%d", __func__, nsid);
            sf->sc = NVME_SC_INVALID_NAMESPACE;
            return FAIL;
        }
        if (n->disk[nsid - 1] == NULL) {
            LOG_NORM("%s(): nsid:%d not created", __func__, nsid);
            sf->sc = NVME_SC_INVALID_NAMESPACE;
            return FAIL;
        }
        disk = n->disk[nsid - 1];
        len = min(PAGE_SIZE - (cmd->prp1 % PAGE_SIZE), sizeof(disk->range_type));
        if (sqe->opcode == NVME_ADM_CMD_SET_FEATURES) {
            LBARangeType rt;
            nvme_dma_mem_read(cmd->prp1, (uint8_t *)&rt, len);
            if (len != sizeof(rt)) {
                nvme_dma_mem_read(cmd->prp2, (uint8_t *)
                    (((uint8_t *)&(rt)) + len),
                    (sizeof(rt) - len));
            }
            disk->range_type.type = rt.type;
            disk->range_type.attributes = rt.attributes;
        } else {
            nvme_dma_mem_write(cmd->prp1, (uint8_t *)&disk->range_type, len);
            if (len != sizeof(disk->range_type)) {
                nvme_dma_mem_write(cmd->prp2, (uint8_t *)
                    (((uint8_t *)&(disk->range_type)) + len),
                    (sizeof(disk->range_type) - len));
            }
            cqe->cmd_specific = 0;
        }
        break;

    case NVME_FEATURE_TEMPERATURE_THRESHOLD:
        if (sqe->opcode == NVME_ADM_CMD_SET_FEATURES) {
            n->feature.temperature_threshold = sqe->cdw11;
            if (n->feature.temperature_threshold <= NVME_TEMPERATURE &&
                    !n->temp_warn_issued) {
                LOG_NORM("Device:%d setting temp threshold feature to:%d",
                    n->instance, n->feature.temperature_threshold);
                n->temp_warn_issued = 1;
                enqueue_async_event(n, event_type_smart,
                    event_info_smart_temp_thresh, NVME_LOG_SMART_INFORMATION);
            }
        } else {
            cqe->cmd_specific = n->feature.temperature_threshold;
        }
        break;

    case NVME_FEATURE_ERROR_RECOVERY:
        if (sqe->opcode == NVME_ADM_CMD_SET_FEATURES) {
            n->feature.error_recovery = sqe->cdw11;
        } else {
            cqe->cmd_specific = n->feature.error_recovery;
        }
        break;

    case NVME_FEATURE_VOLATILE_WRITE_CACHE:
        if (sqe->opcode == NVME_ADM_CMD_SET_FEATURES) {
            n->feature.volatile_write_cache = sqe->cdw11;
        } else {
            cqe->cmd_specific = n->feature.volatile_write_cache;
        }
        break;

    case NVME_FEATURE_NUMBER_OF_QUEUES:
        if (sqe->opcode == NVME_ADM_CMD_SET_FEATURES) {
            cqe->cmd_specific = n->feature.number_of_queues;
        } else {
            cqe->cmd_specific = n->feature.number_of_queues;
        }
        break;

    case NVME_FEATURE_INTERRUPT_COALESCING:
        if (sqe->opcode == NVME_ADM_CMD_SET_FEATURES) {
            n->feature.interrupt_coalescing = sqe->cdw11;
        } else {
            cqe->cmd_specific = n->feature.interrupt_coalescing;
        }
        break;

    case NVME_FEATURE_INTERRUPT_VECTOR_CONF:
        if (sqe->opcode == NVME_ADM_CMD_SET_FEATURES) {
            n->feature.interrupt_vector_configuration = sqe->cdw11;
        } else {
            cqe->cmd_specific =
                n->feature.interrupt_vector_configuration;
        }
        break;

    case NVME_FEATURE_WRITE_ATOMICITY:
        if (sqe->opcode == NVME_ADM_CMD_SET_FEATURES) {
            n->feature.write_atomicity = sqe->cdw11;
        } else {
            cqe->cmd_specific = n->feature.write_atomicity;
        }
        break;

    case NVME_FEATURE_ASYNCHRONOUS_EVENT_CONF:
        if (sqe->opcode == NVME_ADM_CMD_SET_FEATURES) {
            n->feature.asynchronous_event_configuration = sqe->cdw11;
        } else {
            cqe->cmd_specific =
                n->feature.asynchronous_event_configuration;
        }
        break;

    case NVME_FEATURE_SOFTWARE_PROGRESS_MARKER: /* Set Features only*/
        if (sqe->opcode == NVME_ADM_CMD_GET_FEATURES) {
            cqe->cmd_specific = n->feature.software_progress_marker;
        }
        break;

    case NVME_FEATURE_FULTON_STRIPING_CFG:
        if (n->fultondale && sqe->opcode == NVME_ADM_CMD_GET_FEATURES) {
            cqe->cmd_specific = fultondale_boundary_feature[n->fultondale];
            break;
        }
        /* fall through */
    default:
        LOG_NORM("Unknown feature ID: %d", sqe->fid);
        sf->sc = NVME_SC_INVALID_FIELD;
        break;
    }

    return 0;
}

static uint32_t adm_cmd_set_features(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    uint32_t res;

    if (cmd->opcode != NVME_ADM_CMD_SET_FEATURES) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (!security_state_unlocked(n)) {
        LOG_NORM("%s(): invalid security state '%c'", __func__, n->s);
        sf->sc = NVME_SC_CMD_SEQ_ERROR;
        return FAIL;
    }

    res = do_features(n, cmd, cqe);

    LOG_NORM("%s(): called", __func__);
    return res;
}

static uint32_t adm_cmd_get_features(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    uint32_t res;

    if (cmd->opcode != NVME_ADM_CMD_GET_FEATURES) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }

    res = do_features(n, cmd, cqe);

    LOG_NORM("%s(): called", __func__);
    return res;
}

static uint64_t DJBHash(uint8_t* str, uint32_t len)
{
    uint64_t hash = 5381;
    uint64_t i    = 0;

    for(i = 0; i < len; str++, i++) {
        hash = ((hash << 5) + hash) + (*str);
    }

    return hash;
}

static uint32_t adm_cmd_act_fw(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    int fd;
    unsigned sz_fw_buf;
    struct stat sb;
    char fw_file_name[] = "nvme_firmware_disk.img";
    uint8_t *fw_buf;
    char fw_hash[9];
    uint8_t *target_frs;

    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    uint32_t res = 0;

    LOG_NORM("%s(): called", __func__);

    if (cmd->opcode != NVME_ADM_CMD_ACTIVATE_FW) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (!security_state_unlocked(n)) {
        LOG_NORM("%s(): invalid security state '%c'", __func__, n->s);
        sf->sc = NVME_SC_CMD_SEQ_ERROR;
        return FAIL;
    }

    if ((cmd->cdw10 & 0x7) > 7) {
        LOG_NORM("%s(): Invalid Firmware Slot %d", __func__, cmd->cdw10 & 0x7);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }

    fd = open(fw_file_name, O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        LOG_ERR("Error while creating the storage");
        return FAIL;
    }
    if (stat(fw_file_name, &sb) != 0) {
        LOG_ERR("%s(): 'stat' failed for '%s'", __func__, fw_file_name);
        res = FAIL;
        goto close;
    }
    sz_fw_buf = sb.st_size;

    fw_buf = mmap(NULL, sz_fw_buf, PROT_READ, MAP_SHARED, fd, 0);
    if (fw_buf == MAP_FAILED) {
        LOG_ERR("Error mapping FW disk backing file");
        res = FAIL;
        goto close;
    }

    snprintf(fw_hash, 9, "%lx", DJBHash(fw_buf, sz_fw_buf));

    if ((cmd->cdw10 & 0x7) > 0)
        n->fw_slot_log.afi = cmd->cdw10 & 0x7;
    else {
        uint8_t i;
        uint64_t *accessor = (uint64_t *)&(n->fw_slot_log);

        for (i = 1; i <= 7; i++) {
            if(*(accessor + i) == 0) {
                n->fw_slot_log.afi = i;
                break;
            }
        }
        if (i == 8)
            n->fw_slot_log.afi = ((n->last_fw_slot + 1) > 7) ? 1 :
                                  (n->last_fw_slot + 1);
    }
    target_frs = (uint8_t *)&(n->fw_slot_log) + (n->fw_slot_log.afi * 8);

    memcpy ((char *)target_frs, fw_hash, 8);
    memcpy ((char *)n->idtfy_ctrl->fr, fw_hash, 8);
    n->last_fw_slot = n->fw_slot_log.afi;

    munmap(fw_buf, sz_fw_buf);
    if(ftruncate(fd, 0) < 0)
        LOG_ERR("Error truncating backing firmware file");

 close:
    close(fd);
    return res;
}

static uint8_t do_dlfw_prp(NVMEState *n, uint64_t mem_addr,
    uint64_t *data_size_p, uint64_t *buf_offset, uint8_t *buf)
{
    uint64_t data_len;

    if (*data_size_p == 0) {
        return FAIL;
    }

    /* Data Len to be written per page basis */
    data_len = PAGE_SIZE - (mem_addr % PAGE_SIZE);
    if (data_len > *data_size_p) {
        data_len = *data_size_p;
    }

    LOG_DBG("Length of FW Img:%ld", data_len);
    LOG_DBG("Address for FW Img:%ld", mem_addr);
    nvme_dma_mem_read(mem_addr, (buf + *buf_offset), data_len);

    *buf_offset = *buf_offset + data_len;
    *data_size_p = *data_size_p - data_len;
    return NVME_SC_SUCCESS;
}

static uint8_t do_dlfw_prp_list(NVMEState *n, NVMECmd *cmd,
    uint64_t *data_size_p, uint64_t *buf_offset, uint8_t *buf)
{
    uint64_t prp_list[512], prp_entries;
    uint16_t i;
    uint8_t res = NVME_SC_SUCCESS;

    LOG_DBG("Data Size remaining for FW Image:%ld", *data_size_p);

    /* Logic to find the number of PRP Entries */
    prp_entries = (uint64_t) ((*data_size_p + PAGE_SIZE - 1) / PAGE_SIZE);
    nvme_dma_mem_read(cmd->prp2, (uint8_t *)prp_list,
        min(sizeof(prp_list), prp_entries * sizeof(uint64_t)));

    i = 0;
    /* Read/Write on PRPList */
    while (*data_size_p != 0) {
        if (i == 511 && *data_size_p > PAGE_SIZE) {
            /* Calculate the actual number of remaining entries */
            prp_entries = (uint64_t) ((*data_size_p + PAGE_SIZE - 1) /
                PAGE_SIZE);
            nvme_dma_mem_read(prp_list[511], (uint8_t *)prp_list,
                min(sizeof(prp_list), prp_entries * sizeof(uint64_t)));
            i = 0;
        }

        res = do_dlfw_prp(n, prp_list[i], data_size_p, buf_offset, buf);
        LOG_DBG("Data Size remaining for read/write:%ld", *data_size_p);
        if (res == FAIL) {
            break;
        }
        i++;
    }
    return res;
}

static uint32_t fw_get_img(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe,
                                     uint8_t *buf, uint32_t sz_fw_buf)
{
    uint32_t res = 0;
    uint64_t data_size = sz_fw_buf;
    uint64_t buf_offset = 0;
    int fd;
    uint64_t offset = 0;
    uint64_t bytes_written = 0;

    fd = open("nvme_firmware_disk.img", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        LOG_ERR("Error while creating the storage");
        return FAIL;
    }

    /* Reading PRP1 and PRP2 */
    res = do_dlfw_prp(n, cmd->prp1, &data_size, &buf_offset, buf);
    if (res == FAIL) {
        return FAIL;
    }
    if (data_size > 0) {
        if (data_size <= PAGE_SIZE) {
            res = do_dlfw_prp(n, cmd->prp2, &data_size, &buf_offset, buf);
        } else {
            res = do_dlfw_prp_list(n, cmd, &data_size, &buf_offset, buf);
        }
        if (res == FAIL) {
            return FAIL;
        }
    }

    /* Writing to Firmware image file */
    offset = cmd->cdw11 * 4;
    if (lseek(fd, (off_t)offset, SEEK_SET) != offset) {
        LOG_ERR("Error while seeking to offset %ld", offset);
        return FAIL;
    }

    LOG_NORM("Writing buffer: size = %d, offset = %ld", sz_fw_buf, offset);
    bytes_written = write(fd, buf, sz_fw_buf);
    if (bytes_written != sz_fw_buf) {
        LOG_ERR("Error while writing: %ld written out of %d", bytes_written,
                                                                sz_fw_buf);
        return FAIL;
    }

    if (close(fd) < 0) {
        LOG_ERR("Unable to close the nvme disk");
    }

    return res;
}

static uint32_t adm_cmd_dl_fw(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    uint32_t res = 0;
    uint8_t *fw_buf;
    uint32_t sz_fw_buf = 0;

    LOG_NORM("%s(): called", __func__);

    if (cmd->opcode != NVME_ADM_CMD_DOWNLOAD_FW) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (!security_state_unlocked(n)) {
        LOG_NORM("%s(): invalid security state '%c'", __func__, n->s);
        sf->sc = NVME_SC_CMD_SEQ_ERROR;
        return FAIL;
    }

    if (cmd->prp1 == 0) {
        LOG_NORM("%s(): prp1 absent", __func__);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }

    sz_fw_buf = (cmd->cdw10 + 1) * sizeof(uint8_t) * 4;
    fw_buf = (uint8_t *) qemu_mallocz(sz_fw_buf);
    if (fw_buf == NULL) {
        return FAIL;
    }
    LOG_DBG("sz_fw_buf = %d", sz_fw_buf);
    res = fw_get_img(n, cmd, cqe, fw_buf, sz_fw_buf);

    qemu_free(fw_buf);

    return res;
}

void async_process_cb(void *param)
{
    NVMECQE cqe;
    NVMEStatusField *sf = (NVMEStatusField *)&cqe.status;
    NVMEState *n = (NVMEState *)param;
    target_phys_addr_t addr;
    AsyncResult *result;
    AsyncEvent *event;

    if (n->outstanding_asyncs <= 0) {
        LOG_NORM("%s(): called without an outstanding async event", __func__);
        return;
    }
    if (QSIMPLEQ_EMPTY(&n->async_queue)) {
        LOG_NORM("%s(): called with no outstanding events to report", __func__);
        return;
    }

    LOG_NORM("%s(): called outstanding asyncs:%d", __func__,
        n->outstanding_asyncs);

    while ((event = QSIMPLEQ_FIRST(&n->async_queue)) != NULL &&
            n->outstanding_asyncs > 0) {
        QSIMPLEQ_REMOVE_HEAD(&n->async_queue, entry);

        result = (AsyncResult *)&cqe.cmd_specific;
        result->event_type = event->result.event_type;
        result->event_info = event->result.event_info;
        result->log_page   = event->result.log_page;

        qemu_free(event);

        n->outstanding_asyncs--;

        cqe.sq_id = 0;
        cqe.sq_head = n->sq[0].head;
        cqe.command_id = n->async_cid[n->outstanding_asyncs];

        sf->sc = NVME_SC_SUCCESS;
        sf->p = n->cq[0].phase_tag;
        sf->m = 0;
        sf->dnr = 0;

        addr = n->cq[0].dma_addr + n->cq[0].tail * sizeof(cqe);
        nvme_dma_mem_write(addr, (uint8_t *)&cqe, sizeof(cqe));
        incr_cq_tail(&n->cq[0]);
    }
    msix_notify(&(n->dev), 0);
}

static uint32_t adm_cmd_async_ev_req(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    sf->sc = NVME_SC_SUCCESS;

    if (cmd->opcode != NVME_ADM_CMD_ASYNC_EV_REQ) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (!security_state_unlocked(n)) {
        LOG_NORM("%s(): invalid security state '%c'", __func__, n->s);
        sf->sc = NVME_SC_CMD_SEQ_ERROR;
        return FAIL;
    }

    if (n->outstanding_asyncs > n->idtfy_ctrl->aerl) {
        LOG_NORM("%s(): too many asyncs %d %d", __func__, n->outstanding_asyncs,
            n->idtfy_ctrl->aerl);
        sf->sc = NVME_ASYNC_EVENT_LIMIT_EXCEEDED;
        return FAIL;
    }

    LOG_NORM("%s(): called", __func__);

    n->async_cid[n->outstanding_asyncs] = cmd->cid;
    qemu_mod_timer(n->async_event_timer, qemu_get_clock_ns(vm_clock) + 10000);
    n->outstanding_asyncs++;

    return 0;
}

static uint32_t adm_cmd_format_nvm(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    DiskInfo *disk;
    uint64_t old_size;
    uint32_t dw10 = cmd->cdw10;
    uint32_t nsid, block_size;
    uint8_t pil = (dw10 >> 5) & 0x8;
    uint8_t pi = (dw10 >> 5) & 0x7;
    uint8_t meta_loc = dw10 & 0x10;
    uint8_t lba_idx = dw10 & 0xf;

    sf->sc = NVME_SC_SUCCESS;
    if (cmd->opcode != NVME_ADM_CMD_FORMAT_NVM) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (!security_state_unlocked(n)) {
        LOG_NORM("%s(): invalid security state '%c'", __func__, n->s);
        sf->sc = NVME_SC_CMD_SEQ_ERROR;
        return FAIL;
    }
    nsid = cmd->nsid;
    if (nsid > n->num_namespaces || nsid == 0) {
        LOG_NORM("%s(): bad nsid:%d", __func__, nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }
    if (n->disk[nsid - 1] == NULL) {
        LOG_NORM("%s(): nsid:%d not created", __func__, nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }

    disk = n->disk[nsid - 1];
    if ((lba_idx) > disk->idtfy_ns.nlbaf) {
        LOG_NORM("%s(): Invalid format %x, lbaf out of range", __func__, dw10);
        sf->sc = NVME_INVALID_FORMAT;
        return FAIL;
    }
    if (pi) {
        if (pil && !(disk->idtfy_ns.dpc & 0x10)) {
            LOG_NORM("%s(): pi requested as last 8 bytes, dpc:%x",
                __func__, disk->idtfy_ns.dpc);
            sf->sc = NVME_INVALID_FORMAT;
            return FAIL;
        }
        if (!pil && !(disk->idtfy_ns.dpc & 0x8)) {
            LOG_NORM("%s(): pi requested as first 8 bytes, dpc:%x",
                __func__, disk->idtfy_ns.dpc);
            sf->sc = NVME_INVALID_FORMAT;
            return FAIL;
        }
        if (!((disk->idtfy_ns.dpc & 0x7) & (1 << (pi - 1)))) {
            LOG_NORM("%s(): Invalid pi type:%d, dpc:%x", __func__,
                pi, disk->idtfy_ns.dpc);
            sf->sc = NVME_INVALID_FORMAT;
            return FAIL;
        }
    }
    if (meta_loc && disk->idtfy_ns.lbaf[lba_idx].ms &&
            !(disk->idtfy_ns.mc & 1)) {
        LOG_NORM("%s(): Invalid meta location:%x, mc:%x", __func__,
            meta_loc, disk->idtfy_ns.mc);
        sf->sc = NVME_INVALID_FORMAT;
        return FAIL;
    }
    if (!meta_loc && disk->idtfy_ns.lbaf[lba_idx].ms &&
            !(disk->idtfy_ns.mc & 2)) {
        LOG_NORM("%s(): Invalid meta location:%x, mc:%x", __func__,
            meta_loc, disk->idtfy_ns.mc);
        sf->sc = NVME_INVALID_FORMAT;
        return FAIL;
    }

    if (nvme_close_storage_disk(disk)) {
        return FAIL;
    }

    old_size = disk->idtfy_ns.nsze * (1 << disk->idtfy_ns.lbaf[
        disk->idtfy_ns.flbas & 0xf].lbads);
    block_size = 1 << disk->idtfy_ns.lbaf[lba_idx].lbads;

    LOG_NORM("%s(): called, previous: flbas:%x ds:%d ms:%d dps:%x"\
             "new: flbas:%x ds:%d ms:%d dpc:%x", __func__, disk->idtfy_ns.flbas,
             disk->idtfy_ns.lbaf[disk->idtfy_ns.flbas & 0xf].lbads,
             disk->idtfy_ns.lbaf[disk->idtfy_ns.flbas & 0xf].ms,
             disk->idtfy_ns.dps, lba_idx | meta_loc,
             disk->idtfy_ns.lbaf[lba_idx].lbads,
             disk->idtfy_ns.lbaf[lba_idx].ms, pil | pi);

    disk->idtfy_ns.nuse = 0;
    disk->idtfy_ns.flbas = lba_idx | meta_loc;
    disk->idtfy_ns.nsze = old_size / block_size;
    disk->idtfy_ns.ncap = disk->idtfy_ns.nsze;
    disk->idtfy_ns.dps = pil | pi;

    if (nvme_create_storage_disk(n->instance, nsid, disk, n)) {
        return FAIL;
    }

    return 0;
}

/* aon specific */
static uint32_t aon_adm_cmd_create_ns(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    NVMEIdentifyNamespace ns;
    uint32_t len;
    uint32_t nsid;
    uint64_t ns_bytes;
    uint32_t block_shift;

    sf->sc = NVME_SC_SUCCESS;

    if (cmd->opcode != AON_ADM_CMD_CREATE_NAMESPACE || !n->use_aon) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (cmd->prp1 == 0) {
        LOG_NORM("%s(): prp1 is NULL", __func__);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }

    nsid = cmd->nsid;
    if (nsid > n->num_namespaces || nsid == 0) {
        LOG_NORM("%s(): bad nsid:%d", __func__, nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }

    if (n->disk[nsid - 1] != NULL) {
        LOG_NORM("%s(): nsid:%d already created", __func__, nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }

    LOG_NORM("%s(): called", __func__);

    len = PAGE_SIZE - (cmd->prp1 % PAGE_SIZE);
    nvme_dma_mem_read((target_phys_addr_t)cmd->prp1, (uint8_t *)&ns, len);
    if (len != sizeof(ns)) {
        if (cmd->prp2 == 0) {
            LOG_NORM("%s(): prp2 is NULL", __func__);
            sf->sc = NVME_SC_INVALID_FIELD;
            return FAIL;
        }
        nvme_dma_mem_write(cmd->prp2,
            (uint8_t *) ((uint8_t *) ((uint8_t *)&ns) + len),
                (sizeof(ns) - len));
    }

    ns.nlbaf = n->aon_ctrl_vs->nlbaf;
    memcpy(ns.lbaf, n->aon_ctrl_vs->lbaf, sizeof(n->aon_ctrl_vs->lbaf));
    block_shift = ns.lbaf[ns.flbas & 0xf].lbads;
    ns_bytes = ns.nsze * (1 << block_shift);
    if (ns_bytes > n->available_space ||
            ns_bytes < (1 << n->aon_ctrl_vs->mns)) {
        LOG_NORM("%s(): bad ns size:%lu available space:%lu", __func__,
            ns.nsze, n->available_space);
        sf->sc = NVME_AON_INVALID_NAMESPACE_SIZE;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (ns.ncap != ns.nsze) {
        LOG_NORM("%s(): bad ncap:%ld, nsze:%ld", __func__, ns.ncap, ns.nsze);
        sf->sc = NVME_AON_INVALID_NAMESPACE_CAPACITY;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (ns.mc & ~(n->aon_ctrl_vs->mc) || ns.dpc & ~(n->aon_ctrl_vs->dpc) ||
            !nvme_dps_valid(ns.dps, ns.dpc)) {
        /* does not support selected meta-data or data protection */
        LOG_NORM("%s(): bad data protection/meta-data: mc:%x dpc:%x dps:%x\n"
            "  Controller supports: mc:%x dpc:%x", __func__, ns.mc, ns.dpc,
            ns.dps, n->aon_ctrl_vs->mc, n->aon_ctrl_vs->dpc);
        sf->sc = NVME_AON_INVALID_END_TO_END_DATA_PROTECTION_CONFIGURATION;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }

    set_bit(nsid, n->nn_vector);
    n->idtfy_ctrl->nn = find_last_bit(n->nn_vector, n->num_namespaces + 2);
    n->available_space -= ns_bytes;
    n->disk[nsid - 1] = (DiskInfo *)qemu_mallocz(sizeof(DiskInfo));
    memcpy(&n->disk[nsid - 1]->idtfy_ns, &ns, sizeof(ns));

    if (nvme_create_storage_disk(n->instance, nsid, n->disk[nsid - 1], n)) {
        LOG_ERR("failed to create storage disk for namespace:%d\n", nsid);
        return FAIL;
    }

    return 0;
}

static uint32_t aon_adm_cmd_delete_ns(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    sf->sc = NVME_SC_SUCCESS;
    uint32_t nsid;
    uint64_t ns_bytes;
    DiskInfo *disk;

    if (cmd->opcode != AON_ADM_CMD_DELETE_NAMESPACE || !n->use_aon) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }

    nsid = cmd->nsid;
    if (nsid > n->num_namespaces || nsid == 0) {
        LOG_NORM("%s(): bad nsid:%d", __func__, nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }
    if (n->disk[nsid - 1] == NULL) {
        LOG_NORM("%s(): nsid:%d not created", __func__, nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }

    LOG_NORM("%s(): called", __func__);

    disk = n->disk[nsid - 1];

    ns_bytes = disk->idtfy_ns.nsze * (1 << disk->idtfy_ns.lbaf[
        disk->idtfy_ns.flbas & 0xf].lbads);

    clear_bit(nsid, n->nn_vector);
    n->idtfy_ctrl->nn = find_last_bit(n->nn_vector, n->num_namespaces + 2);
    if (n->idtfy_ctrl->nn == n->num_namespaces + 2) {
        /* deleted the last namespace */
        n->idtfy_ctrl->nn = 0;
    }
    n->available_space += ns_bytes;

    nvme_close_storage_disk(disk);

    n->disk[nsid - 1] = NULL;

    return 0;
}

static uint32_t aon_adm_cmd_mod_ns(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    NVMEIdentifyNamespace ns;
    DiskInfo *disk;
    uint32_t len, nsid, lba_idx, block_size;
    uint64_t ns_bytes, current_bytes;

    sf->sc = NVME_SC_SUCCESS;
    if (cmd->opcode != AON_ADM_CMD_MODIFY_NAMESPACE || !n->use_aon) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }

    nsid = cmd->nsid;
    if (nsid > n->num_namespaces || nsid == 0) {
        LOG_NORM("%s(): bad nsid:%d", __func__, nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }
    if (n->disk[nsid - 1] == NULL) {
        LOG_NORM("%s(): nsid:%d not created", __func__, nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }
    if (cmd->prp1 == 0) {
        LOG_NORM("%s(): NULL prp1:%p", __func__, (uint8_t *)cmd->prp1);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }

    LOG_NORM("%s(): called nsid:%d", __func__, nsid);

    disk = n->disk[nsid - 1];
    len = PAGE_SIZE - (cmd->prp1 % PAGE_SIZE);
    nvme_dma_mem_read((target_phys_addr_t)cmd->prp1, (uint8_t *)&ns, len);
    if (len != sizeof(ns)) {
        if (cmd->prp2 == 0) {
            LOG_NORM("%s(): prp2 is NULL", __func__);
            sf->sc = NVME_SC_INVALID_FIELD;
            return FAIL;
        }
        nvme_dma_mem_write(cmd->prp2,
            (uint8_t *) ((uint8_t *) ((uint8_t *)&ns) + len),
                (sizeof(ns) - len));
    }

    lba_idx = disk->idtfy_ns.flbas & NVME_FLBAS_LBA_MASK;
    block_size = 1 << (disk->idtfy_ns.lbaf[lba_idx].lbads);
    ns_bytes = ns.nsze * block_size;
    current_bytes = disk->idtfy_ns.nsze * block_size;
    if (ns_bytes > current_bytes) {
        if (ns_bytes - current_bytes > n->available_space) {
            LOG_NORM("%s(): bad ns size:%lu", __func__, ns.nsze);
            sf->sc = NVME_AON_INVALID_NAMESPACE_SIZE;
            sf->sct = NVME_SCT_CMD_SPEC_ERR;
            return FAIL;
        }
    }
    if (ns.ncap != ns.nsze) {
        LOG_NORM("%s(): bad ncap:%ld, nsze:%ld", __func__, ns.ncap, ns.nsze);
        sf->sc = NVME_AON_INVALID_NAMESPACE_CAPACITY;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }

    if (disk->idtfy_ns.nsze != ns.nsze) {
        uint64_t size = ns.ncap * block_size;
        if (disk->idtfy_ns.flbas & 0x10) {
            size += ns.ncap * disk->idtfy_ns.lbaf[lba_idx].ms;
        }
        disk->ns_util = qemu_realloc(disk->ns_util,
            (ns_bytes / block_size + 7) / 8);
        if (posix_fallocate(disk->fd, 0, size) != 0) {
            LOG_ERR("Error while modifying size of namespace");
            return FAIL;
        }
        if (ns_bytes > current_bytes) {
            n->available_space -= (ns_bytes - current_bytes);
        } else if (ns_bytes < current_bytes) {
            n->available_space += (current_bytes - ns_bytes);
        }
    }

    disk->idtfy_ns.nsze = ns.nsze;
    disk->idtfy_ns.ncap = ns.ncap;
    disk->idtfy_ns.nsfeat = ns.nsfeat;

    return 0;
}

static uint32_t nvme_ata_security_send(NVMEState *n, NVMECmd *cmd,
    NVMECQE *cqe, uint16_t tl)
{
    uint8_t payload[4096];
    char password[32];
    int len, payload_size, i;
    uint16_t opcode;
    uint64_t prp1 = cmd->prp1;
    uint64_t prp2 = cmd->prp2;
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;

    payload_size = sizeof(payload);
    if (tl < payload_size) {
        LOG_NORM("%s(): bad transfer len:%d need:%d", __func__, tl,
            payload_size);
        sf->sc = NVME_SC_CMD_SEQ_ERROR;
        return FAIL;
    }

    len = min(PAGE_SIZE - (prp1 % PAGE_SIZE), payload_size);
    nvme_dma_mem_read(prp1, (uint8_t *)payload, len);
    if (len != payload_size) {
        nvme_dma_mem_read(prp2, ((uint8_t *)payload) + len, payload_size -
            len);
    }

    opcode = payload[0] | (payload[1] << 8);
    switch (opcode) {
    case ATA_SEC_SET_PASSWORD:
        if (n->s != B) {
            LOG_NORM("%s(): invalid security state '%c' for set password",
                __func__, n->s);
            sf->sc = NVME_SC_CMD_SEQ_ERROR;
            if (n->s == E1) {
                n->s = H;
            }
            return FAIL;
        }
        LOG_NORM("received password:%s", &payload[2]);
        memcpy(n->password, &payload[2], sizeof(n->password));
        n->s = H;
        break;
    case ATA_SEC_UNLOCK:
        if (n->s != D) {
            LOG_NORM("%s(): invalid security state '%c' for unlock", __func__,
                n->s);
            sf->sc = NVME_SC_CMD_SEQ_ERROR;
            if (n->s == E1) {
                n->s = H;
            }
            return FAIL;
        }
        memcpy(password, &payload[2], sizeof(password));
        if (memcmp(password, n->password, sizeof(password) != 0)) {
            LOG_NORM("%s(): password mismatch %s %s", __func__, password,
                n->password);
            sf->sc = NVME_SC_CMD_SEQ_ERROR;
            if (++n->password_retry >= NVME_MAX_PASSWORD_RETRY) {
                n->s = G;
            }
            return FAIL;
        }
        n->s = H;
        break;
    case ATA_SEC_ERASE_PREP:
        if (n->s != H) {
            LOG_NORM("%s(): invalid security state '%c' for erase prep",
                __func__, n->s);
            sf->sc = NVME_SC_CMD_SEQ_ERROR;
            if (n->s == E1) {
                n->s = H;
            }
            return FAIL;
        }
        n->s = E1;
        break;
    case ATA_SEC_ERASE_UNIT:
        if (n->s != E1) {
            LOG_NORM("%s(): invalid security state '%c' for erase unit",
                __func__, n->s);
            sf->sc = NVME_SC_CMD_SEQ_ERROR;
            return FAIL;
        }
        n->s = H;
        memcpy(password, &payload[3], sizeof(password));
        if (memcmp(password, n->password, sizeof(password) != 0)) {
            LOG_NORM("%s(): password mismatch", __func__);
            sf->sc = NVME_SC_CMD_SEQ_ERROR;
            return FAIL;
        }
        n->s = B;
        for (i = 0; i < n->num_namespaces; i++) {
            DiskInfo *disk = n->disk[i];
            if (disk == NULL) {
                continue;
            }
            disk->thresh_warn_issued = 0;
            memset(disk->ns_util, 0x0, (disk->idtfy_ns.nsze + 7) / 8);
            if (disk->meta_mapping_addr != NULL) {
                memset(disk->meta_mapping_addr, 0xff, disk->meta_mapping_size);
            }
        }
        break;
    case ATA_SEC_FREEZE_LOCK:
        if (n->s != H) {
            LOG_NORM("%s(): invalid security state '%c' for freeze lock",
                __func__, n->s);
            sf->sc = NVME_SC_CMD_SEQ_ERROR;
            if (n->s == E1) {
                n->s = H;
            }
            return FAIL;
        }
        n->s = E1;
        break;
    case ATA_SEC_DISABLE_PASSWORD:
        if (n->s != H) {
            LOG_NORM("%s(): invalid security state '%c' for disable password",
                __func__, n->s);
            sf->sc = NVME_SC_CMD_SEQ_ERROR;
            if (n->s == E1) {
                n->s = H;
            }
            return FAIL;
        }
        memcpy(&payload[2], password, sizeof(password));
        if (memcmp(password, n->password, sizeof(password) != 0)) {
            LOG_NORM("%s(): password mismatch", __func__);
            sf->sc = NVME_SC_CMD_SEQ_ERROR;
            return FAIL;
        }
        n->s = B;
        break;
    default:
        LOG_NORM("%s(): invalid ata security opcode:%x", __func__, opcode);
        sf->sc = NVME_SC_INVALID_FIELD;
        if (n->s == E1) {
            n->s = H;
        }
        return FAIL;
    }
    return 0;
}

static uint32_t aon_cmd_sec_send(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    sf->sc = NVME_SC_SUCCESS;
    uint8_t secp = cmd->cdw10 >> 24;
    uint16_t spsp = (cmd->cdw10 >> 8) & 0xffff;
    uint16_t tl = cmd->cdw11 >> 16;

    if (cmd->opcode != NVME_ADM_CMD_SECURITY_SEND) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }

    LOG_NORM("%s(): called", __func__);
    switch (secp) {
    case 0xef:
        switch (spsp) {
        case 0x0000:
            return nvme_ata_security_send(n, cmd, cqe, tl);
        default:
            LOG_NORM("%s(): secp:%d bad security protocol specific:%d",
                __func__, secp, spsp);
            sf->sc = NVME_SC_INVALID_FIELD;
            return FAIL;
        }
        break;
    default:
        LOG_NORM("%s(): bad security protocol secp:%d", __func__, secp);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }
    return 0;
}

typedef struct supported_sps_format {
    uint8_t rsvd[6];
    uint16_t sp_len;
    uint8_t supported_sp[];
} supported_sps_format;

static uint32_t nvme_supported_security_protocols(NVMEState *n, NVMECmd *cmd,
    NVMECQE *cqe, uint16_t tl)
{
    int len, payload_size;
    uint64_t prp1 = cmd->prp1;
    uint64_t prp2 = cmd->prp2;
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    static supported_sps_format sps = {
        .sp_len = 2,
        .supported_sp = {
            0x00,
            0xef,
        },
    };

    payload_size = sizeof(sps) + 2;
    if (tl < payload_size) {
        LOG_NORM("%s(): bad transfer len:%d need:%d", __func__, tl,
            payload_size);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }

    len = min(PAGE_SIZE - (prp1 % PAGE_SIZE), payload_size);
    nvme_dma_mem_write(prp1, (uint8_t *) &sps, len);
    if (len != payload_size) {
        nvme_dma_mem_write(prp2, ((uint8_t *)&sps) + len, payload_size - len);
    }
    return 0;
}

static uint32_t aon_cmd_sec_recv(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    sf->sc = NVME_SC_SUCCESS;
    uint8_t secp = cmd->cdw10 >> 24;
    uint16_t spsp = (cmd->cdw10 >> 8) & 0xffff;
    uint16_t tl = cmd->cdw11 >> 16;

    if (cmd->opcode != NVME_ADM_CMD_SECURITY_RECV) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }

    LOG_NORM("%s(): called", __func__);
    switch (secp) {
    case 0x0:
        switch (spsp) {
        case 0x0000:
            return nvme_supported_security_protocols(n, cmd, cqe, tl);
        default:
            LOG_NORM("%s(): secp:%d bad security protocol specific:%d",
                __func__, secp, spsp);
            sf->sc = NVME_SC_INVALID_FIELD;
            return FAIL;
        }
        break;
    default:
        LOG_NORM("%s(): bad security protocol secp:%d", __func__, secp);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }
    return 0;
}

static uint32_t aon_adm_cmd_create_pd(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    uint32_t pdid = cmd->cdw14;
    sf->sc = NVME_SC_SUCCESS;

    if (cmd->opcode != AON_ADM_CMD_CREATE_PD || !n->use_aon) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (pdid == 0 || pdid > n->aon_ctrl_vs->mnpd) {
        LOG_NORM("%s(): Invalid pdid %d", __func__, pdid);
        sf->sc = NVME_AON_INVALID_PROTECTION_DOMAIN_IDENTIFIER;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (n->protection_domains[pdid - 1] != NULL) {
        LOG_NORM("%s(): pdid %d already in use", __func__, pdid);
        sf->sc = NVME_AON_INVALID_PROTECTION_DOMAIN_IDENTIFIER;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }

    LOG_NORM("%s(): called", __func__);
    n->protection_domains[pdid - 1] = qemu_mallocz(sizeof(NVMEAonPD));

    return 0;
}

static uint32_t aon_adm_cmd_delete_pd(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    uint32_t pdid = cmd->cdw14;
    sf->sc = NVME_SC_SUCCESS;

    if (cmd->opcode != AON_ADM_CMD_DELETE_PD || !n->use_aon) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (pdid == 0 || pdid > n->aon_ctrl_vs->mnpd) {
        LOG_NORM("%s(): Invalid pdid %d", __func__, pdid);
        sf->sc = NVME_AON_INVALID_PROTECTION_DOMAIN_IDENTIFIER;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (n->protection_domains[pdid - 1] == NULL) {
        LOG_NORM("%s(): pdid %d not allocated", __func__, pdid);
        sf->sc = NVME_AON_INVALID_PROTECTION_DOMAIN_IDENTIFIER;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (n->protection_domains[pdid - 1]->usage_count != 0) {
        LOG_NORM("%s(): pdid %d in use, count:%d", __func__, pdid,
            n->protection_domains[pdid - 1]->usage_count);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }

    LOG_NORM("%s(): called", __func__);

    qemu_free(n->protection_domains[pdid - 1]);
    n->protection_domains[pdid - 1] = NULL;

    return 0;
}

static uint32_t aon_adm_cmd_create_stag(NVMEState *n, NVMECmd *cmd,
    NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    NVMEAonAdmCmdCreateSTag *c = (NVMEAonAdmCmdCreateSTag *)cmd;
    NVMEAonStag *stag;
    sf->sc = NVME_SC_SUCCESS;

    if (cmd->opcode != AON_ADM_CMD_CREATE_STAG || !n->use_aon) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (c->pdid == 0 || c->pdid > n->aon_ctrl_vs->mnpd) {
        LOG_NORM("%s(): Invalid pdid %d", __func__, c->pdid);
        sf->sc = NVME_AON_INVALID_PROTECTION_DOMAIN_IDENTIFIER;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (n->protection_domains[c->pdid - 1] == NULL) {
        LOG_NORM("%s(): pdid %d not allocated", __func__, c->pdid);
        sf->sc = NVME_AON_INVALID_PROTECTION_DOMAIN_IDENTIFIER;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (c->stag == 0 || c->stag > n->aon_ctrl_vs->mnhr) {
        LOG_NORM("%s(): Invalid stag %d", __func__, c->pdid);
        sf->sc = NVME_AON_INVALID_STAG;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (n->stags[c->stag - 1] != NULL && !c->rstag) {
        LOG_NORM("%s(): Already allocated stag %d", __func__, c->stag);
        sf->sc = NVME_AON_INVALID_STAG;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (n->stags[c->stag - 1] == NULL && c->rstag) {
        LOG_NORM("%s(): Unallocated stag %d for re-register", __func__,
            c->stag);
        sf->sc = NVME_AON_INVALID_STAG;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (c->smps > n->aon_ctrl_vs->smpsmax ||
        c->smps < n->aon_ctrl_vs->smpsmin) {
        LOG_NORM("%s(): invalid smps %d", __func__, c->smps);
        sf->sc = NVME_SC_INVALID_FIELD;
        return FAIL;
    }

    LOG_NORM("%s(): called", __func__);

    if (!c->rstag) {
        n->stags[c->stag - 1] = qemu_mallocz(sizeof(NVMEAonStag));
        n->protection_domains[c->pdid - 1]->usage_count++;
    }

    stag = n->stags[c->stag - 1];
    stag->pdid = c->pdid;
    stag->smps = 1ULL << (c->smps + 12);
    stag->prp = c->prp1;
    stag->nmp = c->nmp;

    return 0;
}

static uint32_t aon_adm_cmd_delete_stag(NVMEState *n, NVMECmd *cmd,
    NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    uint32_t stag = cmd->cdw10;
    uint32_t pdid = cmd->cdw14;
    sf->sc = NVME_SC_SUCCESS;

    if (cmd->opcode != AON_ADM_CMD_DELETE_STAG || !n->use_aon) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (pdid == 0 || pdid > n->aon_ctrl_vs->mnpd) {
        LOG_NORM("%s(): Invalid pdid %d", __func__, pdid);
        sf->sc = NVME_AON_INVALID_PROTECTION_DOMAIN_IDENTIFIER;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (n->protection_domains[pdid - 1] == NULL) {
        LOG_NORM("%s(): pdid %d not allocated", __func__, pdid);
        sf->sc = NVME_AON_INVALID_PROTECTION_DOMAIN_IDENTIFIER;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (stag == 0 || stag > n->aon_ctrl_vs->mnhr) {
        LOG_NORM("%s(): Invalid stag %d", __func__, stag);
        sf->sc = NVME_AON_INVALID_STAG;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (n->stags[stag - 1] == NULL) {
        LOG_NORM("%s(): stag %d not allocated", __func__, stag);
        sf->sc = NVME_AON_INVALID_STAG;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (n->stags[stag - 1]->pdid != pdid) {
        LOG_NORM("%s(): pdid %d mismatch with stag %d, expected:%d", __func__,
            pdid, stag, n->stags[stag - 1]->pdid);
        sf->sc = NVME_AON_INVALID_PROTECTION_DOMAIN_IDENTIFIER;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }

    LOG_NORM("%s(): called", __func__);

    qemu_free(n->stags[stag - 1]);
    n->stags[stag - 1] = NULL;
    n->protection_domains[pdid - 1]->usage_count--;

    return 0;
}

static uint32_t aon_adm_cmd_create_nstag(NVMEState *n, NVMECmd *cmd,
    NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    uint32_t at = cmd->cdw10;
    uint32_t ntag = cmd->cdw11;
    uint32_t pdid = cmd->cdw14;
    sf->sc = NVME_SC_SUCCESS;

    if (cmd->opcode != AON_ADM_CMD_CREATE_NAMESPACE_TAG || !n->use_aon) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (cmd->nsid == 0 || cmd->nsid > n->num_namespaces) {
        LOG_NORM("%s(): bad nsid:%d, must be between 1 and %d", __func__,
            cmd->nsid, n->num_namespaces);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }
    if (n->disk[cmd->nsid - 1] == NULL) {
        LOG_NORM("%s(): unallocated nsid %d", __func__, cmd->nsid);
        sf->sc = NVME_SC_INVALID_NAMESPACE;
        return FAIL;
    }
    if (pdid == 0 || pdid > n->aon_ctrl_vs->mnpd) {
        LOG_NORM("%s(): Invalid pdid %d", __func__, pdid);
        sf->sc = NVME_AON_INVALID_PROTECTION_DOMAIN_IDENTIFIER;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (n->protection_domains[pdid - 1] == NULL) {
        LOG_NORM("%s(): pdid %d not allocated", __func__, pdid);
        sf->sc = NVME_AON_INVALID_PROTECTION_DOMAIN_IDENTIFIER;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (ntag == 0 || ntag > n->aon_ctrl_vs->mnon) {
        LOG_NORM("%s(): invalid ntag %d", __func__, ntag);
        sf->sc = NVME_AON_INVALID_NAMESPACE_TAG;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (n->nstags[ntag - 1] != NULL) {
        LOG_NORM("%s(): already allocated ntag %d", __func__, ntag);
        sf->sc = NVME_AON_INVALID_NAMESPACE_TAG;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }

    LOG_NORM("%s(): called", __func__);

    n->nstags[ntag - 1] = qemu_mallocz(sizeof(NVMEAonNStag));
    n->nstags[ntag - 1]->pdid = pdid;
    n->nstags[ntag - 1]->at = at;
    n->nstags[ntag - 1]->nsid = cmd->nsid;

    n->protection_domains[pdid - 1]->usage_count++;

    return 0;
}

static uint32_t aon_adm_cmd_delete_nstag(NVMEState *n, NVMECmd *cmd,
    NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    uint32_t ntag = cmd->cdw11;
    uint32_t pdid;
    sf->sc = NVME_SC_SUCCESS;

    if (cmd->opcode != AON_ADM_CMD_DELETE_NAMESPACE_TAG || !n->use_aon) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }
    if (ntag == 0 || ntag > n->aon_ctrl_vs->mnon) {
        LOG_NORM("%s(): invalid ntag %d", __func__, ntag);
        sf->sc = NVME_AON_INVALID_NAMESPACE_TAG;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    if (n->nstags[ntag - 1] == NULL) {
        LOG_NORM("%s(): unallocated ntag %d", __func__, ntag);
        sf->sc = NVME_AON_INVALID_NAMESPACE_TAG;
        sf->sct = NVME_SCT_CMD_SPEC_ERR;
        return FAIL;
    }
    LOG_NORM("%s(): called", __func__);

    pdid = n->nstags[ntag - 1]->pdid;
    qemu_free(n->nstags[ntag - 1]);
    n->nstags[ntag - 1] = NULL;
    n->protection_domains[pdid - 1]->usage_count--;

    return 0;
}

static uint32_t aon_adm_cmd_inject_err(NVMEState *n, NVMECmd *cmd, NVMECQE *cqe)
{
    NVMEStatusField *sf = (NVMEStatusField *)&cqe->status;
    sf->sc = NVME_SC_SUCCESS;
    uint32_t cdw10 = cmd->cdw10;
    uint32_t cdw11 = cmd->cdw11;
    uint32_t cdw12 = cmd->cdw12;

    NVMEIoError *me, *next;

    if (cmd->opcode != AON_ADM_CMD_INJECT_ERROR || !n->use_aon) {
        LOG_NORM("%s(): Invalid opcode %x", __func__, cmd->opcode);
        sf->sc = NVME_SC_INVALID_OPCODE;
        return FAIL;
    }

    switch (cdw10 & 0x7) {
    case NVME_ERR_CLEAR:
        n->temperature = NVME_TEMPERATURE;
        n->percentage_used = 0;
        n->injected_available_spare = 0;
        if (n->timeout_error != NULL) {
            qemu_free(n->timeout_error);
            n->timeout_error = NULL;
        }
        QTAILQ_FOREACH_SAFE(me, &n->media_err_list, entry, next) {
            QTAILQ_REMOVE(&n->media_err_list, me, entry);
            qemu_free(me);
            --n->injected_media_errors;
        }
        break;
    case NVME_ERR_SPARE:
        n->injected_available_spare = (cdw10 & 0x7f8) >> 3;
        if (n->injected_available_spare > 100) {
            n->injected_available_spare = 100;
        }
        break;
    case NVME_ERR_TEMP:
        n->temperature = (cdw10 & 0x1fff8) >> 3;
        if (n->temperature >= n->feature.temperature_threshold &&
                !n->temp_warn_issued) {
            LOG_NORM("Device:%d triggering temperature threshold event",
                n->instance);
            n->temp_warn_issued = 1;
            enqueue_async_event(n, event_type_smart,
                event_info_smart_temp_thresh, NVME_LOG_SMART_INFORMATION);
        }
        break;
    case NVME_ERR_WEAR:
        n->percentage_used = (cdw10 & 0x7f8) >> 3;
        break;
    case NVME_ERR_MEDIA:
        if (n->injected_media_errors < 8) {
            ++n->injected_media_errors;
            me = qemu_malloc(sizeof(*me));
            me->slba = cdw11;
            me->elba = cdw12;
            me->io_error = (cdw10 & 0x78) >> 3;
            QTAILQ_INSERT_TAIL(&n->media_err_list, me, entry);
        }
        break;
    case NVME_ERR_TIME_OUT:
        if (n->timeout_error == NULL) {
            me = qemu_malloc(sizeof(*me));
            n->timeout_error = me;
        } else {
            me = n->timeout_error;
        }
        me->slba = cdw11;
        me->elba = cdw12;
        me->io_error = (cdw10 & 0x18) >> 3;
        break;
    default:
        sf->sc = NVME_SC_INVALID_FIELD;
        break;
    }
    return 0;
}

