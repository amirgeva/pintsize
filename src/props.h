#pragma once
// The objects that furnish each environment. Sizes are in world pixels, at roughly 6 px per cm,
// so a car is a few centimetres long and a dinner plate is about 170 px across.
#include "common.h"

// name, reach (bounding radius), halfLen and halfWid (capsule used when scattering),
// height (how far its shadow falls), solid (cars bounce off it)
#define PROP_LIST(X)                          \
    /* Kitchen */                             \
    X(Plate,          84,   0,  84,  6, true) \
    X(BreakfastPlate, 84,   0,  84,  6, true) \
    X(Bowl,           58,   0,  58, 14, true) \
    X(Mug,            48,   0,  30, 20, true) \
    X(Glass,          24,   0,  24, 24, true) \
    X(BottleStand,    26,   0,  26, 40, true) \
    X(BottleLying,   100,  72,  26, 14, true) \
    X(Fork,           80,  60,  14,  3, true) \
    X(Knife,          76,  62,   9,  3, true) \
    X(Spoon,          74,  50,  16,  3, true) \
    X(Napkin,         74,   0,  60,  0, false)\
    X(Apple,          30,   0,  26, 14, true) \
    X(Orange,         26,   0,  26, 14, true) \
    X(Banana,         80,  50,  16, 10, true) \
    X(SaltShaker,     14,   0,  14, 22, true) \
    X(PepperMill,     20,   0,  20, 30, true) \
    X(ButterDish,     68,  25,  38,  8, true) \
    X(Toast,          48,   0,  40,  4, true) \
    X(JamJar,         28,   0,  28, 22, true) \
    X(EggCup,         20,   0,  20, 16, true) \
    X(Teapot,        102,  40,  56, 30, true) \
    X(CerealBox,     110,  30,  62, 16, true) \
    X(JuiceSpill,     72,   0,  70,  0, false)\
    X(MilkSpill,      72,   0,  70,  0, false)\
    X(JamSpill,       72,   0,  70,  0, false)\
    X(CoffeeSpill,    72,   0,  70,  0, false)\
    X(Crumbs,         44,   0,  40,  0, false)\
    X(CuttingBoard,  198,  70, 100,  0, false)\
    X(Placemat,      170,  45,  95,  0, false)\
    X(CerealRing,      6,   0,   5,  1, false)\
    /* Woodworking bench */                   \
    X(Hammer,        112,  70,  38,  8, true) \
    X(HandSaw,       196, 120,  50,  3, true) \
    X(Chisel,        102,  85,  14,  5, true) \
    X(Screwdriver,    88,  70,  14,  6, true) \
    X(Plane,         116,  70,  32, 16, true) \
    X(Clamp,          90,  25,  54, 10, true) \
    X(TapeMeasure,   200,   0,  40, 12, true) \
    X(TrySquare,     136,  70,  40,  3, true) \
    X(PaintCan,       56,   0,  54, 26, true) \
    X(WoodBlock,      65,  30,  30, 12, true) \
    X(Plank,         175, 145,  28,  8, true) \
    X(PaintBrush,     92,  70,  18,  5, true) \
    X(Drill,         122,  60,  50, 14, true) \
    X(Sandpaper,      80,  25,  48,  0, false)\
    X(Sawdust,        92,   0,  90,  0, false)\
    X(Shavings,       40,   0,  38,  2, false)\
    X(Screw,          20,  10,   5,  1, false)\
    X(Nail,           18,  10,   4,  1, false)\
    X(GlueSpill,      72,   0,  70,  0, false)\
    X(Vise,           92,  25,  56, 22, true) \
    X(Pencil,         96,  85,   6,  4, true) \
    /* Office desk */                         \
    X(Book,          122,  35,  72, 10, true) \
    X(BookStack,     132,  40,  80, 20, true) \
    X(Notebook,      105,  30,  62,  0, false)\
    X(PaperSheet,    110,  30,  65,  0, false)\
    X(Eraser,         33,  15,  16,  6, true) \
    X(Ruler,         102,  90,  14,  1, false)\
    X(PaperClip,      18,  10,   6,  1, false)\
    X(Stapler,        65,  45,  18, 10, true) \
    X(PenCup,         42,   0,  42, 34, true) \
    X(Calculator,    102,  35,  56,  6, true) \
    X(Scissors,      100,  50,  30,  4, true) \
    X(InkBottle,      24,   0,  24, 18, true) \
    X(InkSpill,       72,   0,  70,  0, false)\
    X(StickyNote,     57,   0,  42,  0, false)\
    X(Keyboard,      235, 140,  82,  8, true) \
    X(Mouse,          34,  14,  20,  8, true) \
    X(LampBase,       70,   0,  70, 12, true) \
    /* Kitchen sink */                        \
    X(Sponge,         72,  25,  40, 14, true) \
    X(DishBrush,     104,  70,  30,  8, true) \
    X(Saucepan,      264,   0,  90, 20, true) \
    X(Suds,           62,   0,  60,  0, false)\
    X(SudsPuff,        8,   0,   7,  1, false)\
    X(Drain,          30,   0,  30,  0, false)\
    /* Sandbox */                             \
    X(Bucket,         62,   0,  62, 30, true) \
    X(Spade,         112,  70,  26,  6, true) \
    X(Rake,          112,  70,  40,  6, true) \
    X(Castle,        110,  30,  80, 28, true) \
    X(Tower,          40,   0,  40, 34, true) \
    X(Pebble,         24,   0,  22,  8, true) \
    X(Rock,           46,   0,  44, 18, true) \
    X(Ball,           58,   0,  58, 36, true) \
    X(Stick,         114, 100,   8,  4, true) \
    X(Leaf,           24,   0,  20,  0, false)\
    X(SmallPebble,     6,   0,   5,  1, false)\
    /* Freezer */                             \
    X(IceCube,        36,   0,  30, 16, true) \
    X(IceTray,       140,  85,  50, 10, true) \
    X(PeaBag,        120,  30,  76, 14, true) \
    X(Tub,            84,   0,  84, 22, true) \
    X(FoodBox,       148,  40,  88, 18, true) \
    X(Popsicle,       80,  30,  22,  8, true) \
    X(FishStick,      36,  22,  12,  6, true) \
    X(FrostPatch,     72,   0,  70,  0, false)\
    X(IcePatch,       72,   0,  70,  0, false)\
    X(Pea,             5,   0,   4,  1, false)

enum class PropKind : uint8_t {
#define X(name, reach, halfLen, halfWid, height, solid) name,
    PROP_LIST(X)
#undef X
    Count
};

struct PropInfo {
    float reach, halfLen, halfWid, height;
    bool solid;
};
const PropInfo& propInfo(PropKind k);

struct PropPixel {
    uint32_t color = 0;
    float alpha = 1;
    bool solid = false;        // starts as the prop's default; a painter can clear it for one part
    bool setSurface = false;   // flat props can change how the ground behaves
    Surface surface = Surface::Road;
};

// Shades one point of a prop in its own coordinates. Returns false where the prop does not cover
// that point. `light` is the horizontal part of the light direction, in the same coordinates.
bool shadeProp(PropKind k, float u, float v, int seed, Vec2 light, PropPixel& out);
