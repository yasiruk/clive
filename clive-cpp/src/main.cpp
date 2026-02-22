#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>
#include <glib.h>
#include <iostream>
#include <string>
#include <cstring>
#include "signaling_client.hpp"

// State structure to hold application context
struct AppState {
    GMainLoop *loop;
    GstElement *pipeline;
    GstElement *webrtcbin;
    std::unique_ptr<SignalingClient> signaling_client;
    std::string server_url;
    std::string room_id;
    gboolean is_caller;
};

AppState app_state;

// --- WebRTC Callbacks ---

static void on_offer_created(GstPromise *promise, gpointer user_data) {
    GstWebRTCSessionDescription *offer = NULL;
    const GstStructure *reply = gst_promise_get_reply(promise);
    
    gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
    gst_promise_unref(promise);

    if (!offer) {
        g_print("Failed to create offer\n");
        return;
    }

    g_signal_emit_by_name(app_state.webrtcbin, "set-local-description", offer, NULL);
    
    gchar *sdp_text = gst_sdp_message_as_text(offer->sdp);
    app_state.signaling_client->send_offer(sdp_text);
    g_free(sdp_text);
    
    gst_webrtc_session_description_free(offer);
}

static void on_answer_created(GstPromise *promise, gpointer user_data) {
    GstWebRTCSessionDescription *answer = NULL;
    const GstStructure *reply = gst_promise_get_reply(promise);
    
    gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
    gst_promise_unref(promise);

    if (!answer) {
        g_print("Failed to create answer\n");
        return;
    }

    g_signal_emit_by_name(app_state.webrtcbin, "set-local-description", answer, NULL);
    
    gchar *sdp_text = gst_sdp_message_as_text(answer->sdp);
    app_state.signaling_client->send_answer(sdp_text);
    g_free(sdp_text);
    
    gst_webrtc_session_description_free(answer);
}

static void on_negotiation_needed(GstElement *element, gpointer user_data) {
    if (app_state.is_caller) {
        g_print("Negotiation needed. Creating offer...\n");
        GstPromise *promise = gst_promise_new_with_change_func(on_offer_created, NULL, NULL);
        g_signal_emit_by_name(app_state.webrtcbin, "create-offer", NULL, promise);
    }
}

static void on_ice_candidate(GstElement *element, guint mline_index, gchar *candidate, gpointer user_data) {
    g_print("Gathered ICE candidate: %s\n", candidate);
    app_state.signaling_client->send_candidate(candidate, mline_index);
}

static void on_pad_added(GstElement *element, GstPad *pad, gpointer user_data) {
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
        return;

    g_print("Received new pad '%s' from '%s'\n", GST_PAD_NAME(pad), GST_ELEMENT_NAME(element));

    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, NULL);
    
    const GstStructure *str = gst_caps_get_structure(caps, 0);
    const gchar *name = gst_structure_get_name(str);

    GstElement *sink = NULL;
    
    // Check media type (video or audio)
    if (g_str_has_prefix(name, "video")) {
        // Incoming video is RTP VP8 -> depayload -> decode -> display
        sink = gst_parse_bin_from_description(
            "rtpvp8depay ! vp8dec ! videoconvert ! videoscale ! autovideosink", 
            TRUE, NULL);
    } else if (g_str_has_prefix(name, "audio")) {
        // Incoming audio is RTP Opus -> depayload -> decode -> play
        sink = gst_parse_bin_from_description(
            "rtpopusdepay ! opusdec ! audioconvert ! audioresample ! autoaudiosink", 
            TRUE, NULL);
    }

    if (sink) {
        gst_bin_add(GST_BIN(app_state.pipeline), sink);
        gst_element_sync_state_with_parent(sink);
        GstPad *sink_pad = gst_element_get_static_pad(sink, "sink");
        if (gst_pad_link(pad, sink_pad) != GST_PAD_LINK_OK) {
            g_print("Failed to link pad\n");
        }
        gst_object_unref(sink_pad);
    }

    if (caps) gst_caps_unref(caps);
}

// --- Signaling Callbacks ---

void on_signaling_connect() {
    g_print("Connected to signaling server\n");
    if (app_state.is_caller) {
        g_print("Caller mode: Waiting for 'peer-ready' message from signaling server...\n");
    } else {
        g_print("Callee mode: Waiting for 'offer' from remote peer...\n");
    }
    // Start pipeline once connected
    gst_element_set_state(app_state.pipeline, GST_STATE_PLAYING);
}

void on_signaling_error(const std::string& error) {
    g_print("Signaling error: %s\n", error.c_str());
    g_main_loop_quit(app_state.loop);
}

void on_signaling_closed() {
    g_print("Signaling connection closed\n");
    g_main_loop_quit(app_state.loop);
}

void on_peer_ready() {
    if (app_state.is_caller) {
        g_print("Peer ready, initiating negotiation...\n");
        GstPromise *promise = gst_promise_new_with_change_func(on_offer_created, NULL, NULL);
        g_signal_emit_by_name(app_state.webrtcbin, "create-offer", NULL, promise);
    }
}

void on_offer(const std::string& sdp_str) {
    g_print("Setting remote offer...\n");
    GstSDPMessage *sdp;
    gst_sdp_message_new(&sdp);
    gst_sdp_message_parse_buffer((const guint8 *)sdp_str.c_str(), sdp_str.length(), sdp);
    
    GstWebRTCSessionDescription *desc = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp);
    g_signal_emit_by_name(app_state.webrtcbin, "set-remote-description", desc, NULL);
    
    g_print("Creating answer...\n");
    GstPromise *promise = gst_promise_new_with_change_func(on_answer_created, NULL, NULL);
    g_signal_emit_by_name(app_state.webrtcbin, "create-answer", NULL, promise);
    
    gst_webrtc_session_description_free(desc);
}

void on_answer(const std::string& sdp_str) {
    g_print("Setting remote answer...\n");
    GstSDPMessage *sdp;
    gst_sdp_message_new(&sdp);
    gst_sdp_message_parse_buffer((const guint8 *)sdp_str.c_str(), sdp_str.length(), sdp);
    
    GstWebRTCSessionDescription *desc = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp);
    g_signal_emit_by_name(app_state.webrtcbin, "set-remote-description", desc, NULL);
    gst_webrtc_session_description_free(desc);
}

void on_candidate(const std::string& candidate, int mline_index) {
    g_print("Adding remote ICE candidate...\n");
    g_signal_emit_by_name(app_state.webrtcbin, "add-ice-candidate", mline_index, candidate.c_str());
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);

    // Args parsing
    gchar *room = NULL;
    gchar *server = NULL;
    gboolean caller = FALSE;
    
    GOptionEntry entries[] = {
        {"room", 'r', 0, G_OPTION_ARG_STRING, &room, "Room name", "ROOM"},
        {"server", 's', 0, G_OPTION_ARG_STRING, &server, "Signaling server", "HOST:PORT"},
        {"caller", 'c', 0, G_OPTION_ARG_NONE, &caller, "Caller mode", NULL},
        {NULL}
    };

    GOptionContext *context = g_option_context_new("- WebRTC C++ Client");
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_add_group(context, gst_init_get_option_group());
    
    GError *error = NULL;
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_print("Option parsing failed: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    if (!room) room = g_strdup("default-room");
    if (!server) server = g_strdup("localhost:8080");

    app_state.room_id = room;
    app_state.server_url = std::string("ws://") + server + "/ws?room=" + room;
    app_state.is_caller = caller;

    g_print("Room: %s\nServer: %s\nCaller: %s\n", room, server, caller ? "true" : "false");

    // Pipeline Setup
    // Added 'tee' for local self-view and 'is-live=true' for better simulation
    std::string pipeline_str = 
        "webrtcbin name=sendrecv bundle-policy=max-bundle stun-server=stun://stun.l.google.com:19302 "
        "videotestsrc pattern=ball is-live=true ! videoconvert ! tee name=t "
        "t. ! queue ! autovideosink "
        "t. ! queue ! vp8enc deadline=1 ! rtpvp8pay ! sendrecv. "
        "audiotestsrc wave=red-noise is-live=true ! audioconvert ! queue ! opusenc ! rtpopuspay ! sendrecv. ";

    GError *pipeline_error = NULL;
    app_state.pipeline = gst_parse_launch(pipeline_str.c_str(), &pipeline_error);
    if (pipeline_error) {
        g_print("Failed to create pipeline: %s\n", pipeline_error->message);
        g_error_free(pipeline_error);
        return 1;
    }

    app_state.webrtcbin = gst_bin_get_by_name(GST_BIN(app_state.pipeline), "sendrecv");
    
    g_signal_connect(app_state.webrtcbin, "on-negotiation-needed", G_CALLBACK(on_negotiation_needed), NULL);
    g_signal_connect(app_state.webrtcbin, "on-ice-candidate", G_CALLBACK(on_ice_candidate), NULL);
    g_signal_connect(app_state.webrtcbin, "pad-added", G_CALLBACK(on_pad_added), NULL);

    // Initialize Signaling Client
    SignalingClient::Callbacks callbacks;
    callbacks.on_connect = on_signaling_connect;
    callbacks.on_error = on_signaling_error;
    callbacks.on_closed = on_signaling_closed;
    callbacks.on_peer_ready = on_peer_ready;
    callbacks.on_offer = on_offer;
    callbacks.on_answer = on_answer;
    callbacks.on_candidate = on_candidate;

    app_state.signaling_client = std::make_unique<SignalingClient>(callbacks);
    app_state.signaling_client->connect(app_state.server_url);

    app_state.loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(app_state.loop);

    // Cleanup
    gst_element_set_state(app_state.pipeline, GST_STATE_NULL);
    gst_object_unref(app_state.pipeline);
    if (app_state.webrtcbin) gst_object_unref(app_state.webrtcbin);
    
    // Explicitly reset signaling client to trigger close
    app_state.signaling_client.reset();

    g_main_loop_unref(app_state.loop);
    g_free(room);
    g_free(server);

    return 0;
}
