Improved frame rate for NTR wireless streaming with New 3DS (and some other misc streaming-related changes).

For use with [Snickerstream](https://github.com/RattletraPM/Snickerstream), [Chokistream](https://github.com/Eiim/Chokistream), or any [NTR](https://www.gamebrew.org/wiki/NTRViewer_3DS) compatible viewers.

## Summary

NTR is a homebrew originally made by cell9 that allows game patching, debugging, and wireless streaming on 3DS.

This fork attempts to improve the wireless streaming aspect of the homebrew. Currently it is able achieve 60 ~ 90 fps for average quality settings (at 18Mbps).

## Changes from [NTR 3.6](https://github.com/44670/NTR)

- Use up to three cores for encoding
- - Improved frame rate by around 80% to 120% compared to 3.6
- Can now switch between games and keep the stream going
- No more green tint when streaming games with RGB565 output (e.g. SMT4)
- Can now update quality setting, QoS, and screen priority settings while streaming
- New menu item in NTR menu (accessed by X + Y), can be used to change viewer's port and other settings
- Skip duplicate frames (when actual frame rate is lower than how fast NTR can encode)
- - Should lead to better frame pacing
- Stability increase
- - Should no longer cause data corruption when used with [NTRClient](https://github.com/phecdaDia/NTRClient) (specifically avoid doing Memlayout on pid 0)
- Various optimizations and updated dependencies

Features unrelated to streaming are unchanged and should continues to work with existing tools.

## Known issues

- Crash/fail to initialize on occasions when starting NTR, or when starting remote play.
- Some games are not compatible with streaming.
- When cheat plugins have been loaded, launching another game (or the same game again) would hang.
- [UWPStreamer](https://github.com/toolboc/UWPStreamer) flickers and crashes sometimes.

The issues were present in 3.6 also and I don't really know how to fix them..

## Credits

- cell9
- Nanquitas
- PabloMK7

Especially thanks to cell9 for releasing the source of NTR 3.6 making this mod possible


## Recommended setup

### Connecting 3DS to your PC

A WiFi dongle dedicated to host a hotspot for the 3DS is recommended. You can go to device manager and disable 5GHz mode for the WiFi dongle you are using for hotspot to maximize connection stability and connection speed for 3DS. If you have something like a Raspberry Pi you can connect the RPI to PC with Ethernet and run the hotspot off the RPI instead (with bridged connection). This should guarantee a connection speed up to 18Mbps without dropping too many packets.

### Viewer settings

Quality is recommended to be between 75 and 90. At 75 you'd get between 55 to 75 fps. 40 to 60 fps for quality 90. (Tested on N2DS. N3DS seems to have 10% to 15% lower streaming performance) (Assuming good WiFi connection, which you can have if you use a dedicated 2.4GHz only hotspot)

QoS can be up to 18 for around 18Mbps. Higher values will cause more packet drops.

Priority settings is up to your personal preference.
