# neuro-sdk-websocketpp


This is a fairly barebones SDK for incorporating [Neuro-Sama API](https://github.com/VedalAI/neuro-game-sdk) into C++ projects using websocketpp.

This is a header-only library but it requires following dependencies:

[nlohmann/json](https://github.com/nlohmann/json) - heder only version can be downloaded from that repo's releases page
[zaphoyd/websocketpp](https://github.com/zaphoyd/websocketpp) - included as a submodule. This one requires Boost to be installed.

See example project in Test/RockPaperScissors.cpp for an idea of how to use this SDK.

NeuroGameClient is an abstract class that you should extend and override its handleMessage method with your game's logic. handleMessage method will get called every time Neuro sends an action.
You shouldn't use sendForceAction method directly, instead use forceAction which will block program execution until Neuro responds and prevent race conditions.
For timeout, set a value in seconds after which forceAction will throw an error. Set to -1 to wait forever.
Set URL to empty string to use NEURO_SDK_WS_URL environment variable (as required by Vedal).
If connection closes Neuro client will automatically attempt to reconnect.
Neuro client automatically connects during construction and disconnects during destruction. If you want it to stop reconnecting for any reason you need to delete the object.

