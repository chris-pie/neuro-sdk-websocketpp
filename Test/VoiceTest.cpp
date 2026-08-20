#include "../NeuroGameSdkWebsocketpp.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

class ConsoleVoiceClient : public NeuroWebsocketpp::NeuroVoiceClient {
public:
    ConsoleVoiceClient(const std::string &uri, const std::string &gameName)
        : NeuroVoiceClient(uri, gameName, &std::cout, &std::cerr) {
    }

    void start() {
        logOutgoing("voice/start");
        startVoiceSession();
    }

    void stop() {
        logOutgoing("voice/stop");
        stopVoiceSession();
        speakerIds_.clear();
    }

    void registerSpeakers(const std::vector<std::string> &names) {
        logOutgoing("voice/speakers/register: " + join(names));

        // IDs are allocated sequentially by NeuroVoiceClient, starting at 1.
        const int firstId = nextSpeakerId_;
        sendRegisterSpeakers(names);

        for (std::size_t i = 0; i < names.size(); ++i) {
            speakerIds_[names[i]] = firstId + static_cast<int>(i);
        }
        nextSpeakerId_ += static_cast<int>(names.size());
    }

    void unregisterSpeakersByName(const std::vector<std::string> &names) {
        logOutgoing("voice/speakers/unregister by name: " + join(names));
        sendUnregisterSpeakers(names);

        for (const std::string &name : names) {
            speakerIds_.erase(name);
        }
    }

    void unregisterSpeakersById(const std::vector<int> &ids) {
        logOutgoing("voice/speakers/unregister by ID: " + join(ids));
        sendUnregisterSpeakers(ids);

        for (std::map<std::string, int>::iterator it = speakerIds_.begin();
             it != speakerIds_.end();) {
            bool removed = false;
            for (int id : ids) {
                if (it->second == id) {
                    removed = true;
                    break;
                }
            }

            if (removed) {
                it = speakerIds_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void renameSpeakerByName(const std::string &oldName,
                             const std::string &newName) {
        logOutgoing("voice/speakers/register (rename): \"" + oldName +
                    "\" -> \"" + newName + "\"");
        sendRenameSpeaker(oldName, newName);

        const int id = speakerIds_[oldName];
        speakerIds_.erase(oldName);
        speakerIds_[newName] = id;
    }

    void renameSpeakerById(int id, const std::string &newName) {
        logOutgoing("voice/speakers/register (rename ID " +
                    std::to_string(id) + "): \"" + newName + "\"");
        sendRenameSpeaker(id, newName);

        for (std::map<std::string, int>::iterator it = speakerIds_.begin();
             it != speakerIds_.end(); ++it) {
            if (it->second == id) {
                speakerIds_.erase(it);
                break;
            }
        }
        speakerIds_[newName] = id;
    }

    void sendSilenceFrame(int speakerId) {
        std::vector<float> samples(
            NeuroVoiceClient::VoiceSampleStream::samples_per_frame, 0.0f);

        logOutgoing("binary voice frame: speaker ID " +
                    std::to_string(speakerId) + ", " +
                    std::to_string(samples.size()) + " samples");
        sendVoiceSamples(speakerId, samples.data(), samples.size());
    }

    void sendSilenceStream(int speakerId, std::size_t sampleCount) {
        std::vector<float> samples(sampleCount, 0.0f);

        logOutgoing("voice stream: speaker ID " + std::to_string(speakerId) +
                    ", " + std::to_string(sampleCount) + " samples");

        NeuroVoiceClient::VoiceSampleStream stream =
            createVoiceStream(speakerId);
        stream.write(samples);
        stream.finish();

        log("[OUT] Voice stream finished.");
    }

    void printStatus() const {
        std::ostringstream status;
        status << "[STATUS] connected=" << (isConnected() ? "true" : "false")
               << ", voice=" << voiceAvailabilityToString(
                   get_voice_chat_available());

        const std::map<int, std::string> speakers = get_speakers();
        status << ", speakers=";

        if (speakers.empty()) {
            status << "(none)";
        } else {
            bool first = true;
            for (const std::pair<const int, std::string> &speaker : speakers) {
                if (!first) {
                    status << ", ";
                }
                status << speaker.first << ":\"" << speaker.second << "\"";
                first = false;
            }
        }

        log(status.str());
    }

protected:
    void handleVoiceData(const std::string &data) override {
        log("[IN] Binary voice message arrived (" +
            std::to_string(data.size()) +
            " bytes).");
    }

    void handleSpeakingChanged(bool speaking) override {
        log(std::string("[IN] voice/speaking: ") +
            (speaking ? "true" : "false"));
    }

    void handleCancelled() override {
        log("[IN] voice/cancelled: discard any locally buffered audio now.");
    }

    void handleStartupAcknowledgement(int sampleRate, int channels) override {
        log("[IN] voice/ready: sample_rate=" + std::to_string(sampleRate) +
            ", channels=" + std::to_string(channels));
    }

    void handleStartupFail(const std::string &reason) override {
        log("[IN] voice/unavailable" +
            (reason.empty() ? std::string() : ": " + reason));
    }

private:
    static std::string voiceAvailabilityToString(
        NeuroWebsocketpp::VoiceChatAvailable available) {
        switch (available) {
            case NeuroWebsocketpp::INITIALIZING:
                return "initializing";
            case NeuroWebsocketpp::VOICE_CHAT_AVAILABLE:
                return "available";
            case NeuroWebsocketpp::VOICE_CHAT_UNAVAILABLE:
                return "unavailable";
        }
        return "unknown";
    }

    template <typename T>
    static std::string join(const std::vector<T> &values) {
        std::ostringstream result;

        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i != 0) {
                result << ", ";
            }
            result << values[i];
        }

        return result.str();
    }

    void logOutgoing(const std::string &message) const {
        log("[OUT] " + message);
    }

    void log(const std::string &message) const {
        std::lock_guard<std::mutex> lock(consoleMutex_);
        std::cout << message << std::endl;
    }

    mutable std::mutex consoleMutex_;
    std::map<std::string, int> speakerIds_;
    int nextSpeakerId_ = 1;
};

static void printHelp() {
    std::cout
        << "Commands:\n"
        << "  start                         Start the voice session\n"
        << "  stop                          Stop the voice session\n"
        << "  register <name> [name...]     Register one or more speakers\n"
        << "  unregister-name <name> [...]  Unregister speakers by name\n"
        << "  unregister-id <id> [...]      Unregister speakers by ID\n"
        << "  rename-name <old> <new>       Rename a speaker by name\n"
        << "  rename-id <id> <new>          Rename a speaker by ID\n"
        << "  frame <speaker-id>            Send one 960-sample silent PCM frame\n"
        << "  stream <speaker-id> <count>   Send silent PCM through VoiceSampleStream\n"
        << "  status                        Show connection and speaker state\n"
        << "  help                          Show this help text\n"
        << "  quit                          Exit\n";
}

int main(int argc, char **argv) {
    const std::string uri = argc > 1 ? argv[1] : "";
    const std::string gameName = argc > 2 ? argv[2] : "Voice Console Test";

    std::cout << "Voice client test\n"
              << "URI: " << (uri.empty() ? "NEURO_SDK_WS_URL environment variable"
                                         : uri)
              << "\nGame: " << gameName << "\n\n";

    try {
        ConsoleVoiceClient client(uri, gameName);
        printHelp();

        std::string line;
        while (std::cout << "\n> " && std::getline(std::cin, line)) {
            std::istringstream input(line);
            std::string command;
            input >> command;

            try {
                if (command.empty()) {
                    continue;
                }

                if (command == "quit" || command == "exit") {
                    break;
                }

                if (command == "help") {
                    printHelp();
                } else if (command == "start") {
                    client.start();
                } else if (command == "stop") {
                    client.stop();
                } else if (command == "register") {
                    std::vector<std::string> names;
                    std::string name;
                    while (input >> name) {
                        names.push_back(name);
                    }
                    client.registerSpeakers(names);
                } else if (command == "unregister-name") {
                    std::vector<std::string> names;
                    std::string name;
                    while (input >> name) {
                        names.push_back(name);
                    }
                    client.unregisterSpeakersByName(names);
                } else if (command == "unregister-id") {
                    std::vector<int> ids;
                    int id;
                    while (input >> id) {
                        ids.push_back(id);
                    }
                    client.unregisterSpeakersById(ids);
                } else if (command == "rename-name") {
                    std::string oldName;
                    std::string newName;
                    input >> oldName >> newName;
                    client.renameSpeakerByName(oldName, newName);
                } else if (command == "rename-id") {
                    int id;
                    std::string newName;
                    input >> id >> newName;
                    client.renameSpeakerById(id, newName);
                } else if (command == "frame") {
                    int speakerId;
                    input >> speakerId;
                    client.sendSilenceFrame(speakerId);
                } else if (command == "stream") {
                    int speakerId;
                    std::size_t sampleCount;
                    input >> speakerId >> sampleCount;
                    client.sendSilenceStream(speakerId, sampleCount);
                } else if (command == "status") {
                    client.printStatus();
                } else {
                    std::cerr << "Unknown command. Type 'help' for commands.\n";
                }
            } catch (const std::exception &exception) {
                std::cerr << "Command failed: " << exception.what() << '\n';
            }
        }
    } catch (const std::exception &exception) {
        std::cerr << "Client failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}