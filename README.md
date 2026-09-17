Improved frame rate for NTR wireless streaming with New 3DS/New 2DS (and some other misc streaming-related changes).

For use with [Snickerstream](https://github.com/RattletraPM/Snickerstream), [Chokistream](https://github.com/Eiim/Chokistream), or any other [NTR](https://www.gamebrew.org/wiki/NTRViewer_3DS) compatible viewers.

[NTRViewer-HR](https://github.com/xzn/ntrviewer-hr) is a companion PC viewer for Windows/Linux/macOS containing some additional features.

## Summary

NTR is a homebrew originally made by cell9 that allows game patching, debugging, and wireless streaming on New 3DS.

This fork attempts to improve the wireless streaming aspect of the homebrew. Currently it is able achieve 60 ~ 90 fps for average quality settings (at 18Mbps).

## Changes from [NTR 3.6](https://github.com/44670/NTR) and [NTR 3.6.1](https://github.com/PabloMK7/NTR)

- Use up to three cores for encoding. Improved frame rate by around 80% to 120% compared to 3.6.
- Can now switch between games and keep the stream going.
- No more green tint when streaming games with RGB565 output (e.g. SMT4).
- Can now update quality setting, QoS, and screen priority settings while streaming.
- New menu item in NTR menu (accessed by X + Y), can be used to change viewer's port and other settings.
- - Has NFC patching functionality built-in, to avoid USUM hang when using Remote Play. (Alternative to Input Redirection/Debugger in Rosalina.)
- - Can start Remote Play from the menu and change viewer's IP and port.
- - QTM (3D head-tracking) disable patch, to improve Remote Play performance on New 3DS (New 2DS unaffected).
- - Chroma subsampling option for customizing image quality at the cost of frame-rate.
- - Some other misc. settings.
- Skip duplicate frames (when actual frame rate is lower than how fast NTR can encode). Should lead to better frame pacing.
- Various optimizations and updated dependencies.
- Fixed race conditions in startup unhooking code. Should no longer crash/fail to initialize randomly when starting NTR, or when starting remote play.

Removed all other features aside from streaming and PLG loading:

- Screenshots, debugger, and night color are available with Luma3DS/Rosalina.
- Real-time save/load doesn't save/load handles (and has no way of doing so) and generally doesn't work very well. I can add this back in if needed.

## Known issues

- Some games are not compatible with streaming.
- When cheat plugins have been loaded, launching another game (or the same game again) would hang for certain plugins.
- [UWPStreamer](https://github.com/toolboc/UWPStreamer) flickers and crashes sometimes.
- [NTRView for Wii U](https://github.com/yawut/ntrview-wiiu) flickers especially when core count is set to more than one.

## Credits

- cell9
- Nanquitas
- PabloMK7
- denzi-gh (For DSP audio capture support)

Especially thanks to cell9 for releasing the source of NTR 3.6 making this mod possible

## Usage guide

See https://wiki.hacks.guide/wiki/3DS:Wireless_streaming for more info on setup and instructions.

Once NTR-HR is running, press X+Y on your New 3DS/New 2DS to open NTR-HR menu, which contains additional options.

## Recommended setup

### Fix crashing on start/crashing during remote play

Make sure you are on the latest Luma3DS and ideally the latest firmware. NTR-HR will crash if Luma3DS is too old. Remote play will also not function if firmware is too low.

For remote play on New 3DS, make sure to use the non-Mode3 version. Mode3 is only for Old 3DS for extended memory games and has less options. Note that remote play performance will be very low on Old 3DS (3~7 FPS).

### Connecting 3DS to your PC

A dedicated WiFi AP connecting your 3DS wireless and no other wireless device, with your PC connected with Ethernet, should yield best result. An example would be a Raspberry Pi connected to PC with Ethernet, and bridged WiFi just for 3DS.

Otherwise a WiFi dongle dedicated to host a hotspot for the 3DS would work as well. Note that on Windows it's unavoidable that WiFi-scanning will causes periodic lag spike if you connect with this method. On Linux you can play with config to prevent all other background uses of the WiFi dongle.

If your 3DS is connected to a router which also has other WiFi devices connected (other consoles, phones, etc.) lag will be unavoidable in general.

### Viewer settings

Quality is recommended to be between 75 and 90. At 75 you'd get between 55 to 75 fps. 40 to 60 fps for quality 90. (Tested on N2DS. N3DS seems to have 10% to 15% lower streaming performance) (Assuming good WiFi connection, which you can have if you use a dedicated 2.4GHz only hotspot)

QoS can be up to 18 for around 18Mbps. Higher values will cause more packet drops.

Priority settings is up to your personal preference.

### Audio capture

DPS audio capture can be enabled in NTRViewer-HR's Debug window. CSND audio capture is not supported. Use a 3.5 mm audio aux cable to connect from your 3DS' headphone jack to your PC's line-in as a workaround for audio capture if desired.

### New 3DS/New 2DS only

Old (original) 3DS and 2DS are **NOT** supported. Only the New variants has the CPU necessary to stream at a usable frame-rate.

### DS games, DSiWare, and GBA games

DS games, DSiWare, and GBA games are **NOT** supported, as 3DS' CPU run under a compatibility mode that loses all the computing power normal 3DS would have.

### Fix USUM and other Pokemon games hanging when loading a save

The recommended way is to enable Input Redirection under Misc. settings in Rosalina, or enable Debugger, also in Rosalina (default keybind for Luma3DS/Rosalina menu is L+Down+Select).

(If you WiFi is cut somehow while above options are enabled, your 3DS will likely freeze. To unfreeze your console, go into Rosalina menu again and disable IR/Debugger respectively.)

Alternatively, apply NFC patch in the NTR-HR menu. This method however is not compatible with Reliable Stream, which will automatically be disabled.

### Improve performance on New 3DS to be closer to New 2DS

QTM disable option in the NTR-HR menu can be used to increase Remote Play performance (max frame-rate) for New 3DS by around 10%~15%, making it closer to New 2DS. This should have no effect on gameplay. This will **NOT** fix lag due to WiFi environment. It only give more CPU time for Remote Play on your 3DS, while lag is mostly due to WiFi setup.

## License

GPLv2 as the original NTR 3.6 by cell9.

Various third party code are available in more permissive licenses. See individual files and subfolders for more info.
