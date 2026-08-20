//Make derive_uri method public for these tests to work

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../NeuroGameSdkWebsocketpp.hpp"

namespace {

struct DeriveUriTestCase {
    const char* description;
    std::string base_uri;
    std::string game;
    std::string expected;
};

void require_equal(const DeriveUriTestCase& test) {
    const std::string actual =
        NeuroWebsocketpp::NeuroVoiceClient::derive_uri(
            test.base_uri, test.game);

    if (actual != test.expected) {
        std::cerr
            << "FAILED: " << test.description << '\n'
            << "  Base URI: " << test.base_uri << '\n'
            << "  Game:     " << test.game << '\n'
            << "  Expected: " << test.expected << '\n'
            << "  Actual:   " << actual << '\n';

        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    const std::vector<DeriveUriTestCase> tests = {
        {
            "Empty base URI returns an empty string",
            "",
            "My Game",
            ""
        },
        {
            "Empty game returns an empty string",
            "ws://localhost:8000",
            "",
            ""
        },
        {
            "Both arguments empty return an empty string",
            "",
            "",
            ""
        },
        {
            "Adds the complete game and voice path",
            "ws://localhost:8000",
            "MyGame",
            "ws://localhost:8000/game/MyGame/voice"
        },
        {
            "Adds the game name to a URI ending in /game",
            "ws://localhost:8000/game",
            "MyGame",
            "ws://localhost:8000/game/MyGame/voice"
        },
        {
            "Trims one trailing slash before processing",
            "ws://localhost:8000/game/",
            "MyGame",
            "ws://localhost:8000/game/MyGame/voice"
        },
        {
            "Trims multiple trailing slashes",
            "ws://localhost:8000/game///",
            "MyGame",
            "ws://localhost:8000/game/MyGame/voice"
        },
        {
            "Reuses an existing game name",
            "ws://localhost:8000/game/ExistingGame",
            "IgnoredGame",
            "ws://localhost:8000/game/ExistingGame/voice"
        },
        {
            "Reuses an existing game name after trimming a slash",
            "ws://localhost:8000/game/ExistingGame/",
            "IgnoredGame",
            "ws://localhost:8000/game/ExistingGame/voice"
        },
        {
            "Does not reuse a game segment followed by another path segment",
            "ws://localhost:8000/game/ExistingGame/api",
            "MyGame",
            "ws://localhost:8000/game/ExistingGame/api/game/MyGame/voice"
        },
        {
            "Does not treat uppercase /Game as lowercase /game",
            "ws://localhost:8000/Game",
            "MyGame",
            "ws://localhost:8000/Game/game/MyGame/voice"
        },
        {
            "Preserves a query string",
            "ws://localhost:8000?token=abc&mode=voice",
            "MyGame",
            "ws://localhost:8000/game/MyGame/voice?token=abc&mode=voice"
        },
        {
            "Preserves a query on a URI ending in /game",
            "ws://localhost:8000/game?token=abc",
            "MyGame",
            "ws://localhost:8000/game/MyGame/voice?token=abc"
        },
        {
            "Preserves a query for an existing game name",
            "ws://localhost:8000/game/ExistingGame?token=abc",
            "IgnoredGame",
            "ws://localhost:8000/game/ExistingGame/voice?token=abc"
        },
        {
            "Preserves an empty query marker",
            "ws://localhost:8000?",
            "MyGame",
            "ws://localhost:8000/game/MyGame/voice?"
        },
        {
            "Encodes spaces in the game name",
            "ws://localhost:8000",
            "My Game",
            "ws://localhost:8000/game/My%20Game/voice"
        },
        {
            "Encodes reserved URI characters",
            "ws://localhost:8000",
            "game/name?mode=test&x=1",
            "ws://localhost:8000/game/game%2Fname%3Fmode%3Dtest%26x%3D1/voice"
        },
        {
            "Encodes a plus sign instead of treating it as a space",
            "ws://localhost:8000",
            "C++",
            "ws://localhost:8000/game/C%2B%2B/voice"
        },
        {
            "Leaves RFC 3986 unreserved characters unchanged",
            "ws://localhost:8000",
            "AZaz09-_.~",
            "ws://localhost:8000/game/AZaz09-_.~/voice"
        },
        {
            "Percent-encodes UTF-8 bytes",
            "ws://localhost:8000",
            u8"Café",
            "ws://localhost:8000/game/Caf%C3%A9/voice"
        },
        {
            "Retains an existing base path",
            "wss://example.com/api/v1",
            "MyGame",
            "wss://example.com/api/v1/game/MyGame/voice"
        },
        {
            "Handles an existing game path below a base prefix",
            "wss://example.com/api/v1/game/ExistingGame",
            "IgnoredGame",
            "wss://example.com/api/v1/game/ExistingGame/voice"
        }
    };

    for (std::vector<DeriveUriTestCase>::const_iterator it = tests.begin();
         it != tests.end();
         ++it) {
        require_equal(*it);
    }

    std::cout << "All " << tests.size()
              << " derive_uri tests passed.\n";
    return EXIT_SUCCESS;
}