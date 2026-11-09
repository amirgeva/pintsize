// Vehicles, environments and the six courses.
#include "track.h"

const VehicleSpec& vehicleSpec(VehicleKind k) {
    //          accel  top  brake  turn  grip   mass   rad  halfL  halfW  rough  gun
    static const VehicleSpec specs[] = {
        {"COUPE",   360, 235, 520, 5.4f,  8.5f, 1.0f,  7.5f, 10.5f, 7.0f, 1.0f,  false},
        {"BUGGY",   400, 225, 500, 5.6f,  6.5f, 1.1f,  8.0f,  9.5f, 8.0f, 0.45f, false},
        {"FORMULA", 460, 290, 640, 5.2f, 11.0f, 0.85f, 8.0f, 13.0f, 7.0f, 1.3f,  false},
        {"SKIFF",   300, 240, 300, 4.4f,  2.6f, 1.0f,  8.0f, 12.5f, 6.0f, 1.0f,  false},
        {"TANK",    290, 190, 500, 3.8f, 16.0f, 2.2f,  9.0f, 11.5f, 9.5f, 0.2f,  true},
        {"SLED",    320, 230, 340, 5.0f,  3.6f, 0.9f,  7.5f, 12.0f, 7.0f, 1.0f,  false},
    };
    return specs[(int)k];
}

const ThemeInfo& themeInfo(ThemeId t) {
    static const ThemeInfo themes[] = {
        {"KITCHEN TABLE",     Surface::Road,  Surface::Void,  Surface::Void, 1.0f,  {214, 190, 150}},
        {"WOODWORKING BENCH", Surface::Road,  Surface::Void,  Surface::Void, 1.0f,  {232, 204, 150}},
        {"FREEZER DRAWER",    Surface::Rough, Surface::Void,  Surface::Wall, 0.75f, {240, 248, 252}},
        {"KITCHEN SINK",      Surface::Road,  Surface::Void,  Surface::Wall, 1.0f,  {236, 246, 252}},
        {"OFFICE DESK",       Surface::Road,  Surface::Void,  Surface::Void, 1.0f,  {180, 170, 160}},
        {"BACKYARD SANDBOX",  Surface::Rough, Surface::Water, Surface::Wall, 1.0f,  {222, 190, 132}},
    };
    return themes[(int)t];
}

const ThemeDressing& themeDressing(ThemeId t) {
    using K = PropKind;
    static const ThemeDressing dressings[] = {
        // Kitchen: the route is edged with a trail of spilled cereal.
        {{{K::Crumbs, 5, 0.7f, 1.2f, false}, {K::Napkin, 1, 0.8f, 1.0f, false}, {K::Apple, 2, 1, 1, false},
          {K::Orange, 2, 1, 1, false}, {K::Banana, 1, 1, 1, false}, {K::Mug, 2, 1, 1, false}, {K::Glass, 2, 1, 1, false},
          {K::SaltShaker, 1, 1, 1, false}, {K::JamJar, 1, 1, 1, false}, {K::EggCup, 1, 1, 1, false}, {K::Toast, 2, 1, 1, false},
          {K::Fork, 1, 1, 1, false}, {K::Knife, 1, 1, 1, false}, {K::Spoon, 1, 1, 1, false}, {K::Plate, 2, 0.75f, 1.0f, false},
          {K::BreakfastPlate, 1, 0.9f, 1.0f, false}, {K::Bowl, 1, 1, 1, false}, {K::ButterDish, 1, 1, 1, false},
          {K::BottleStand, 1, 1, 1, false}},
         1200, true, K::CerealRing, 15},
        // Workshop: snapped chalk lines mark the route, so there is no border piece.
        {{{K::Screw, 6, 1, 1, false}, {K::Nail, 6, 1, 1, false}, {K::Shavings, 6, 0.8f, 1.3f, false}, {K::Sawdust, 2, 0.6f, 1.0f, false},
          {K::WoodBlock, 3, 0.8f, 1.1f, false}, {K::Chisel, 2, 1, 1, false}, {K::Screwdriver, 2, 1, 1, false}, {K::Pencil, 2, 1, 1, false},
          {K::Clamp, 1, 1, 1, false}, {K::TrySquare, 1, 1, 1, false}, {K::PaintCan, 1, 1, 1, false}, {K::PaintBrush, 1, 1, 1, false},
          {K::Sandpaper, 1, 1, 1, false}},
         1200, false, K::Nail, 0},
        // Freezer: frozen peas line the scraped lane.
        {{{K::IceCube, 5, 0.8f, 1.1f, false}, {K::FishStick, 3, 1, 1, false}, {K::Popsicle, 2, 1, 1, false}, {K::Pea, 6, 1, 1.2f, false},
          {K::FrostPatch, 4, 0.8f, 1.4f, false}, {K::FoodBox, 1, 0.8f, 1.0f, false}, {K::PeaBag, 1, 0.8f, 1.0f, false},
          {K::IceTray, 1, 1, 1, false}, {K::Tub, 1, 0.7f, 0.9f, false}},
         1200, true, K::Pea, 14},
        // Sink: lines of soap bubbles.
        {{{K::SudsPuff, 8, 1, 1.5f, false}, {K::Suds, 2, 0.6f, 1.0f, false}, {K::Sponge, 1, 0.9f, 1, false}, {K::Mug, 2, 1, 1, false},
          {K::Glass, 2, 1, 1, false}, {K::Bowl, 1, 1, 1, false}, {K::Plate, 1, 0.8f, 1.0f, false}, {K::DishBrush, 1, 1, 1, false},
          {K::Fork, 2, 1, 1, true}, {K::Knife, 2, 1, 1, true}, {K::Spoon, 2, 1, 1, true}},
         1200, true, K::SudsPuff, 12},
        // Desk: highlighter strips mark the route, so there is no border piece.
        {{{K::PaperClip, 5, 1, 1, false}, {K::Eraser, 2, 1, 1, false}, {K::Pencil, 3, 1, 1, false}, {K::StickyNote, 3, 1, 1, false},
          {K::PaperSheet, 2, 1, 1, false}, {K::Notebook, 1, 1, 1, false}, {K::Book, 2, 0.9f, 1, false}, {K::BookStack, 1, 0.9f, 1, false},
          {K::Stapler, 1, 1, 1, false}, {K::Scissors, 1, 1, 1, false}, {K::Calculator, 1, 1, 1, false}, {K::InkBottle, 1, 1, 1, false},
          {K::PenCup, 1, 1, 1, false}, {K::Mouse, 1, 1, 1, false}, {K::Ruler, 1, 1, 1, false}},
         1200, false, K::PaperClip, 0},
        // Sandbox: a packed-down lane edged with small pebbles.
        {{{K::Pebble, 5, 0.8f, 1.2f, false}, {K::SmallPebble, 8, 1, 1.4f, false}, {K::Leaf, 5, 0.8f, 1.2f, false}, {K::Rock, 2, 0.8f, 1.1f, false},
          {K::Stick, 2, 1, 1, false}, {K::Bucket, 1, 1, 1, false}, {K::Spade, 1, 1, 1, false}, {K::Rake, 1, 1, 1, false}, {K::Ball, 1, 0.8f, 1, false},
          {K::Tower, 1, 0.8f, 1.1f, false}},
         1200, true, K::SmallPebble, 13},
    };
    return dressings[(int)t];
}

namespace {
PropPlacement P(PropKind k, float x, float y, float rotDeg = 0, float scale = 1, int seed = 0) {
    return {k, false, x, y, rotDeg, scale, seed, false};
}
PropPlacement R(PropKind k, float at, float offset, float rotDeg = 0, float scale = 1, int seed = 0) {
    return {k, true, at, offset, rotDeg, scale, seed, false};
}
PropPlacement Under(PropKind k, float at, float offset, float rotDeg = 0) {
    return {k, true, at, offset, rotDeg, 1.0f, 0, true};
}

// A laid place: mat, plate, cutlery either side and a glass at the top corner.
void placeSetting(std::vector<PropPlacement>& out, float x, float y, float rotDeg, int seed) {
    using K = PropKind;
    float a = rotDeg * PI / 180.0f, c = std::cos(a), s = std::sin(a);
    auto at = [&](K k, float u, float v, float extraDeg, int sd) {
        out.push_back(P(k, x + u * c - v * s, y + u * s + v * c, rotDeg + extraDeg, 1.0f, sd));
    };
    at(K::Placemat, 0, 0, 0, seed);
    at(seed % 2 ? K::BreakfastPlate : K::Plate, 0, 0, 0, seed);
    at(K::Fork, -112, 0, -90, 0);
    at(K::Knife, 112, 0, -90, 0);
    at(K::Glass, 112, -82, 0, seed);
}

std::vector<TrackDef> buildTracks() {
    using K = PropKind;
    std::vector<TrackDef> tracks;

    {   // Kitchen table: no jumps, but a table edge to fall from.
        TrackDef t{"SUNDAY BRUNCH", ThemeId::Kitchen, VehicleKind::Coupe, 58, 3,
                   {{300,250},{800,200},{1300,260},{1650,400},{1700,750},{1450,950},{1150,850},
                    {900,650},{650,800},{600,1100},{300,1150},{150,900},{130,500}},
                   {}, {}, 11};
        auto& p = t.props;
        p = {R(K::Fork, 0.75f, -34, 15), R(K::Mug, 1.35f, 30, 40, 1, 0), R(K::Glass, 1.7f, -32, 0, 1, 1),
             R(K::JuiceSpill, 2.4f, 0, 0, 0.8f, 3), R(K::BottleLying, 3.35f, 100, -90, 1, 0), R(K::Plate, 5.5f, -100, 0, 1, 0),
             R(K::Apple, 7.25f, 24, 0, 1, 1), R(K::Orange, 7.6f, -28), R(K::Knife, 8.5f, 32, -15),
             R(K::JamSpill, 9.4f, -8, 0, 0.7f, 2), R(K::Spoon, 10.6f, -32, 10), R(K::SaltShaker, 11.6f, -24),
             R(K::PepperMill, 11.75f, 26), R(K::Toast, 12.45f, 30, 20),
             P(K::Teapot, 1000, 470, -30, 1, 0), P(K::CerealBox, 700, 470, 25), P(K::Bowl, 860, 520),
             P(K::CuttingBoard, 1120, 1170, 4), P(K::Banana, 1090, 1150, 30), P(K::Apple, 1200, 1190, 0, 1, 1)};
        placeSetting(p, 1330, 530, 20, 0);
        placeSetting(p, 390, 690, -10, 1);
        placeSetting(p, 1560, 1140, 8, 2);
        tracks.push_back(std::move(t));
    }
    {   // Woodworking bench: one jump over the tool well.
        TrackDef t{"SAWDUST SPRINT", ThemeId::Workshop, VehicleKind::Buggy, 56, 3,
                   {{300,300},{700,180},{1100,300},{1000,620},{1350,700},{1750,520},{1950,800},
                    {1750,1150},{1300,1200},{1050,1000},{700,1150},{350,1200},{150,900},{400,650},{170,500}},
                   {{7.5f, 56}}, {}, 22};
        t.props = {R(K::Hammer, 0.8f, 95, -90), R(K::Chisel, 1.6f, 34, 10), R(K::WoodBlock, 2.6f, -30, 30),
                   R(K::Sawdust, 3.5f, 0, 0, 0.7f, 1), R(K::Screwdriver, 5.4f, -30, -20, 1, 1), R(K::GlueSpill, 6.4f, 6, 0, 0.65f, 2),
                   R(K::Clamp, 8.5f, 110, 0), R(K::Pencil, 9.4f, -30, 15, 1, 1), R(K::TapeMeasure, 4.4f, 95, 90),
                   R(K::Sandpaper, 11.5f, 10, 30, 0.9f), R(K::PaintCan, 12.6f, 95, 0, 1, 1), R(K::WoodBlock, 13.5f, 30, -15, 0.9f, 1),
                   P(K::HandSaw, 700, 520, 15), P(K::Plane, 1530, 920, -20), P(K::Drill, 700, 880, 40),
                   P(K::Plank, 1400, 320, 10), P(K::PaintCan, 1650, 220, 0, 1, 1), P(K::PaintCan, 1560, 240, 0, 0.9f, 2),
                   P(K::Clamp, 480, 950, 30), P(K::TrySquare, 880, 880, 10), P(K::Vise, 2000, 240, 90)};
        tracks.push_back(std::move(t));
    }
    {   // Freezer drawer: little grip anywhere.
        TrackDef t{"DEEP FREEZE", ThemeId::Freezer, VehicleKind::Sled, 56, 3,
                   {{900,150},{1300,230},{1600,520},{1620,900},{1330,1180},{1050,1000},{800,1250},
                    {450,1180},{200,880},{400,620},{180,400},{500,180}},
                   {}, {}, 33};
        t.props = {R(K::IceCube, 1.5f, -28, 30), R(K::Popsicle, 2.5f, 30, 10, 1, 0), R(K::IcePatch, 3.5f, 0, 0, 0.9f, 1),
                   R(K::FishStick, 3.9f, -30, 20), R(K::IceCube, 4.5f, 26, 15), R(K::Popsicle, 6.5f, -30, -15, 1, 1),
                   R(K::IceCube, 7.5f, 28, 45), R(K::IcePatch, 9.5f, 0, 0, 0.8f, 2), R(K::FishStick, 8.5f, 30, 25), R(K::IceCube, 10.5f, -26, 10),
                   P(K::Tub, 900, 620, 0, 1, 0), P(K::FoodBox, 1250, 700, 10, 1, 1), P(K::PeaBag, 650, 900, -20), P(K::IceTray, 650, 480, 30),
                   P(K::Tub, 1100, 420, 0, 0.7f, 2), P(K::FoodBox, 1600, 210, -12, 0.8f, 2), P(K::IceTray, 1550, 1290, 10)};
        tracks.push_back(std::move(t));
    }
    {   // Kitchen sink: boats, dirty dishes and a drain to avoid.
        TrackDef t{"DISHWATER DASH", ThemeId::Sink, VehicleKind::Skiff, 62, 3,
                   {{250,400},{600,200},{1000,420},{1400,220},{1800,400},{1900,800},{1500,1050},
                    {1150,850},{800,1100},{420,1200},{150,950},{130,650}},
                   {}, {}, 44};
        t.props = {R(K::Mug, 1.5f, 30, 0, 1, 1), R(K::Glass, 2.4f, -30), R(K::Sponge, 3.5f, 96, 20), R(K::Suds, 4.4f, 0, 0, 0.8f, 1),
                   R(K::Mug, 5.5f, -30, 120, 1, 2), R(K::Bowl, 6.5f, 100), R(K::Drain, 7.9f, -36), R(K::Suds, 8.5f, 0, 0, 0.7f, 2),
                   R(K::Sponge, 9.5f, -100, -15, 1, 1), R(K::Glass, 10.5f, 30, 0, 1, 1), Under(K::Fork, 0.8f, 12, 20),
                   Under(K::Knife, 4.8f, -14, -12), Under(K::Spoon, 7.3f, 20, 30), Under(K::Fork, 11.3f, -10, -8),
                   P(K::Saucepan, 720, 640, 25), P(K::Plate, 1400, 620, 0, 1, 1), P(K::Plate, 1480, 660, 0, 0.9f, 2),
                   P(K::BottleLying, 1600, 200, 15, 1, 2), P(K::Bowl, 480, 1000), P(K::DishBrush, 1250, 1150, -30)};
        tracks.push_back(std::move(t));
    }
    {   // Office desk: fast, with two gaps to clear.
        TrackDef t{"HOMEWORK HUSTLE", ThemeId::Desk, VehicleKind::Formula, 52, 3,
                   {{200,250},{900,220},{1700,260},{1900,500},{1650,720},{1100,720},{900,920},
                    {1100,1120},{1700,1130},{1850,1350},{1600,1500},{500,1500},{200,1300},
                    {400,1000},{250,800},{180,500}},
                   {{1.4f, 90}, {10.4f, 100}}, {}, 55};
        t.props = {R(K::Eraser, 0.7f, -24, 20), R(K::Pencil, 3.4f, -24, 10, 1, 1), R(K::StickyNote, 2.5f, 8, 25, 1, 1),
                   R(K::PaperSheet, 4.6f, 0, 80), R(K::Stapler, 5.5f, 30, 10), R(K::Eraser, 6.5f, -22, -30), R(K::Mouse, 7.6f, 24, 0),
                   R(K::Pencil, 8.5f, 24, -8), R(K::InkSpill, 9.3f, 0, 0, 0.65f, 2), R(K::Scissors, 12.4f, 46, 0), R(K::Eraser, 13.5f, -24, 40),
                   R(K::Ruler, 14.4f, 0, 70),
                   P(K::Keyboard, 900, 470, 0), P(K::LampBase, 1400, 480), P(K::Book, 1400, 920, -10, 1, 1), P(K::Calculator, 1700, 930, 8),
                   P(K::BookStack, 800, 1250, 15, 1, 2), P(K::PenCup, 1300, 1300), P(K::Notebook, 560, 1180, -20, 1, 3), P(K::Mug, 1120, 1320, 0, 1, 1)};
        tracks.push_back(std::move(t));
    }
    {   // Sandbox: tanks in loose sand.
        TrackDef t{"BACKYARD BLITZ", ThemeId::Sandbox, VehicleKind::Tank, 56, 2,
                   {{300,300},{900,180},{1500,300},{1600,700},{1250,800},{1000,600},{700,800},
                    {1000,1100},{1500,1050},{1700,1300},{1300,1500},{600,1450},{200,1200},{150,700}},
                   {}, {}, 66};
        t.props = {R(K::Pebble, 0.8f, -30), R(K::Rock, 1.5f, 100), R(K::Pebble, 2.5f, 30, 0, 1, 1), R(K::Stick, 3.3f, -30, 15),
                   R(K::Rock, 4.5f, -92, 0, 1, 1), R(K::Pebble, 5.5f, 28, 0, 1, 2), R(K::Spade, 6.5f, 110, -90), R(K::Pebble, 7.5f, -30, 0, 1, 3),
                   R(K::Stick, 9.5f, 30, -12, 1, 1), R(K::Rock, 11.5f, 95, 0, 1, 2), R(K::Pebble, 12.5f, -28, 0, 1, 4),
                   P(K::Castle, 950, 420, 10), P(K::Bucket, 1300, 520), P(K::Ball, 1250, 1300), P(K::Spade, 600, 1100, 30),
                   P(K::Rake, 1100, 1290, 80), P(K::Tower, 400, 700), P(K::Tower, 420, 880, 0, 0.8f), P(K::Bucket, 1650, 980, 0, 0.8f, 1)};
        tracks.push_back(std::move(t));
    }
    return tracks;
}
}  // namespace

const std::vector<TrackDef>& allTracks() {
    static const std::vector<TrackDef> tracks = buildTracks();
    return tracks;
}
