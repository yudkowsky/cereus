Compilation requires Vulkan SDK - I use 1.4.321.1, but the vulkan code is all very orthodox, so it 'should' work on any version.
To compile: navigate to ../cereus/code and run build_cereus.bat. It will create a folder ../build_cereus in the same repo as cereus is in (this is essentially just the handmade hero setup copied) - in there, there will be a data folder and a .exe; double click on the exe to start into the overworld.

When in the game:

WASD for movement
Z: undo
R: restart
Q: interact: When standing on those fancy blocks, press Q to go into that level. Once that level is complete, you'll unlock the next level. You can press ESC to go out of a level you've had enough of.
F: if you want to skip a level. press F while standing on a level instead of Q to 'solve' the level
TAB: go to alternate camera (only works in overworld right now, no other levels need an alternative camera. it's also a bit janky in the overworld, but it should work)

debug binds:
Y: toggle the debug text that's visible in the top left
T: draw camera boundary lines (shows camera chunks in overworld, and level boundary in other levels)
E: toggle rendering of 'real' models (right now just suzanne replacing the win blocks - i.e., I can load models, but I haven't done any art yet)

mode toggle:
0: game mode (standard)
1: place / break mode: able to fly around, space / shift to go up / down, otherwise WASD.
there's a bunch of binds in place/break mode, but most probably aren't so interesting. if you want to fly around and then reset the camera to where it started press backspace.
