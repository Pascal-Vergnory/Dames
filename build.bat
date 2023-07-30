@echo Compile the checker engine with an SDL2-based GUI
@echo -----------------------------------------------
@gcc src\bin_to_h.c -o bin_to_h.exe
@bin_to_h resources\OptimusPrinceps.ttf src\font_ttf.h
@bin_to_h resources\Checker_Pieces.svg src\pieces_svg.h
@del bin_to_h.exe

@gcc src/dames.c src/engine.c SDL2_image.dll SDL2_ttf.dll libfreetype-6.dll -o dames.exe -Wall -Wextra -Wpedantic -Wimplicit-fallthrough=0 -lmingw32 -lSDL2main -lSDL2 -O3 -s

@del src\font_ttf.h
@del src\pieces_svg.h

@echo.
