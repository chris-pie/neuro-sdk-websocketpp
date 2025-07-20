#ifndef INCLUDE_NEURO_WEBSOCKETPP_LIBRARY_HPP
#define INCLUDE_NEURO_WEBSOCKETPP_LIBRARY_HPP

#include <utility>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include "websocketpp/client.hpp"
#include <iostream>
#include <string>
#include <thread>

#include "json.hpp"

namespace NeuroWebsocketpp {
    class Action {
    public:
        // Constructor
        Action(std::string  name, std::string  description, const nlohmann::json& schema)
            : name(std::move(name)), description(std::move(description)), schema(schema) {}

        std::string getName() const {
            return name;
        }

        std::string getDescription() const {
            return description;
        }

        nlohmann::json getSchema() const {
            return schema;
        }

    private:
        std::string name;
        std::string description;
        nlohmann::json schema;
    };


    class NeuroResponse {
    public:
        explicit NeuroResponse(const std::string& jsonStr);
        std::string getCommand() const {
            return command;
        }
        std::string getId() const {
            return id;
        }
        std::string getName() const {
            return name;
        }
        std::string getData() const {
            return data;
        }
    private:
        std::string command;
        std::string id;
        std::string name;
        std::string data;
    };

    inline NeuroResponse::NeuroResponse(const std::string& jsonStr) {
        try {
            if (jsonStr.empty()) {
                id = "";
                name = "";
                data = "";
                command = "";
                return;
            }
            nlohmann::json parsedJson = nlohmann::json::parse(jsonStr);

            if (parsedJson.contains("command") && parsedJson["command"].is_string()) {
                command = parsedJson["command"];
            } else {
                throw std::invalid_argument("JSON is missing the 'command' field or it is not a string.");
            }


            id = parsedJson["data"]["id"];
            name = parsedJson["data"]["name"];
            try {
                data = parsedJson["data"]["data"];
            } //if data is null need to treat is as empty string
            catch (const std::exception&) {
                data = "";
            }


        } catch (const nlohmann::json::parse_error& e) {
            throw std::invalid_argument("Invalid JSON string: " + std::string(e.what()));
        } catch (const std::exception& e) {
            throw std::invalid_argument("Error processing JSON: " + std::string(e.what()));
        }
    }




    // Type definitions
    using websocketpp::connection_hdl;
    using client = websocketpp::client<websocketpp::config::asio_client>;
    typedef websocketpp::config::asio_client::message_type::ptr message_ptr;
    class NeuroGameClient {
    public:
        virtual ~NeuroGameClient() {
            shutting_down = true;
            if (connected) {
                websocketpp::lib::error_code ec;
                ws_client.close(ws_hdl, websocketpp::close::status::going_away, "Shutting down", ec);
                if (ec) {
                    *error << "Error closing WebSocket connection: " << ec.message() << std::endl;
                }
                ws_client.stop();
            }
            condition.notify_all();
            if (connection_thread.joinable()) {
                connection_thread.join();
            }
            if (reconnect_thread.joinable()) {
                reconnect_thread.join();
            }
        }


        NeuroGameClient(const std::string& uri, std::string  game_name, std::ostream* output_stream = &std::cout, std::ostream* error_stream = &std::cerr, int timeout = -1, bool retry_on_fail = true)
            : game_name(std::move(game_name)), lastResponse(""), timeout(timeout), uri(uri), retry_on_fail(retry_on_fail) {
            output = output_stream;
            error = error_stream;
            ws_client.init_asio();
            ws_client.set_open_handler( [this](connection_hdl && PH1) { on_open(std::forward<decltype(PH1)>(PH1)); });
            ws_client.set_message_handler([this](connection_hdl && PH1, message_ptr && PH2) { on_message(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2)); });
            ws_client.set_close_handler([this](connection_hdl && PH1) { on_close(std::forward<decltype(PH1)>(PH1)); });
            ws_client.set_fail_handler([this](connection_hdl && PH1) { on_fail(std::forward<decltype(PH1)>(PH1)); });
            reconnect_thread = std::thread(&NeuroGameClient::_connect, this);
        }

        void reconnect() {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            _connect();
        }

        void _connect() {
            std::unique_lock<std::mutex> lock(reconnectMutex);
            _failed_reconnecting = false;
            if (connection_thread.joinable()) {
                connection_thread.join();
            }
            if (shutting_down) {
              return;
            }
            std::string finalUri = !uri.empty()
            ? uri
            : std::getenv("NEURO_SDK_WS_URL") ? std::getenv("NEURO_SDK_WS_URL") : "";

            connection_thread = std::thread(&NeuroGameClient::connect, this, finalUri);
            if (timeout >= 0) {
                bool success = condition.wait_for(lock, std::chrono::seconds(timeout), [this]() { return connected || connection_failed || _failed_reconnecting || shutting_down; });
                if (!success || connection_failed) {
                    *error << "Failed to connect to server" << std::endl;
                    throw std::runtime_error("Failed to connect to server");
                }
            } else {
                condition.wait(lock, [this]() { return connected || connection_failed  || _failed_reconnecting || shutting_down; });
                if (connection_failed) {
                    *error << "Failed to connect to server" << std::endl;
                    if (!retry_on_fail) {
                        throw std::runtime_error("Failed to connect to server");
                    }
                }
            }
            if (connection_failed || _failed_reconnecting || shutting_down) {
              return;
            }
            while (!messageQueue.empty()) {
                std::string msg = messageQueue.front();
                messageQueue.pop();
                websocketpp::lib::error_code ec;
                ws_client.send(ws_hdl, msg, websocketpp::frame::opcode::text, ec);
                if (ec) {
                    *error << "Error sending stored message after reconnect: " << ec.message() << std::endl;
                    throw std::runtime_error("Error sending stored message after reconnect");
                }
            }
        }
        void sendStartup() {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "startup";
            //Block until connection is established for startup.
            std::unique_lock<std::mutex> lock(reconnectMutex);
            if (!(connected ||  shutting_down)) {
              condition.wait(lock, [this]() { return connected ||  shutting_down; });
            }
            lock.unlock();
            send(payload.dump());
        }

        void sendContext(const std::string& context_message, bool silent) {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "context";
            payload["data"]["message"] = context_message;
            payload["data"]["silent"] = silent;
            send(payload.dump());
        }

        void sendRegisterActions(const std::vector<Action>& actions) {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "actions/register";
            payload["data"]["actions"] = nlohmann::json::array();
            for (const auto& action : actions) {
                nlohmann::json action_json;
                action_json["name"] = action.getName();
                action_json["description"] = action.getDescription();
                action_json["schema"] = action.getSchema();
                payload["data"]["actions"].push_back(action_json);
            }
            send(payload.dump());

        }

        void sendUnregisterActions(const std::vector<std::string>& action_names) {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "actions/unregister";
            nlohmann::json action_names_json;
            for (const auto& action : action_names) {
                action_names_json.push_back(action);
            }
            payload["data"]["action_names"] = action_names_json;
            send(payload.dump());
        }

        void sendForceActions(const std::string& state, const std::string& query, bool ephemeral, const std::vector<std::string>& actions) {
            nlohmann::json payload;
            payload["command"] = "actions/force";
            payload["game"] = game_name;
            payload["data"]["state"] = state;
            payload["data"]["query"] = query;
            payload["data"]["ephemeral_context"] = ephemeral;
            payload["data"]["action_names"] = actions;
            send(payload.dump());
        }

        void sendActionResult(const NeuroResponse& neuroAction, bool success, const std::string& message) {
            nlohmann::json payload;
            payload["command"] = "action/result";
            payload["game"] = game_name;
            payload["data"]["id"] = neuroAction.getId();
            payload["data"]["success"] = success;
            payload["data"]["message"] = message;
            send(payload.dump());
        }

        void forceAction(const std::string& state, const std::string& query, bool ephemeral, const std::vector<std::string>& actions) {
            std::unique_lock<std::mutex> lock(mutex);
            forcedActions = actions;
            waitingForForcedAction = true;
            sendForceActions(state, query, ephemeral, actions);
            if (timeout >= 0)
            {
                bool success = condition.wait_for(lock, std::chrono::seconds(timeout), [this]() { return !waitingForForcedAction || connection_failed || shutting_down; });
                if (!success || connection_failed) {
                    *error << "Error waiting for forced action" << std::endl;
                    throw std::runtime_error("Error waiting for forced action");
                }
            }
            else {
                condition.wait(lock, [this]() { return !waitingForForcedAction || connection_failed || shutting_down; });
                if (connection_failed) {
                    *error << "Error waiting for forced action" << std::endl;
                    throw std::runtime_error("Error waiting for forced action");
                }
            }
            forcedActions.clear();
        }
    //Helper function that registers actions, sends force action request for them, then stores sent actions in disposableActions field
    //so that they can't be unregistered in handleMessage method. If forceUnregister is true, it will unregister them just before exiting
    //as a failsafe, but this shouldn't be relied on - unregister should happen in handleMessage before sending action result.
    void forceDisposableActions(const std::string& state, const std::string& query, bool ephemeral, const std::vector<Action>& actions, bool forceUnregister = false) {
            sendRegisterActions(actions);
            disposableActions = getActionNamesFromActions(actions);
            forceAction(state, query, ephemeral, getActionNamesFromActions(actions));
            if (forceUnregister) {
                sendUnregisterActions(disposableActions);
            }

        }

    static std::vector<std::string> getActionNamesFromActions(const std::vector<Action>& actions) {
        std::vector<std::string> actionNames;
        for (const auto& action : actions) {
            actionNames.push_back(action.getName());
        }
        return actionNames;
    }



    protected:
        void on_open(connection_hdl hdl) {
            *output << "Connection established!" << std::endl;
            ws_hdl = std::move(hdl);
            connected = true;
            condition.notify_all();


        }


        //Override this method to handle actions.
        //To prevent race conditions, only accept disposable actions when waitingForForcedAction is true
        //When valid action arrives, set waitingForForcedAction to false to continue execution
        //Remember to unregister actions and send action result to Neuro ASAP
        virtual void handleMessage(NeuroResponse const& response) = 0;

        virtual void on_message(const connection_hdl&, const client::message_ptr& msg) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto message = msg->get_payload();
                auto JsonMessage = nlohmann::json::parse(message);
                if (JsonMessage["command"] == "actions/reregister_all")
                    return;
                NeuroResponse const response = NeuroResponse(message);
                handleMessage(response);
                lastResponse = response;
            }

            condition.notify_all();
        }
        void on_close(const connection_hdl&) {
            connected = false;
            *output << "Connection closed. Reconnecting..." << std::endl;
            _failed_reconnecting = true;
            condition.notify_all();
            if (reconnect_thread.joinable()) {
              reconnect_thread.join();
            }
            reconnect_thread = std::thread(&NeuroGameClient::reconnect, this);
        }

        void on_fail(const connection_hdl&) {
            std::lock_guard<std::mutex> lock(mutex);
            *error << "Connection failed!" << std::endl;

            //If we don't retry on fail this flag will cause exceptions to be thrown if we're waiting for a forced action.
            //Otherwise we just keep waiting until we reconnect.
            if (!retry_on_fail) {
                connection_failed = true;
                condition.notify_all();
            }
            else {
                _failed_reconnecting = true;
                condition.notify_all();
                if (reconnect_thread.joinable()) {
                    reconnect_thread.join();
                }
                reconnect_thread = std::thread(&NeuroGameClient::reconnect, this);
            }
        }

        void send(const std::string& message) {
            std::lock_guard<std::mutex> lock(reconnectMutex);
            if (connection_failed) {
                *error << "Trying to send message on a failed connection" << std::endl;
                throw std::runtime_error("Trying to send message on a failed connection");
            }
            if (!connected) {
                std::cerr << "Connection closed. Storing message in queue." << std::endl;
                messageQueue.push(message);
            }
            else {
                websocketpp::lib::error_code ec;
                ws_client.send(ws_hdl, message, websocketpp::frame::opcode::text, ec);
                if (ec) {
                    *error << "Error sending message: " << ec.message() << " Storing message in queue." << std::endl;
                    messageQueue.push(message);
                }

            }


        }

        void connect(const std::string& uri) {
            websocketpp::lib::error_code ec;
            connection_failed = false;
            ws_client.reset();
            auto con = ws_client.get_connection(uri, ec);

            if (ec) {
                *error << "Error creating connection: " << ec.message() << std::endl;
                return;
            }

            ws_client.connect(con);
            ws_client.run();
        }

        client ws_client;
        connection_hdl ws_hdl;
        std::ostream* output;
        std::ostream* error;
        std::string game_name;
        std::thread connection_thread;
        std::thread reconnect_thread;
        std::mutex mutex;
        std::mutex reconnectMutex;
        std::condition_variable condition;
        NeuroResponse lastResponse;
        bool waitingForForcedAction = false;
        bool connected = false;

    public:
        bool isConnected() const {
            return connected;
        }

    protected:
        std::vector<std::string> forcedActions;
        int timeout;
        bool connection_failed = false;
        std::vector<std::string> disposableActions;
        std::string uri;
        std::queue<std::string> messageQueue;
        bool retry_on_fail;
        bool _failed_reconnecting = false;
        bool shutting_down = false;
    };

}

#endif
