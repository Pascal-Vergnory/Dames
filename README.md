# dames

I have derived a checker engine from my chess engine! Dames is a small program written in C with a graphical user interface. You can play against it, or you can even let it play against it-self!

## checker features 

- 5 squares per row times 10 rows, plus a few "dummy" squares to handle the borders
- Entire board copy at each new move, so undoing a move is very simple
- Negamax search with alpha beta pruning
- Iterative deepening
- Transposition table (using Pengy hash)

## Pre-requisites to build

Under Windows, msys64 and MINGW64 must be installed.
Under Linux, gcc must be installed

Under the two platforms, the GUI uses the SDL2 graphical library

## Building dames

To build the game, use `build.bat` on Windows or `./build` on Linux without any argument.

## Using dames (Linux), or dames.exe (Windows)

On Windows, the program needs to have access to the 4 following graphical DLLs. These DLLs can be in the same directory as the program or in a directory listed in the PATH environment variable:
- SDL2.dll
- SDL2_image.dll
- SDL2_ttf.dll
- libfreetype-6.dll

Use the mouse to move a piece, use the left arrow and right arrow keys to respectively undo and redo a move.

You can play against the engine or you can start the engine twice (in two separate CMD or terminal windows but in the same directory) and make both instances play against each other! For this, click on "PLAY" on one of the instances.
