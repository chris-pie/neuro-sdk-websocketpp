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
    enum Priority {
        LOW,
        MEDIUM,
        HIGH,
        CRITICAL,
    };

    inline std::string priorityToString(Priority priority) {
        switch (priority) {
            case LOW:
                return "low";
            case MEDIUM:
                return "medium";
            case HIGH:
                return "high";
            case CRITICAL:
                return "critical";
            default:
                return "low";
        }
    }

    class Action {
    public:
        // Constructor
        Action(std::string name, std::string description, const nlohmann::json &schema)
            : name(std::move(name)), description(std::move(description)), schema(schema) {
        }

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
        explicit NeuroResponse(const std::string &jsonStr);

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

    inline NeuroResponse::NeuroResponse(const std::string &jsonStr) {
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
            catch (const std::exception &) {
                data = "";
            }
        } catch (const nlohmann::json::parse_error &e) {
            throw std::invalid_argument("Invalid JSON string: " + std::string(e.what()));
        } catch (const std::exception &e) {
            throw std::invalid_argument("Error processing JSON: " + std::string(e.what()));
        }
    }


    // Type definitions
    using websocketpp::connection_hdl;
    using client = websocketpp::client<websocketpp::config::asio_client>;
    typedef websocketpp::config::asio_client::message_type::ptr message_ptr;

    enum VoiceChatAvailable {
        INITIALIZING, // Voice chat still initializing, unknown
        VOICE_CHAT_AVAILABLE, // Server returned voice/ready
        VOICE_CHAT_UNAVAILABLE, // Connection failed or server returned voice/unavailable
    };

    class NeuroVoiceClient {
    public:
        virtual ~NeuroVoiceClient() {
            shutting_down = true;
            if (voice_chat_available) {
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


        NeuroVoiceClient(const std::string &uri, std::string game_name, std::ostream *output_stream = &std::cout,
                         std::ostream *error_stream = &std::cerr, int timeout = -1)
            : game_name(std::move(game_name)), lastResponse(""), timeout(timeout), uri(uri) {
            output = output_stream;
            error = error_stream;
            ws_client.init_asio();
            ws_client.set_open_handler([this](connection_hdl &&PH1) { on_open(std::forward<decltype(PH1)>(PH1)); });
            ws_client.set_message_handler([this](connection_hdl &&PH1, message_ptr &&PH2) {
                on_message(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2));
            });
            ws_client.set_close_handler([this](connection_hdl &&PH1) { on_close(std::forward<decltype(PH1)>(PH1)); });
            ws_client.set_fail_handler([this](connection_hdl &&PH1) { on_fail(std::forward<decltype(PH1)>(PH1)); });
            reconnect_thread = std::thread(&NeuroVoiceClient::_connect, this);
        }


        void startVoiceSession() {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "voice/start";
            //Block until connection is established for startup.
            std::unique_lock<std::mutex> lock(reconnectMutex);
            if (!(connected || shutting_down)) {
                condition.wait(lock, [this]() { return connected || shutting_down; });
            }
            lock.unlock();
            send_startup(payload.dump());
        }

        void stopVoiceSession() {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "voice/stop";
            voice_chat_available = VOICE_CHAT_UNAVAILABLE;
            speakers.clear();
            send(payload.dump());
        }

        void sendRegisterSpeakers(const std::vector<std::string> &speakers) {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "voice/speakers/register";
            payload["data"]["speakers"] = nlohmann::json::array();
            for (const auto &speaker: speakers) {
                int id = speaker_id++;
                this->speakers[id] = speaker;
                nlohmann::json speaker_json;
                speaker_json["name"] = speaker;
                speaker_json["id"] = id;
                payload["data"]["speakers"].push_back(speaker_json);
            }
            send(payload.dump());
        }

        void sendUnregisterSpeakers(const std::vector<std::string> &speaker_names) {
            auto ids = speaker_names_to_ids(speaker_names);
            sendUnregisterSpeakers(ids);
        }

        void sendUnregisterSpeakers(const std::vector<int> &speaker_ids) {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "voice/speakers/unregister";
            nlohmann::json speakers_json;
            for (const auto &speaker: speaker_ids) {
                speakers_json.push_back(speaker);
                this->speakers.erase(speaker);
            }
            payload["data"]["ids"] = speakers_json;
            send(payload.dump());
        }


        void sendRenameSpeaker(const std::string &old_name, const std::string &new_name) {
            int id = speaker_name_to_id(old_name);
            sendRenameSpeaker(id, new_name);
        }

        void sendRenameSpeaker(const int id, const std::string &new_name) {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "voice/speakers/register";
            payload["data"]["speakers"] = nlohmann::json::array();
            this->speakers[id] = new_name;
            nlohmann::json speaker_json;
            speaker_json["name"] = new_name;
            speaker_json["id"] = id;
            payload["data"]["speakers"].push_back(speaker_json);
            send(payload.dump());
        }

        //Send PCM samples directly
        //You should use VoiceSampleStream instead for automatic splitting but this is kept public just in case.
        void sendVoiceSamples(int speaker_id,
                              const float *samples,
                              std::size_t sample_count) {
            if (speaker_id < 0 || speaker_id > 0xFFFF) {
                throw std::invalid_argument("speaker_id must fit in uint16_t");
            }

            if (sample_count != 0 && samples == nullptr) {
                throw std::invalid_argument("samples must not be null");
            }

            // Header: 4 bytes, followed by 4 bytes per float sample.
            std::string message;
            message.reserve(4 + sample_count * sizeof(float));

            message.push_back(1); // Protocol version
            message.push_back(0); // Reserved flags

            const auto speaker =
                    static_cast<std::uint16_t>(speaker_id);

            // Speaker ID, little-endian.
            message.push_back(static_cast<char>(speaker & 0xFF));
            message.push_back(static_cast<char>((speaker >> 8) & 0xFF));

            // PCM samples encoded as little-endian float32 values.
            for (std::size_t i = 0; i < sample_count; ++i) {
                std::uint32_t bits = 0;
                static_assert(sizeof(bits) == sizeof(samples[i]),
                              "float must be 32 bits");

                std::memcpy(&bits, &samples[i], sizeof(bits));

                message.push_back(static_cast<char>(bits & 0xFF));
                message.push_back(static_cast<char>((bits >> 8) & 0xFF));
                message.push_back(static_cast<char>((bits >> 16) & 0xFF));
                message.push_back(static_cast<char>((bits >> 24) & 0xFF));
            }

            this->send(message, websocketpp::frame::opcode::binary);
        }

        class VoiceSampleStream {
        public:
            static constexpr std::size_t samples_per_frame = 960;

            VoiceSampleStream(NeuroVoiceClient &client, int speaker_id)
                : client_(&client),
                  speaker_id_(speaker_id),
                  finished_(false) {
                if (speaker_id < 0 || speaker_id > 0xFFFF) {
                    throw std::invalid_argument(
                        "speaker_id must fit in uint16_t");
                }

                buffer_.reserve(samples_per_frame);
            }

            VoiceSampleStream(const VoiceSampleStream &) = delete;

            VoiceSampleStream &operator=(const VoiceSampleStream &) = delete;

            VoiceSampleStream(VoiceSampleStream &&other)
                noexcept : client_(other.client_),
                           speaker_id_(other.speaker_id_),
                           buffer_(std::move(other.buffer_)),
                           finished_(other.finished_) {
                other.client_ = nullptr;
                other.finished_ = true;
            }

            VoiceSampleStream &operator=(VoiceSampleStream &&) = delete;

            void write(const float *samples, std::size_t sample_count) {
                if (finished_) {
                    throw std::logic_error(
                        "Cannot write to a finished voice stream");
                }

                if (sample_count != 0 && samples == nullptr) {
                    throw std::invalid_argument(
                        "samples must not be null");
                }

                // Complete a previously buffered partial frame first.
                if (!buffer_.empty()) {
                    const std::size_t required =
                            samples_per_frame - buffer_.size();
                    const std::size_t copied =
                            std::min(required, sample_count);

                    buffer_.insert(
                        buffer_.end(), samples, samples + copied);

                    samples += copied;
                    sample_count -= copied;

                    if (buffer_.size() == samples_per_frame) {
                        client_->sendVoiceSamples(
                            speaker_id_,
                            buffer_.data(),
                            buffer_.size());
                        buffer_.clear();
                    }
                }

                // Send complete frames directly from the caller's memory.
                while (sample_count >= samples_per_frame) {
                    client_->sendVoiceSamples(
                        speaker_id_, samples, samples_per_frame);

                    samples += samples_per_frame;
                    sample_count -= samples_per_frame;
                }

                // Preserve the remaining partial frame for the next write.
                if (sample_count != 0) {
                    buffer_.insert(
                        buffer_.end(), samples, samples + sample_count);
                }
            }

            void write(const std::vector<float> &samples) {
                write(samples.data(), samples.size());
            }

            VoiceSampleStream &operator()(
                const float *samples,
                const std::size_t sample_count) {
                write(samples, sample_count);
                return *this;
            }

            void finish() {
                if (finished_) {
                    return;
                }

                // Mark the stream finished only after a successful send,
                // allowing finish() to be retried if sending throws.
                if (!buffer_.empty()) {
                    client_->sendVoiceSamples(
                        speaker_id_,
                        buffer_.data(),
                        buffer_.size());
                    buffer_.clear();
                }

                finished_ = true;
            }

            bool finished() const {
                return finished_;
            }

        private:
            NeuroVoiceClient *client_;
            int speaker_id_;
            std::vector<float> buffer_;
            bool finished_;
        };

        VoiceSampleStream createVoiceStream(int speaker_id) {
            return VoiceSampleStream(*this, speaker_id);
        }

        bool isConnected() const {
            return connected;
        }

        VoiceChatAvailable get_voice_chat_available() const {
            return voice_chat_available;
        }

        std::map<int, std::string> get_speakers() const {
            return speakers;
        }

    protected:
        std::ostream *output;
        std::ostream *error;

        //Override this method to handle incoming voice data from Neuro
        virtual void handleVoiceData(std::string const &data) = 0;

        //Override this method to handle when Neuro starts/stops speaking.
        virtual void handleSpeakingChanged(bool speaking) = 0;

        //Override this method to handle when Neuro gets interrupted/[FILTERED].
        //You MUST mute her immediately and discard any buffered audio.
        virtual void handleCancelled() = 0;

        //Optionally override these methods to handle the startup acknowledgement/failure.
        //Alternatively the parameter values are stored in the object and can be accessed via getters at any time.
        virtual void handleStartupAcknowledgement(int sample_rate, int channels) {
        }

        virtual void handleStartupFail(std::string const &reason) {
        }

    private:
        std::vector<int> speaker_names_to_ids(const std::vector<std::string> &names) const {
            std::vector<int> ids;
            for (const auto &speaker: names) {
                ids.push_back(speaker_name_to_id(speaker));
            }
            return ids;
        }

        int speaker_name_to_id(const std::string &name) const {
            for (const auto &pair: this->speakers) {
                if (pair.second == name) {
                    return pair.first;
                }
            }
            throw std::runtime_error("Speaker not found: " + name);
        }

        void reconnect() {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            _connect();
        }

        static std::string derive_uri(const std::string &base_uri,
                                      const std::string &game) {
            if (base_uri.empty() || game.empty()) {
                return {};
            }

            std::string finalUri = base_uri;
            std::string query;

            const std::size_t queryPos = finalUri.find('?');
            if (queryPos != std::string::npos) {
                query = finalUri.substr(queryPos);
                finalUri.erase(queryPos);
            }

            while (!finalUri.empty() && finalUri.back() == '/') {
                finalUri.pop_back();
            }

            static constexpr char hex[] = "0123456789ABCDEF";
            std::string encodedGame;
            encodedGame.reserve(game.size());

            for (char it : game) {
                const auto character =
                        static_cast<unsigned char>(it);

                const bool unreserved =
                        (character >= 'A' && character <= 'Z') ||
                        (character >= 'a' && character <= 'z') ||
                        (character >= '0' && character <= '9') ||
                        character == '-' ||
                        character == '_' ||
                        character == '.' ||
                        character == '~';

                if (unreserved) {
                    encodedGame.push_back(static_cast<char>(character));
                } else {
                    encodedGame.push_back('%');
                    encodedGame.push_back(hex[(character >> 4) & 0x0F]);
                    encodedGame.push_back(hex[character & 0x0F]);
                }
            }

            std::string path;
            const std::string gamePrefix = "/game/";
            const std::size_t gameIndex = finalUri.rfind(gamePrefix);

            if (finalUri.size() >= 5 &&
                finalUri.compare(finalUri.size() - 5, 5, "/game") == 0) {
                path = finalUri + "/" + encodedGame + "/voice";
            } else if (gameIndex != std::string::npos &&
                       finalUri.find('/', gameIndex + gamePrefix.size()) ==
                       std::string::npos) {
                // Already .../game/<name>; reuse the existing name segment.
                path = finalUri + "/voice";
            } else {
                path = finalUri + "/game/" + encodedGame + "/voice";
            }

            return path + query;
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
            std::string baseUri = !uri.empty()
                                      ? uri
                                      : std::getenv("NEURO_SDK_WS_URL")
                                            ? std::getenv("NEURO_SDK_WS_URL")
                                            : "";

            if (baseUri.empty()) {
                *error << "URI was not provided. Set NEURO_SDK_WS_URL environment variable." << std::endl;
                throw std::runtime_error("URI was not provided. Set NEURO_SDK_WS_URL environment variable.");
            }

            std::string finalUri = derive_uri(baseUri, game_name);

            connection_thread = std::thread(&NeuroVoiceClient::connect, this, finalUri);
            if (timeout >= 0) {
                bool success = condition.wait_for(lock, std::chrono::seconds(timeout), [this]() {
                    return connected || connection_failed || _failed_reconnecting || shutting_down;
                });
                if (!success || connection_failed) {
                    *error << "Failed to connect to server" << std::endl;
                    voice_chat_available = VOICE_CHAT_UNAVAILABLE;
                    throw std::runtime_error("Failed to connect to server");
                }
            } else {
                condition.wait(lock, [this]() {
                    return connected || connection_failed || _failed_reconnecting || shutting_down;
                });
                if (connection_failed) {
                    *error << "Failed to connect to server" << std::endl;
                    voice_chat_available = VOICE_CHAT_UNAVAILABLE;
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

        void on_open(connection_hdl hdl) {
            *output << "Connection established!" << std::endl;
            connected = true;
            ws_hdl = std::move(hdl);
            condition.notify_all();
        }


        void on_message(const connection_hdl &, const client::message_ptr &msg) {
            std::lock_guard<std::mutex> lock(mutex);
            auto opcode = msg->get_opcode();
            if (opcode == websocketpp::frame::opcode::text) {
                auto message = msg->get_payload();
                auto JsonMessage = nlohmann::json::parse(message);
                if (JsonMessage["command"] == "voice/cancelled") {
                    handleCancelled();
                    return;
                }
                if (JsonMessage["command"] == "voice/ready") {
                    try {
                        sample_rate = JsonMessage["data"]["sample_rate"];
                        channels = JsonMessage["data"]["channels"];
                        handleStartupAcknowledgement(sample_rate, channels);
                        voice_chat_available = VOICE_CHAT_AVAILABLE;
                        return;
                    } catch (const std::exception &e) {
                        throw std::invalid_argument(
                            "Startup acknowledgement was received but as malformed: " + std::string(e.what()));
                    }
                }
                if (JsonMessage["command"] == "voice/unavailable") {
                    std::string fail_reason;
                    try {
                        fail_reason = JsonMessage["data"]["reason"];
                    } catch (...) {
                        //if no reason present
                        fail_reason = "";
                    }
                    handleStartupFail(fail_reason);
                    voice_chat_available = VOICE_CHAT_UNAVAILABLE;
                    return;
                }
                if (JsonMessage["command"] == "voice/speaking") {
                    const bool speaking = JsonMessage["data"]["speaking"];
                    handleSpeakingChanged(speaking);
                    return;
                }
                NeuroResponse const response = NeuroResponse(message);
                lastResponse = response;
            } else if (opcode == websocketpp::frame::opcode::binary) {
                handleVoiceData(msg->get_payload());
            }
            condition.notify_all();
        }

        void on_close(const connection_hdl &) {
            voice_chat_available = INITIALIZING;
            connected = false;
            *output << "Connection closed. Reconnecting..." << std::endl;
            _failed_reconnecting = true;
            condition.notify_all();
            if (reconnect_thread.joinable()) {
                reconnect_thread.join();
            }
            reconnect_thread = std::thread(&NeuroVoiceClient::reconnect, this);
        }

        void on_fail(const connection_hdl &) {
            //For voice chat, fail is to be treated as voice chat unavailable.
            std::lock_guard<std::mutex> lock(mutex);
            *error << "Voice chat: Connection failed!" << std::endl;
            voice_chat_available = VOICE_CHAT_UNAVAILABLE;
            //Don't try to reconnect or throw, just keep a disconnected object.
        }

        void send(const std::string &message,
                  websocketpp::frame::opcode::value format = websocketpp::frame::opcode::text) {
            std::lock_guard<std::mutex> lock(reconnectMutex);
            if (connection_failed) {
                *error << "Trying to send message on a failed connection" << std::endl;
                throw std::runtime_error("Trying to send message on a failed connection");
            }
            if (voice_chat_available != VOICE_CHAT_AVAILABLE) {
                std::cerr << "Voice chat is unavailable. Cannot send message." << std::endl;
                throw std::runtime_error("Voice chat is unavailable. Cannot send message.");
            }
            websocketpp::lib::error_code ec;
            ws_client.send(ws_hdl, message, format, ec);
            if (ec) {
                *error << "Error sending message: " << ec.message() << std::endl;
            }
        }

        void send_startup(const std::string &message) {
            std::lock_guard<std::mutex> lock(reconnectMutex);
            if (connection_failed) {
                *error << "Trying to send message on a failed connection" << std::endl;
                throw std::runtime_error("Trying to send message on a failed connection");
            }
            websocketpp::lib::error_code ec;
            ws_client.send(ws_hdl, message, websocketpp::frame::opcode::text, ec);
            if (ec) {
                *error << "Error sending message: " << ec.message() << std::endl;
            }
        }


        void connect(const std::string &uri) {
            websocketpp::lib::error_code ec;
            connection_failed = false;
            ws_client.reset();
            auto con = ws_client.get_connection(uri, ec);

            if (ec) {
                *error << "Error creating connection: " << ec.message() << std::endl;
                voice_chat_available = VOICE_CHAT_UNAVAILABLE;
                return;
            }

            ws_client.connect(con);
            ws_client.run();
        }

        client ws_client;
        connection_hdl ws_hdl;

        std::string game_name;
        std::thread connection_thread;
        std::thread reconnect_thread;
        std::mutex mutex;
        std::mutex reconnectMutex;
        std::condition_variable condition;
        NeuroResponse lastResponse;
        std::map<int, std::string> speakers;
        int speaker_id = 1;
        VoiceChatAvailable voice_chat_available = INITIALIZING;
        bool connected = false;
        int sample_rate;
        int channels;
        int timeout;
        bool connection_failed = false;
        std::string uri;
        std::queue<std::string> messageQueue;
        bool _failed_reconnecting = false;
        bool shutting_down = false;
    };

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


        NeuroGameClient(const std::string &uri, std::string game_name, std::ostream *output_stream = &std::cout,
                        std::ostream *error_stream = &std::cerr, int timeout = -1, bool retry_on_fail = true)
            : game_name(std::move(game_name)), lastResponse(""), timeout(timeout), uri(uri),
              retry_on_fail(retry_on_fail) {
            output = output_stream;
            error = error_stream;
            ws_client.init_asio();
            ws_client.set_open_handler([this](connection_hdl &&PH1) { on_open(std::forward<decltype(PH1)>(PH1)); });
            ws_client.set_message_handler([this](connection_hdl &&PH1, message_ptr &&PH2) {
                on_message(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2));
            });
            ws_client.set_close_handler([this](connection_hdl &&PH1) { on_close(std::forward<decltype(PH1)>(PH1)); });
            ws_client.set_fail_handler([this](connection_hdl &&PH1) { on_fail(std::forward<decltype(PH1)>(PH1)); });
            reconnect_thread = std::thread(&NeuroGameClient::_connect, this);
        }


        void sendStartup() {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "startup";
            //Block until connection is established for startup.
            std::unique_lock<std::mutex> lock(reconnectMutex);
            if (!(connected || shutting_down)) {
                condition.wait(lock, [this]() { return connected || shutting_down; });
            }
            lock.unlock();
            send(payload.dump());
        }

        void sendContext(const std::string &context_message, bool silent) {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "context";
            payload["data"]["message"] = context_message;
            payload["data"]["silent"] = silent;
            send(payload.dump());
        }

        void sendRegisterActions(const std::vector<Action> &actions) {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "actions/register";
            payload["data"]["actions"] = nlohmann::json::array();
            for (const auto &action: actions) {
                nlohmann::json action_json;
                action_json["name"] = action.getName();
                action_json["description"] = action.getDescription();
                action_json["schema"] = action.getSchema();
                payload["data"]["actions"].push_back(action_json);
            }
            send(payload.dump());
        }

        void sendUnregisterActions(const std::vector<std::string> &action_names) {
            nlohmann::json payload;
            payload["game"] = game_name;
            payload["command"] = "actions/unregister";
            nlohmann::json action_names_json;
            for (const auto &action: action_names) {
                action_names_json.push_back(action);
            }
            payload["data"]["action_names"] = action_names_json;
            send(payload.dump());
        }

        void sendForceActions(const std::string &state, const std::string &query, bool ephemeral,
                              const std::vector<std::string> &actions, Priority priority = Priority::LOW) {
            nlohmann::json payload;
            payload["command"] = "actions/force";
            payload["game"] = game_name;
            payload["data"]["state"] = state;
            payload["data"]["query"] = query;
            payload["data"]["ephemeral_context"] = ephemeral;
            payload["data"]["priority"] = priorityToString(priority);
            payload["data"]["action_names"] = actions;
            send(payload.dump());
        }

        void sendActionResult(const NeuroResponse &neuroAction, bool success, const std::string &message) {
            nlohmann::json payload;
            payload["command"] = "action/result";
            payload["game"] = game_name;
            payload["data"]["id"] = neuroAction.getId();
            payload["data"]["success"] = success;
            payload["data"]["message"] = message;
            send(payload.dump());
        }

        void forceAction(const std::string &state, const std::string &query, bool ephemeral,
                         const std::vector<std::string> &actions, Priority priority = Priority::LOW) {
            std::unique_lock<std::mutex> lock(mutex);
            forcedActions = actions;
            waitingForForcedAction = true;
            sendForceActions(state, query, ephemeral, actions, priority);
            if (timeout >= 0) {
                bool success = condition.wait_for(lock, std::chrono::seconds(timeout), [this]() {
                    return !waitingForForcedAction || connection_failed || shutting_down;
                });
                if (!success || connection_failed) {
                    *error << "Error waiting for forced action" << std::endl;
                    throw std::runtime_error("Error waiting for forced action");
                }
            } else {
                condition.wait(lock, [this]() {
                    return !waitingForForcedAction || connection_failed || shutting_down;
                });
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
        void forceDisposableActions(const std::string &state, const std::string &query, bool ephemeral,
                                    const std::vector<Action> &actions, bool forceUnregister = false,
                                    Priority priority = Priority::LOW) {
            sendRegisterActions(actions);
            disposableActions = getActionNamesFromActions(actions);
            forceAction(state, query, ephemeral, getActionNamesFromActions(actions), priority);
            if (forceUnregister) {
                sendUnregisterActions(disposableActions);
            }
        }

        static std::vector<std::string> getActionNamesFromActions(const std::vector<Action> &actions) {
            std::vector<std::string> actionNames;
            for (const auto &action: actions) {
                actionNames.push_back(action.getName());
            }
            return actionNames;
        }

        bool isConnected() const {
            return connected;
        }

        std::string session_id() const {
            return sessionId;
        }

        std::string character_id() const {
            return characterId;
        }

        std::string display_name() const {
            return displayName;
        }

        NeuroResponse last_response() const {
            return lastResponse;
        }

    protected:
        //Override this method to handle actions.
        //To prevent race conditions, only accept disposable actions when waitingForForcedAction is true
        //When valid action arrives, set waitingForForcedAction to false to continue execution
        //Remember to unregister actions and send action result to Neuro ASAP
        virtual void handleMessage(NeuroResponse const &response) = 0;

        //Override this method to handle startup acknowledgement.
        //Optional. Startup acknowledgement data is also stored in NeuroGameClient object.
        virtual void handleStartupAcknowledgement(std::string const &_sessionId, std::string const &_characterId,
                                                  std::string const &_displayName) {
        };

        std::ostream *output;
        std::ostream *error;
        bool waitingForForcedAction = false;
        std::vector<std::string> disposableActions;

    private:
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
                                       : std::getenv("NEURO_SDK_WS_URL")
                                             ? std::getenv("NEURO_SDK_WS_URL")
                                             : "";

            if (finalUri.empty()) {
                *error << "URI was not provided. Set NEURO_SDK_WS_URL environment variable." << std::endl;
                throw std::runtime_error("URI was not provided. Set NEURO_SDK_WS_URL environment variable.");
            }

            connection_thread = std::thread(&NeuroGameClient::connect, this, finalUri);
            if (timeout >= 0) {
                bool success = condition.wait_for(lock, std::chrono::seconds(timeout), [this]() {
                    return connected || connection_failed || _failed_reconnecting || shutting_down;
                });
                if (!success || connection_failed) {
                    *error << "Failed to connect to server" << std::endl;
                    throw std::runtime_error("Failed to connect to server");
                }
            } else {
                condition.wait(lock, [this]() {
                    return connected || connection_failed || _failed_reconnecting || shutting_down;
                });
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

        void on_open(connection_hdl hdl) {
            *output << "Connection established!" << std::endl;
            ws_hdl = std::move(hdl);
            connected = true;
            condition.notify_all();
        }


        void on_message(const connection_hdl &, const client::message_ptr &msg) { {
                std::lock_guard<std::mutex> lock(mutex);
                auto message = msg->get_payload();
                auto JsonMessage = nlohmann::json::parse(message);
                if (JsonMessage["command"] == "actions/reregister_all")
                    return;
                if (JsonMessage["command"] == "startup") {
                    try {
                        sessionId = JsonMessage["data"]["session"]["sessionId"];
                        characterId = JsonMessage["data"]["session"]["characterId"];
                        displayName = JsonMessage["data"]["session"]["displayName"];
                        handleStartupAcknowledgement(sessionId, characterId, displayName);
                        return;
                    } catch (const std::exception &e) {
                        throw std::invalid_argument(
                            "Startup acknowledgement was received but as malformed: " + std::string(e.what()));
                    }
                }
                NeuroResponse const response = NeuroResponse(message);
                handleMessage(response);
                lastResponse = response;
            }

            condition.notify_all();
        }

        void on_close(const connection_hdl &) {
            connected = false;
            *output << "Connection closed. Reconnecting..." << std::endl;
            _failed_reconnecting = true;
            condition.notify_all();
            if (reconnect_thread.joinable()) {
                reconnect_thread.join();
            }
            reconnect_thread = std::thread(&NeuroGameClient::reconnect, this);
        }

        void on_fail(const connection_hdl &) {
            std::lock_guard<std::mutex> lock(mutex);
            *error << "Connection failed!" << std::endl;

            //If we don't retry on fail this flag will cause exceptions to be thrown if we're waiting for a forced action.
            //Otherwise we just keep waiting until we reconnect.
            if (!retry_on_fail) {
                connection_failed = true;
                condition.notify_all();
            } else {
                _failed_reconnecting = true;
                condition.notify_all();
                if (reconnect_thread.joinable()) {
                    reconnect_thread.join();
                }
                reconnect_thread = std::thread(&NeuroGameClient::reconnect, this);
            }
        }

        void send(const std::string &message) {
            std::lock_guard<std::mutex> lock(reconnectMutex);
            if (connection_failed) {
                *error << "Trying to send message on a failed connection" << std::endl;
                throw std::runtime_error("Trying to send message on a failed connection");
            }
            if (!connected) {
                std::cerr << "Connection closed. Storing message in queue." << std::endl;
                messageQueue.push(message);
            } else {
                websocketpp::lib::error_code ec;
                ws_client.send(ws_hdl, message, websocketpp::frame::opcode::text, ec);
                if (ec) {
                    *error << "Error sending message: " << ec.message() << " Storing message in queue." << std::endl;
                    messageQueue.push(message);
                }
            }
        }

        void connect(const std::string &uri) {
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

        std::string game_name;
        std::thread connection_thread;
        std::thread reconnect_thread;
        std::mutex mutex;
        std::mutex reconnectMutex;
        std::condition_variable condition;
        NeuroResponse lastResponse;

        bool connected = false;
        std::string sessionId;
        std::string characterId;
        std::string displayName;
        std::vector<std::string> forcedActions;
        int timeout;
        bool connection_failed = false;
        std::string uri;
        std::queue<std::string> messageQueue;
        bool retry_on_fail;
        bool _failed_reconnecting = false;
        bool shutting_down = false;
    };
}

#endif
