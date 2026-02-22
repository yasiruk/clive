#include "signaling_client.hpp"
#include <iostream>
#include <cstring>

using json = nlohmann::json;

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

void SignalingClient::send_message(const std::string& type, const json& data) {
    if (!ws_conn_) return;

    json j;
    j["type"] = type;
    if (!data.is_null()) {
        j["data"] = data;
    } else {
        j["data"] = nullptr;
    }

    std::string text = j.dump();
    soup_websocket_connection_send_text(ws_conn_, text.c_str());
}

void SignalingClient::send_offer(const std::string& sdp) {
    json data = {{"sdp", sdp}};
    send_message("offer", {{"sdp", sdp}});
}

void SignalingClient::send_answer(const std::string& sdp) {
    send_message("answer", {{"sdp", sdp}});
}

void SignalingClient::send_candidate(const std::string& candidate, int sdp_mline_index) {
    json data = {
        {"candidate", candidate},
        {"sdpMLineIndex", sdp_mline_index}
    };
    send_message("candidate", data);
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
    
    try {
        json j = json::parse(data, data + len);
        std::string msg_type = j.value("type", "");

        if (msg_type == "peer-ready") {
            if (self->callbacks_.on_peer_ready) self->callbacks_.on_peer_ready();
        } else if (msg_type == "offer") {
            if (j.contains("data")) {
                std::string sdp = j["data"].value("sdp", "");
                if (self->callbacks_.on_offer) self->callbacks_.on_offer(sdp);
            }
        } else if (msg_type == "answer") {
            if (j.contains("data")) {
                std::string sdp = j["data"].value("sdp", "");
                if (self->callbacks_.on_answer) self->callbacks_.on_answer(sdp);
            }
        } else if (msg_type == "candidate") {
            if (j.contains("data")) {
                std::string candidate = j["data"].value("candidate", "");
                int mline_index = j["data"].value("sdpMLineIndex", 0);
                if (self->callbacks_.on_candidate) self->callbacks_.on_candidate(candidate, mline_index);
            }
        }
    } catch (json::parse_error& e) {
        if (self->callbacks_.on_error) {
            self->callbacks_.on_error(std::string("Failed to parse JSON: ") + e.what());
        }
    }
}

void SignalingClient::on_ws_closed(SoupWebsocketConnection* conn, gpointer user_data) {
    SignalingClient* self = static_cast<SignalingClient*>(user_data);
    self->ws_conn_ = nullptr;
    if (self->callbacks_.on_closed) {
        self->callbacks_.on_closed();
    }
}
