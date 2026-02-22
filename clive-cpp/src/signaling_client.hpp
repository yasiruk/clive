#pragma once

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <string>
#include <functional>
#include <memory>

class SignalingClient {
public:
    // Callback types for various events
    using OnConnectCallback = std::function<void()>;
    using OnErrorCallback = std::function<void(const std::string&)>;
    using OnPeerReadyCallback = std::function<void()>;
    using OnOfferCallback = std::function<void(const std::string&)>;
    using OnAnswerCallback = std::function<void(const std::string&)>;
    using OnCandidateCallback = std::function<void(const std::string&, int)>;
    using OnClosedCallback = std::function<void()>;

    struct Callbacks {
        OnConnectCallback on_connect;
        OnErrorCallback on_error;
        OnPeerReadyCallback on_peer_ready;
        OnOfferCallback on_offer;
        OnAnswerCallback on_answer;
        OnCandidateCallback on_candidate;
        OnClosedCallback on_closed;
    };

    SignalingClient(Callbacks callbacks);
    ~SignalingClient();

    void connect(const std::string& url);
    void close();
    
    // Sending messages
    void send_offer(const std::string& sdp);
    void send_answer(const std::string& sdp);
    void send_candidate(const std::string& candidate, int sdp_mline_index);

private:
    void send_message(const std::string& type, JsonNode* data_node);
    
    // Internal static callbacks for libsoup/glib
    static void on_connection_created(GObject* source, GAsyncResult* res, gpointer user_data);
    static void on_ws_message(SoupWebsocketConnection* conn, gint type, GBytes* message, gpointer user_data);
    static void on_ws_closed(SoupWebsocketConnection* conn, gpointer user_data);

    SoupSession* session_;
    SoupWebsocketConnection* ws_conn_;
    Callbacks callbacks_;
};
