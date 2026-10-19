# Breeze app

Breeze as an app on the PS5 itself, opened from the home screen and shown on the
PS5's own screen. The fan control stays in `breeze.elf`, the background payload:
an app is closed when a game starts, and the fan has to be steered while you play.
The app reads `breeze.elf`'s state the way the web panel does (127.0.0.1, panel
port), and when nothing answers it starts the `breeze.elf` it carries by sending
it to the ELF loader on port 9021, once per launch.

Built with [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
(GPL-3.0-or-later), fetched at a pinned commit; installed as a folder that
[ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) mounts.

## Build and install

In Linux or WSL, with Clang 18 and lld 18 (`apt install clang-18 lld-18 ccache
pkg-config`):

```sh
./build.sh                              # build/breeze.elf, which the app carries
app/build.sh                            # build/app/PPSA92878 and its ZIP
app/build.sh deploy <PS5 IP>            # also copy it to /data/homebrew over FTP
```

ShadowMountPlus picks up `/data/homebrew/PPSA92878` within about 15 seconds.

While the app is open the PS5 treats it like a game, so its minimum fan speed is
the in-game 23% until the app is closed.
