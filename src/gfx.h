#pragma once
#include "race.h"
#include <SDL.h>
#include <vector>

SDL_Color driverColor(int i);

class Gfx {
public:
    explicit Gfx(SDL_Renderer* r);
    ~Gfx();

    void setTrack(const Track& t);
    void resetFx();
    void updateFx(const Race& race);
    void drawRace(const Race& race);

    // align: 0 left, 1 centre, 2 right
    void text(int x, int y, const char* s, SDL_Color c, int scale = 1, int align = 0);
    void fill(int x, int y, int w, int h, SDL_Color c);
    void clear(SDL_Color c);
    void drawCar(VehicleKind k, int color, int x, int y, float heading, float scale = 1.0f, int alpha = 255);
    void drawTrackPreview(const TrackDef& def, SDL_Rect area, SDL_Color c);

private:
    struct Particle { Vec2 pos, vel; float life, maxLife; SDL_Color color; int size; };
    void buildFont();
    void buildCars();
    void freeChunks();
    void spawn(Vec2 p, Vec2 v, float life, SDL_Color c, int size);
    void burst(Vec2 p, int n, float speed, float life, SDL_Color c, int size);
    SDL_Texture* carTexture(VehicleKind k, int color) const { return cars_[(int)k * NUM_DRIVERS + color]; }

    SDL_Renderer* r_;
    SDL_Texture* font_ = nullptr;
    std::vector<SDL_Texture*> cars_;
    std::vector<SDL_Texture*> chunks_;
    int chunkCols_ = 0, chunkRows_ = 0;
    std::vector<Particle> particles_;
    std::vector<Vec2> skids_;
    size_t skidHead_ = 0;
    std::mt19937 rng_{7};
};
