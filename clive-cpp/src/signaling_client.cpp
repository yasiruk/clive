#include "signaling_client.hpp"
#include <iostream>
#include <cstring>

SignalingClient::SignalingClient(Callbacks callbacks)
    : session_(nullptr), ws_conn_(nullptr), callbacks_(callbacks) {
}

SignalingClient::~SignalingClient() {
    close();
}

void SignalingClient::connect(const std::string& url) {
    session_ = soup_session_new();
    SoupMessage* msg = soup_message_new("GET", url.c_str());
    
    soup_session_websocket_connect_async(session_, msg, NULL, NULL, 0, NULL, 
        on_connection_created, this);
}

void SignalingClient::close() {
    if (ws_conn_) {
        soup_websocket_connection_close(ws_conn_, 1000, "Client closing");
        g_object_unref(ws_conn_);
        ws_conn_ = nullptr;
    }
    if (session_) {
        g_object_unref(session_);
        session_ = nullptr;
    }
}

void SignalingClient::send_message(const std::string& type, JsonNode* data_node) {
    if (!ws_conn_) return;

    JsonBuilder* builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "type");
    json_builder_add_string_value(builder, type.c_str());
    json_builder_set_member_name(builder, "data");
    json_builder_add_value(builder, data_node ? json_node_copy(data_node) : json_node_new(JSON_NODE_NULL));
    json_builder_end_object(builder);

    JsonGenerator* gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar* text = json_generator_to_data(gen, NULL);

    soup_websocket_connection_send_text(ws_conn_, text);

    g_free(text);
    g_object_unref(gen);
    g_object_unref(builder);
}

void SignalingClient::send_offer(const std::string& sdp) {
    JsonBuilder* builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "type");
    json_builder_add_string_value(builder, "offer");
    json_builder_set_member_name(builder, "sdp");
    json_builder_add_string_value(builder, sdp.c_str());
    json_builder_end_object(builder);
    
    JsonNode* data_node = json_builder_get_root(builder);
    send_message("offer", data_node);
    g_object_unref(builder);
}

void SignalingClient::send_answer(const std::string& sdp) {
    JsonBuilder* builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "type");
    json_builder_add_string_value(builder, "answer");
    json_builder_set_member_name(builder, "sdp");
    json_builder_add_string_value(builder, sdp.c_str());
    json_builder_end_object(builder);
    
    JsonNode* data_node = json_builder_get_root(builder);
    send_message("answer", data_node);
    g_object_unref(builder);
}

void SignalingClient::send_candidate(const std::string& candidate, int sdp_mline_index) {
    JsonBuilder* builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "candidate");
    json_builder_add_string_value(builder, candidate.c_str());
    json_builder_set_member_name(builder, "sdpMLineIndex");
    json_builder_add_int_value(builder, sdp_mline_index);
    json_builder_end_object(builder);
    
    JsonNode* root = json_builder_get_root(builder);
    send_message("candidate", root);
    g_object_unref(builder);
}

void SignalingClient::on_connection_created(GObject* source, GAsyncResult* res, gpointer user_data) {
    SignalingClient* self = static_cast<SignalingClient*>(user_data);
    GError* err = nullptr;
    SoupWebsocketConnection* ws = soup_session_websocket_connect_finish(SOUP_SESSION(source), res, &err);
    
    if (err) {
        if (self->callbacks_.on_error) {
            self->callbacks_.on_error(err->message);
        }
        g_error_free(err);
        return;
    }
    
    self->ws_conn_ = ws;
    g_signal_connect(ws, "message", G_CALLBACK(on_ws_message), self);
    g_signal_connect(ws, "closed", G_CALLBACK(on_ws_closed), self);
    
    if (self->callbacks_.on_connect) {
        self->callbacks_.on_connect();
    }
}

void SignalingClient::on_ws_message(SoupWebsocketConnection* conn, gint type, GBytes* message, gpointer user_data) {
    SignalingClient* self = static_cast<SignalingClient*>(user_data);
    gsize len;
    const char* data = (const char*)g_bytes_get_data(message, &len);
    
    JsonParser* parser = json_parser_new();
    if (!json_parser_load_from_data(parser, data, len, nullptr)) {
        if (self->callbacks_.on_error) {
            self->callbacks_.on_error("Failed to parse JSON");
        }
        g_object_unref(parser);
        return;
    }

    JsonNode* root = json_parser_get_root(parser);
    JsonObject* obj = json_node_get_object(root);
    const gchar* msg_type = json_object_get_string_member(obj, "type");
    
    if (g_strcmp0(msg_type, "peer-ready") == 0) {
        if (self->callbacks_.on_peer_ready) self->callbacks_.on_peer_ready();
    } else if (g_strcmp0(msg_type, "offer") == 0) {
        if (json_object_has_member(obj, "data")) {
            JsonObject* data_obj = json_object_get_object_member(obj, "data");
            const gchar* sdp = json_object_get_string_member(data_obj, "sdp");
            if (self->callbacks_.on_offer) self->callbacks_.on_offer(sdp);
        }
    } else if (g_strcmp0(msg_type, "answer") == 0) {
        if (json_object_has_member(obj, "data")) {
            JsonObject* data_obj = json_object_get_object_member(obj, "data");
            const gchar* sdp = json_object_get_string_member(data_obj, "sdp");
            if (self->callbacks_.on_answer) self->callbacks_.on_answer(sdp);
        }
    } else if (g_strcmp0(msg_type, "candidate") == 0) {
        if (json_object_has_member(obj, "data")) {
            JsonObject* data_obj = json_object_get_object_member(obj, "data");
            const gchar* candidate = json_object_get_string_member(data_obj, "candidate");
            gint mline_index = json_object_get_int_member(data_obj, "sdpMLineIndex");
            if (self->callbacks_.on_candidate) self->callbacks_.on_candidate(candidate, mline_index);
        }
    }

    g_object_unref(parser);
}

void SignalingClient::on_ws_closed(SoupWebsocketConnection* conn, gpointer user_data) {
    SignalingClient* self = static_cast<SignalingClient*>(user_data);
    self->ws_conn_ = nullptr;
    if (self->callbacks_.on_closed) {
        self->callbacks_.on_closed();
    }
}
