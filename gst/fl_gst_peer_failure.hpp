#pragma once
// Why a coordinated session failed, carried producer -> consumer (M6 decision 8).
//
// Producer: an error a FrameLink producer element raises itself records a reason
// on the session (invalid input: stream format errors; overload: the pool
// exhausted under error-on-ring-full). Errors caused by the peer going away carry
// none. fl_gst_session_cancel then sends the reason as an ABORT record.
//
// Consumer: before a FrameLink consumer element posts an error, it waits briefly
// for the producer's end of stream. If the producer failed or closed, the error
// becomes one stable message, domain GST_RESOURCE_ERROR, code
// GST_RESOURCE_ERROR_READ, with a "framelink-peer-failure" details structure
// (field "reason": "invalid-input", "overload" or "closed"; field "original":
// the error the element would have posted). A local consumer failure keeps its
// own error. Sessions without an event stream (audio-only) are left unchanged.
#include "framelink_session_transport.h"
#include <gst/gst.h>

// Bounded wait for the producer's ABORT or close once a consumer error is posted.
static constexpr guint FL_GST_PEER_FAILURE_WAIT_MS=100;

static inline guint flGstProducerFailureReason(GstMessage* message){
    if(GST_MESSAGE_TYPE(message)!=GST_MESSAGE_ERROR)return 0;
    GError* error=nullptr;gst_message_parse_error(message,&error,nullptr);guint reason=0;
    if(error&&error->domain==GST_STREAM_ERROR&&error->code==GST_STREAM_ERROR_FORMAT)reason=FL_GST_PEER_INVALID_INPUT;
    else if(error&&error->domain==GST_RESOURCE_ERROR&&error->code==GST_RESOURCE_ERROR_BUSY)reason=FL_GST_PEER_OVERLOAD;
    g_clear_error(&error);return reason;
}
static inline void flGstNoteProducerFailure(FlGstSession* session,GstMessage* message){
    if(session)if(const guint reason=flGstProducerFailureReason(message))fl_gst_session_set_failure_reason(session,reason);
}
static inline const char* flGstPeerReasonName(guint reason){
    return reason==FL_GST_PEER_INVALID_INPUT?"invalid-input":reason==FL_GST_PEER_OVERLOAD?"overload":"closed";
}
// Returns the message to post: the stable peer-failure error (taking ownership
// of message) or message itself.
static inline GstMessage* flGstConsumerPeerFailure(FlGstSession* session,GstMessage* message){
    if(!session||GST_MESSAGE_TYPE(message)!=GST_MESSAGE_ERROR)return message;
    const GstStructure* existing=nullptr;gst_message_parse_error_details(message,&existing);
    if(existing&&gst_structure_has_name(existing,"framelink-peer-failure"))return message;
    guint reason=0;
    if(fl_gst_session_peer_failure(session,FL_GST_PEER_FAILURE_WAIT_MS,&reason)!=FL_GST_SESSION_READY)return message;
    GError* original=nullptr;gchar* debug=nullptr;gst_message_parse_error(message,&original,&debug);
    const char* text=reason==FL_GST_PEER_INVALID_INPUT?"FrameLink producer failed: invalid input":
                     reason==FL_GST_PEER_OVERLOAD?"FrameLink producer failed: overload":"FrameLink peer closed or failed";
    GError* stable=g_error_new_literal(GST_RESOURCE_ERROR,GST_RESOURCE_ERROR_READ,text);
    gchar* detail=g_strdup_printf("%s%s%s",original?original->message:"",debug?"; ":"",debug?debug:"");
    auto* details=gst_structure_new("framelink-peer-failure","reason",G_TYPE_STRING,flGstPeerReasonName(reason),
        "original",G_TYPE_STRING,original?original->message:"",nullptr);
    GstMessage* replaced=gst_message_new_error_with_details(GST_MESSAGE_SRC(message),stable,detail,details);
    gst_message_set_seqnum(replaced,gst_message_get_seqnum(message));
    g_error_free(stable);g_free(detail);g_free(debug);g_clear_error(&original);gst_message_unref(message);
    return replaced;
}
