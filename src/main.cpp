#include "game.h"
#include <SDL.h>
#include <SDL_main.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// --shot <file.bmp> <menu|race:N|h2h:N|map:N> [ticks]
// Renders one frame off-screen and saves it; used to check visuals without a display.
// map:N saves the whole world for track N rather than a game frame.
static int runShot(int argc, char** argv) {
    if (argc < 4) return 2;
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    SDL_Init(SDL_INIT_VIDEO);

    if (std::strncmp(argv[3], "map:", 4) == 0) {
        Track track(allTracks()[std::atoi(argv[3] + 4)]);
        SDL_Surface* world = SDL_CreateRGBSurfaceWithFormatFrom(
            const_cast<uint32_t*>(track.pixels().data()), track.width(), track.height(), 32, track.width() * 4, SDL_PIXELFORMAT_ARGB8888);
        SDL_SaveBMP(world, argv[2]);
        SDL_FreeSurface(world);
        SDL_Quit();
        return 0;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, VIEW_W, VIEW_H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    if (!renderer) { std::fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }

    Audio silent;
    {
        Game game(renderer, &silent);
        std::string what = argv[3];
        int ticks = argc > 4 ? std::atoi(argv[4]) : 600;
        if (what == "menu") {
            game.showMainMenu();
        } else {
            size_t colon = what.find(':');
            int track = colon == std::string::npos ? 0 : std::atoi(what.c_str() + colon + 1);
            game.startDemo(track, what.rfind("h2h", 0) == 0 ? RaceMode::HeadToHead : RaceMode::Circuit);
        }
        for (int i = 0; i < ticks; ++i) game.tick();
        game.draw();
        SDL_RenderPresent(renderer);
        SDL_SaveBMP(surface, argv[2]);
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    SDL_Quit();
    return 0;
}

// --record <dir> <title|menu|race:N|h2h:N> <skipTicks> <frames> <ticksPerFrame>
// Like --shot, but saves a sequence <dir>/f0000.bmp... for building gameplay clips.
static int runRecord(int argc, char** argv) {
    if (argc < 7) return 2;
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, VIEW_W, VIEW_H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    if (!renderer) { std::fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }

    Audio silent;
    {
        Game game(renderer, &silent);
        std::string what = argv[3];
        int skip = std::atoi(argv[4]), frames = std::atoi(argv[5]), step = std::atoi(argv[6]);
        if (what == "title") {
            // the game opens on the title screen
        } else if (what == "menu") {
            game.showMainMenu();
        } else {
            size_t colon = what.find(':');
            int track = colon == std::string::npos ? 0 : std::atoi(what.c_str() + colon + 1);
            game.startDemo(track, what.rfind("h2h", 0) == 0 ? RaceMode::HeadToHead : RaceMode::Circuit);
        }
        for (int i = 0; i < skip; ++i) game.tick();
        for (int f = 0; f < frames; ++f) {
            for (int i = 0; i < step; ++i) game.tick();
            game.draw();
            SDL_RenderPresent(renderer);
            char path[1024];
            std::snprintf(path, sizeof path, "%s/f%04d.bmp", argv[2], f);
            SDL_SaveBMP(surface, path);
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    SDL_Quit();
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--shot") == 0) return runShot(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--record") == 0) return runRecord(argc, argv);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pint-Size Prix", SDL_GetError(), nullptr);
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    SDL_Window* window = SDL_CreateWindow("Pint-Size Prix", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          VIEW_W * 3, VIEW_H * 3, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pint-Size Prix", SDL_GetError(), nullptr);
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pint-Size Prix", SDL_GetError(), nullptr);
        return 1;
    }
    SDL_Texture* frame = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, VIEW_W, VIEW_H);

    Audio audio;
    audio.init();  // the game still runs if no sound device is available
    {
        Game game(renderer, &audio);

        // LAN play from the command line:
        //   --host | --join ADDRESS[:PORT]          go straight to the lobby
        //   --lag MS --jitter MS --loss PERCENT     simulate a poor connection (F9 cycles presets in game)
        NetConditions sim;
        std::string join;
        bool host = false;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            const bool more = i + 1 < argc;
            if (a == "--host") host = true;
            else if (a == "--join" && more) join = argv[++i];
            else if (a == "--lag" && more) sim.lagMs = (float)std::atof(argv[++i]);
            else if (a == "--jitter" && more) sim.jitterMs = (float)std::atof(argv[++i]);
            else if (a == "--loss" && more) sim.lossPercent = (float)std::atof(argv[++i]);
        }
        game.setNetConditions(sim);
        if (host) game.startLanHost();
        else if (!join.empty()) game.startLanJoin(join);

        const double tickSeconds = DT;
        const double freq = (double)SDL_GetPerformanceFrequency();
        Uint64 last = SDL_GetPerformanceCounter();
        double accumulator = 0;
        bool fullscreen = false;

        while (!game.wantsQuit()) {
            SDL_Event e;
            bool closed = false;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) closed = true;
                else if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.sym == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                } else {
                    game.handleEvent(e);
                }
            }
            if (closed) break;

            Uint64 now = SDL_GetPerformanceCounter();
            accumulator += (double)(now - last) / freq;
            last = now;
            if (accumulator > 0.1) accumulator = 0.1;
            while (accumulator >= tickSeconds) {
                game.tick();
                accumulator -= tickSeconds;
            }
            game.frame();

            SDL_SetRenderTarget(renderer, frame);
            game.draw();
            SDL_SetRenderTarget(renderer, nullptr);

            int ww, wh;
            SDL_GetRendererOutputSize(renderer, &ww, &wh);
            double scale = std::min((double)ww / VIEW_W, (double)wh / VIEW_H);
            SDL_Rect dst;
            dst.w = (int)(VIEW_W * scale);
            dst.h = (int)(VIEW_H * scale);
            dst.x = (ww - dst.w) / 2;
            dst.y = (wh - dst.h) / 2;
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, frame, nullptr, &dst);
            SDL_RenderPresent(renderer);
        }
    }

    SDL_DestroyTexture(frame);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
