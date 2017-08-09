#define TRACE_MODULE _emm_sm

#include "core_debug.h"

#include "nas_message.h"

#include "mme_event.h"
#include "emm_handler.h"
#include "emm_build.h"
#include "mme_s6a_handler.h"
#include "mme_kdf.h"
#include "s1ap_handler.h"

void emm_state_initial(fsm_t *s, event_t *e)
{
    d_assert(s, return, "Null param");

    mme_sm_trace(3, e);

    FSM_TRAN(s, &emm_state_operational);
}

void emm_state_final(fsm_t *s, event_t *e)
{
    d_assert(s, return, "Null param");

    mme_sm_trace(3, e);
}

void emm_state_operational(fsm_t *s, event_t *e)
{
    d_assert(s, return, "Null param");
    d_assert(e, return, "Null param");

    mme_sm_trace(3, e);

    switch (event_get(e))
    {
        case FSM_ENTRY_SIG:
        {
            break;
        }
        case FSM_EXIT_SIG:
        {
            break;
        }
        case MME_EVT_EMM_BEARER_FROM_S11:
        {
            index_t index = event_get_param1(e);
            mme_bearer_t *bearer = NULL;

            d_assert(index, break, "Null param");
            bearer = mme_bearer_find(index);
            d_assert(bearer, break, "No Bearer context");

            switch(event_get_param2(e))
            {
                case GTP_CREATE_SESSION_RESPONSE_TYPE:
                    emm_handle_create_session_response(bearer);
                    break;
                case GTP_DELETE_SESSION_RESPONSE_TYPE:
                    emm_handle_delete_session_response(bearer);
                    break;
                case GTP_DOWNLINK_DATA_NOTIFICATION_TYPE:
                {
                    mme_ue_t *mme_ue = NULL;
                    gtp_xact_t *xact = (gtp_xact_t *)event_get_param3(e);

                    emm_handle_downlink_data_notification(xact, bearer);
                    mme_ue = bearer->mme_ue;
                    d_assert(mme_ue, break, "Null param");

                    s1ap_handle_paging(mme_ue);
                    /* Start T3413 */
                    tm_start(mme_ue->t3413);
                    break;
                }
            }

            break;
        }
        case MME_EVT_EMM_UE_FROM_S6A:
        case MME_EVT_EMM_UE_FROM_S11:
        {
            index_t index = event_get_param1(e);
            mme_ue_t *mme_ue = NULL;

            d_assert(index, return, "Null param");
            mme_ue = mme_ue_find(index);
            d_assert(mme_ue, return, "Null param");

            switch(event_get_param2(e))
            {
                case S6A_CMD_AUTHENTICATION_INFORMATION:
                {
                    c_uint32_t result_code = event_get_param3(e);
                    emm_handle_s6a_aia(mme_ue, result_code);
                    break;
                }
                case S6A_CMD_UPDATE_LOCATION:
                {
                    mme_sess_t *sess = mme_sess_first(mme_ue);
                    while(sess)
                    {
                        mme_bearer_t *bearer = mme_bearer_first(sess);
                        while(bearer)
                        {
                            event_t e;
                            event_set(&e, MME_EVT_ESM_BEARER_FROM_S6A);
                            event_set_param1(&e, (c_uintptr_t)bearer->index);
                            event_set_param2(&e, 
                                    (c_uintptr_t)S6A_CMD_UPDATE_LOCATION);
                            mme_event_send(&e);

                            bearer = mme_bearer_next(bearer);
                        }

                        sess = mme_sess_next(sess);
                    }
                    break;
                }
                case GTP_MODIFY_BEARER_RESPONSE_TYPE:
                {
                    d_trace(3, "[GTP] Modify Bearer Response : "
                            "MME <-- SGW\n");
                    break;
                }
            }
            break;
        }
        case MME_EVT_EMM_UE_MSG:
        {
            index_t index = event_get_param1(e);
            mme_ue_t *mme_ue = NULL;
            nas_message_t *message = NULL;

            d_assert(index, return, "Null param");
            mme_ue = mme_ue_find(index);
            d_assert(mme_ue, return, "Null param");

            message = (nas_message_t *)event_get_param4(e);
            d_assert(message, break, "Null param");

            /* Save Last Received NAS-EMM message */
            memcpy(&mme_ue->last_emm_message, message, sizeof(nas_message_t));

            if (message->emm.h.security_header_type
                    == NAS_SECURITY_HEADER_FOR_SERVICE_REQUEST_MESSAGE)
            {
                mme_ue_paged(mme_ue);
                emm_handle_service_request(
                        mme_ue, &message->emm.service_request);
                break;
            }

            switch(message->emm.h.message_type)
            {
                case NAS_ATTACH_REQUEST:
                {
                    mme_ue_paged(mme_ue);
                    emm_handle_attach_request(
                            mme_ue, &message->emm.attach_request);
                    break;
                }
                case NAS_IDENTITY_RESPONSE:
                {
                    emm_handle_identity_response(mme_ue,
                            &message->emm.identity_response);
                    break;
                }
                case NAS_AUTHENTICATION_RESPONSE:
                {
                    emm_handle_authentication_response(
                            mme_ue, &message->emm.authentication_response);
                    break;
                }
                case NAS_SECURITY_MODE_COMPLETE:
                {
                    d_trace(3, "[NAS] Security mode complete : "
                            "UE[%s] --> EMM\n", mme_ue->imsi_bcd);

                    /* Update Kenb */
                    if (SECURITY_CONTEXT_IS_VALID(mme_ue))
                        mme_kdf_enb(mme_ue->kasme, mme_ue->ul_count.i32, 
                                mme_ue->kenb);

                    mme_s6a_send_ulr(mme_ue);
                    break;
                }
                case NAS_ATTACH_COMPLETE:
                {
                    d_trace(3, "[NAS] Attach complete : UE[%s] --> EMM\n",
                            mme_ue->imsi_bcd);
                    emm_handle_attach_complete(
                            mme_ue, &message->emm.attach_complete);
                    break;
                }
                case NAS_EMM_STATUS:
                {
                    emm_handle_emm_status(mme_ue, &message->emm.emm_status);
                    break;
                }
                case NAS_DETACH_REQUEST:
                {
                    emm_handle_detach_request(
                            mme_ue, &message->emm.detach_request_from_ue);
                    break;
                }
                case NAS_TRACKING_AREA_UPDATE_REQUEST:
                {
                    mme_ue_paged(mme_ue);
                    break;
                }
                default:
                {
                    d_warn("Not implemented(type:%d)", 
                            message->emm.h.message_type);
                    break;
                }
            }
            break;
        }
        case MME_EVT_EMM_UE_T3:
        {
            index_t index = event_get_param1(e);
            mme_ue_t *mme_ue = NULL;

            d_assert(index, return, "Null param");
            mme_ue = mme_ue_find(index);
            d_assert(mme_ue, return, "Null param");
            break;
        }
        case MME_EVT_EMM_UE_T3413:
        {
            index_t index = event_get_param1(e);
            mme_ue_t *mme_ue = NULL;

            d_assert(index, return, "Null param");
            mme_ue = mme_ue_find(index);
            d_assert(mme_ue, return, "Null param");

            if (mme_ue->max_paging_retry >= MAX_NUM_OF_PAGING)
            {
                /* Paging failed */
                d_warn("Paging to UE(%s) failed. Stop paging",
                        mme_ue->imsi_bcd);
                if (mme_ue->last_paging_msg)
                {
                    pkbuf_free(mme_ue->last_paging_msg);
                    mme_ue->last_paging_msg = NULL;
                }
                break;
            }

            mme_ue->max_paging_retry++;
            s1ap_handle_paging(mme_ue);
            /* Start T3413 */
            tm_start(mme_ue->t3413);

            break;
        }

        default:
        {
            d_error("Unknown event %s", mme_event_get_name(e));
            break;
        }
    }
}

void emm_state_exception(fsm_t *s, event_t *e)
{
    d_assert(s, return, "Null param");
    d_assert(e, return, "Null param");

    mme_sm_trace(3, e);

    switch (event_get(e))
    {
        case FSM_ENTRY_SIG:
        {
            break;
        }
        case FSM_EXIT_SIG:
        {
            break;
        }
        default:
        {
            d_error("Unknown event %s", mme_event_get_name(e));
            break;
        }
    }
}
