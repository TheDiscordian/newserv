# newserv (modified for DiscoNet)<img align="right" src="s-newserv.png" />

[newserv](https://github.com/fuzziqersoftware/newserv) is a game server and proxy for Phantasy Star Online (PSO). Check there for the full README.

This version of the codebase contains some modifications and [a script](run_newserv.py) to enable events that change automatically, and PSO2-style emergency quests (elimination quests). The script also runs an HTTP server which can be used as a registration server. The registration server is currently technically unstable (I don't figure out serials correctly, but it does provide working ones), but it should work fine for most small servers.