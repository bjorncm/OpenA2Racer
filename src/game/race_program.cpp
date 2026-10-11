#include "race_program.hpp"

#include "keys.hpp"
#include "launcher.hpp"
#include "menu_program.hpp"
#include "race_assets.hpp"
#include "race_audio.hpp"
#include "race_hud.hpp"
#include "race_renderer.hpp"

#include "opena2/menu_data.hpp"
#include "opena2/race/hud.hpp"
#include "opena2/race/pickups.hpp"
#include "opena2/race/player_car.hpp"
#include "opena2/race/race_sound.hpp"
#include "opena2/race/race_state.hpp"
#include "opena2/race/scene.hpp"
#include "opena2/race/stage.hpp"
#include "opena2/race/track.hpp"
#include "opena2/race/traffic.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace opena2::game {
namespace {

// A static object type with its mesh and texture loaded once
// (the table at 0x457c58 keeps the mesh frame and texture id).
struct LoadedType {
    bool loaded = false;
    int mesh = -1;
    int texture = -1;
    float squaredRadius = 0.0F;
};

// The stage objects the options ask for (load_a2_ini): DETAIL 1 loads the
// scenery ("init static objects"), HORIZON the sky ("set horizon object").
struct StageOptions {
    bool scenery = true;
    bool sky = true;
};

StageOptions stageOptions(const Infio& infio)
{
    return {infio.detail == 1, infio.horizon != 0};
}

// A moving object drawn with its own matrix: a bonus or a pion.
struct DynamicObject {
    int type = 0;
    race::Vec3 position;
    race::Mat4 matrix;
};

class Stage {
public:
    bool load(Runtime& runtime, int stage, const StageOptions& options, std::string& error)
    {
        if (!assets_.open(runtime.directory, error) || !renderer_.initialize(error)) {
            return false;
        }
        viewDistance_ = runtime.raceViewDistance;
        const auto& info = race::stageTable()[static_cast<std::size_t>(stage)];

        // "Loading Pallette": the dashboard picture's palette is the game palette.
        const IndexedImage* palette = assets_.texture("surscrn.pcx");
        if (palette == nullptr) {
            error = "SURSCRN.PCX is missing.";
            return false;
        }
        renderer_.setPalette(palette->paletteRgb);
        background_ = renderer_.addTexture(assets_.texture("back.pcx"));

        const auto trackBytes = assets_.bytes(info.trackFile, error);
        auto track = trackBytes ? race::decodeTrack(*trackBytes, error) : std::nullopt;
        if (!track) {
            return false;
        }
        track_ = std::move(*track);
        pieces_ = race::buildRoadPieces(track_);
        buildRoad();

        const auto sceneryBytes = assets_.bytes(info.sceneryFile, error);
        const auto scenery = sceneryBytes ? race::decodeScenery(*sceneryBytes, error) : std::nullopt;
        if (!scenery) {
            return false;
        }
        // The sky (FUN_004077dc type 0); its callback keeps it at the player.
        sky_ = options.sky;
        if (sky_) {
            loadType(race::static_object::sky);
        }
        // FUN_00411296: the start and finish banners, 60 units out on side
        // 1, mode 2 forward and mode 1 backward.
        const int pieceCount = static_cast<int>(pieces_.size());
        const auto banner = [this](int type, int piece, int mode) {
            const race::SceneryRecord record{piece, 1, mode, type, 60};
            objects_.push_back(race::placeStaticObject(pieces_, record, loadType(type).squaredRadius));
        };
        if (info.forward) {
            banner(race::static_object::start, 16, 2);
            banner(race::static_object::finish, pieceCount - 15, 2);
        } else {
            banner(race::static_object::finish, 15, 1);
            banner(race::static_object::start, pieceCount - 16, 1);
        }
        if (options.scenery) {
            for (const auto& record : *scenery) {
                const LoadedType& type = loadType(record.type);
                objects_.push_back(race::placeStaticObject(pieces_, record, type.squaredRadius));
            }
        }
        // The pickup and pion objects.
        for (const int type : {race::static_object::roadworks2, race::static_object::speedCamera,
                               race::static_object::money, race::static_object::fuel,
                               race::static_object::time}) {
            loadType(type);
        }
        SDL_Log("Race stage %d: %zu road pieces, %zu scenery objects", stage, pieces_.size(),
                objects_.size());
        return true;
    }

    const std::vector<race::RoadPiece>& pieces() const { return pieces_; }

    // `heading` is the player's heading, which places the view window and
    // wedge; the sky follows the player's x (callback 0x408370: z is 0).
    RaceRenderer::Frame frame(const race::CameraView& camera, int heading, float playerX,
                              const RaceRenderer::Object* car = nullptr,
                              std::span<const DynamicObject> dynamics = {},
                              std::span<const RaceRenderer::Object> cars = {}) const
    {
        RaceRenderer::Frame frame;
        frame.background = background_;
        frame.view = race::viewMatrix(camera.position, camera.pitch, camera.heading, camera.roll);

        const race::ViewWindow window = race::viewWindow(camera.position, heading, viewDistance_);
        const race::ViewWedge wedge(camera.position, heading);

        // Object table order: the sky was created first.
        if (sky_) {
            const LoadedType& sky = types_[race::static_object::sky];
            frame.objects.push_back({sky.mesh, sky.texture,
                                     race::Mat4::translation({playerX, 0.0F, 0.0F})});
        }
        for (const auto& object : objects_) {
            if (!window.contains(object.position.x, object.position.z)) {
                continue;
            }
            const LoadedType& type = types_[static_cast<std::size_t>(object.type)];
            frame.objects.push_back({type.mesh, type.texture, race::staticObjectMatrix(object)});
        }
        for (const auto& object : dynamics) {
            if (!window.contains(object.position.x, object.position.z)) {
                continue;
            }
            const LoadedType& type = types_[static_cast<std::size_t>(object.type)];
            frame.objects.push_back({type.mesh, type.texture, object.matrix});
        }
        // The computer cars and the place signs, by their translation.
        for (const auto& object : cars) {
            if (window.contains(object.world.m[3][0], object.world.m[3][2])) {
                frame.objects.push_back(object);
            }
        }

        const auto visible = race::findVisibleSegments(pieces_, wedge, window);
        for (std::size_t i = visible.first; i < visible.last; ++i) {
            if (visible.flags[i]) {
                frame.segments.push_back(i);
            }
        }
        if (car != nullptr) {
            frame.objects.push_back(*car);
        }
        return frame;
    }

    RaceRenderer& renderer() { return renderer_; }
    RaceAssets& assets() { return assets_; }

    // A car type of the table at 0x457978 with its mesh, its "k_" skin and
    // its box (create_opponent: load_object_mesh once per type).
    struct LoadedCarType {
        bool loaded = false;
        int mesh = -1;
        int texture = -1;
        race::CarBox box;
    };
    const LoadedCarType& loadCarType(int index)
    {
        LoadedCarType& type = carTypes_[static_cast<std::size_t>(index)];
        if (!type.loaded) {
            type.loaded = true;
            const auto& entry = race::carTypes()[static_cast<std::size_t>(index)];
            if (const D3DMesh* mesh = assets_.mesh(entry.mesh)) {
                type.mesh = renderer_.addMesh(*mesh);
                std::vector<race::Vec3> vertices;
                for (const auto& v : mesh->vertices) {
                    vertices.push_back({v.position[0], v.position[1], v.position[2]});
                }
                type.box = race::CarBox::fromVertices(vertices);
            }
            type.texture = renderer_.addTexture(assets_.texture(entry.skin));
        }
        return type;
    }
    // The place signs number1..6 (static types 0x26..0x2b).
    const LoadedType& placeSignType(int place) { return loadType(race::static_object::number1 + place - 1); }

    // "Initializing player object" (FUN_00401f79): bmw.d3d with the skin of
    // car type 19 (PTR_s_bmw_d3d_00457bd8).
    struct LoadedCar {
        int mesh = -1;
        int texture = -1;
        std::vector<race::Vec3> vertices;
    };
    LoadedCar loadPlayerCar()
    {
        const auto& type = race::carTypes()[19];
        LoadedCar car;
        if (const D3DMesh* mesh = assets_.mesh(type.mesh)) {
            car.mesh = renderer_.addMesh(*mesh);
            for (const auto& v : mesh->vertices) {
                car.vertices.push_back({v.position[0], v.position[1], v.position[2]});
            }
        }
        car.texture = renderer_.addTexture(assets_.texture(type.skin));
        return car;
    }

private:
    void buildRoad()
    {
        // d3d.lib [WEG] COMPLETE is 0 without that file: segments use half
        // of the verge vertices.
        std::vector<race::RoadSegmentMesh> segments;
        for (std::size_t i = 0; i + 1 < pieces_.size(); ++i) {
            const auto& point = track_.points[i + 1];
            segments.push_back(race::buildRoadSegmentMesh(pieces_[i], pieces_[i + 1],
                                                          point.leftTiles.size() / 2,
                                                          point.leftTiles, point.rightTiles));
        }
        renderer_.setRoad(segments, renderer_.addTexture(assets_.texture("2baans.pcx")),
                          renderer_.addTexture(assets_.texture("grass2.pcx")));
    }

    const LoadedType& loadType(int index)
    {
        LoadedType& type = types_[static_cast<std::size_t>(index)];
        if (!type.loaded) {
            type.loaded = true;
            const auto& entry = race::staticObjectTypes()[static_cast<std::size_t>(index)];
            if (const D3DMesh* mesh = assets_.mesh(entry.mesh)) {
                type.mesh = renderer_.addMesh(*mesh);
                std::vector<race::Vec3> vertices;
                for (const auto& v : mesh->vertices) {
                    vertices.push_back({v.position[0], v.position[1], v.position[2]});
                }
                type.squaredRadius = race::squaredRadius(vertices);
            }
            type.texture = renderer_.addTexture(assets_.texture(entry.texture));
        }
        return type;
    }

    RaceAssets assets_;
    RaceRenderer renderer_;
    race::Track track_;
    std::vector<race::RoadPiece> pieces_;
    std::array<LoadedType, 48> types_{};
    std::array<LoadedCarType, 23> carTypes_{};
    std::vector<race::StaticObject> objects_;
    int background_ = -1;
    bool sky_ = true;
    float viewDistance_ = 1.0F;
};

// load_a2_ini (0x40c2f4) reads A2.INI from the game folder only when it
// exists; without it the port uses the values of the CD's A2.INI.
race::RaceSettings loadRaceSettings(const Runtime& runtime)
{
    std::string error;
    const auto path = runtime.directory.findFile("A2.INI", error);
    const auto bytes = path ? readFileBytes(*path) : std::nullopt;
    if (!bytes) {
        return {};
    }
    return race::RaceSettings::fromIni(parseIniValues(std::string(bytes->begin(), bytes->end())));
}

// The player's device (load_control_keys, 0x402ac7): BEDIENING 0 keyboard
// with the INFIO keys, 1 joypad, 2 wheel. The joysticks are SDL gamepads
// (the first one is joystick 1).
class CarControls {
public:
    explicit CarControls(const Infio& infio)
        : device_(infio.control)
        , left_(infio.keys[0])
        , right_(infio.keys[1])
        , gas_(infio.keys[2])
        , brake_(infio.keys[3])
    {
        int count = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
            if (count > 0) {
                gamepad_ = SDL_OpenGamepad(ids[0]);
            }
            SDL_free(ids);
        }
    }
    ~CarControls()
    {
        if (gamepad_ != nullptr) {
            SDL_CloseGamepad(gamepad_);
        }
    }
    CarControls(const CarControls&) = delete;
    CarControls& operator=(const CarControls&) = delete;

    race::CarInput read(const Runtime& runtime) const
    {
        race::CarInput input;
        if (device_ == 0) {
            // key_is_pressed with device 1: the key, with Shift up.
            input.left = runtime.keyIsPressed(left_);
            input.right = runtime.keyIsPressed(right_);
            input.gas = runtime.keyIsPressed(gas_);
            input.brake = runtime.keyIsPressed(brake_);
            return input;
        }
        if (gamepad_ == nullptr) {
            return input;
        }
        // joyGetPosEx positions 0..65535 around a centre of 32768, with the
        // dead zone 0.1 of the centre (_DAT_00459484).
        const int x = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX) + 32768;
        const int y = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY) + 32768;
        constexpr int centre = 32768;
        constexpr int dead = centre / 10;
        const bool low = x < centre - dead || SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        const bool high = x > centre + dead || SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        const bool up = y < centre - dead || SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_UP);
        const bool down = y > centre + dead || SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        if (device_ == 1) {
            // poll_joystick: stick left, right, up and down are the slots of
            // KEY_LINKS, KEY_RECHTS, KEY_GAS and KEY_REM. The D-pad is a port
            // addition.
            input.left = low;
            input.right = high;
            input.gas = up;
            input.brake = down;
            return input;
        }
        // FUN_0041654c(1, 28, 28): the wheel position becomes the steer
        // index; the pedal axis sets gas (low) or brake (high).
        constexpr int half = 14;
        if (x < centre - dead) {
            input.wheelSteer = x / (centre / half);
        } else if (x > centre + dead) {
            input.wheelSteer = x / ((65535 - centre) / half);
        } else {
            input.wheelSteer = half;
        }
        input.left = low;
        input.right = high;
        input.gas = up;
        input.brake = down;
        return input;
    }

private:
    int device_ = 0;
    int left_ = 0;
    int right_ = 0;
    int gas_ = 0;
    int brake_ = 0;
    SDL_Gamepad* gamepad_ = nullptr;
};

race::TrafficSettings trafficSettings(const race::RaceSettings& settings)
{
    return {settings.policeCount,
            settings.civilCount,
            static_cast<float>(settings.racerSpeed),
            static_cast<float>(settings.policeSpeed),
            settings.racerRange,
            settings.racerHighSpeed,
            settings.racerLowSpeed};
}

// create_standard_opponents, FUN_00411401 (the racers on the grid at
// `racerPlaces`), FUN_00411473 (police) and the civilians.
void setUpTraffic(Stage& world, race::Traffic& traffic, const race::PlayerCar& car, bool forward,
                  int startPiece, const std::array<int, 5>& racerPlaces,
                  const race::RaceSettings& settings, race::Lcg& random)
{
    std::array<race::CarBox, 23> boxes{};
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        boxes[i] = world.loadCarType(static_cast<int>(i)).box;
    }
    traffic.setUp(world.pieces(), forward, startPiece, car, racerPlaces, boxes,
                  trafficSettings(settings), random);
    for (int place = 1; place <= 6; ++place) {
        world.placeSignType(place);
    }
    SDL_Log("Race traffic: 5 racers, %d police, %d civilians", settings.policeCount,
            settings.civilCount);
}

// The computer cars (callback 0x40654b) and the place signs.
// The other cars and the place signs over the racers, at `places`.
std::vector<RaceRenderer::Object> trafficObjects(Stage& world, const race::Traffic& traffic,
                                                 const std::array<int, 6>& places)
{
    std::vector<RaceRenderer::Object> objects;
    const auto cars = traffic.cars();
    for (std::size_t slot = 1; slot < cars.size(); ++slot) {
        const auto& type = world.loadCarType(cars[slot].carType);
        objects.push_back({type.mesh, type.texture, cars[slot].matrix});
    }
    for (int place = 1; place <= 6; ++place) {
        race::Mat4 sign;
        if (traffic.placeSign(places, place, sign)) {
            const auto& type = world.placeSignType(place);
            objects.push_back({type.mesh, type.texture, sign});
        }
    }
    return objects;
}

// Development: the rule that leads to `state`.
void forceRaceState(race::RaceRules& rules, int state, std::int32_t now)
{
    switch (state) {
    case race::race_state::finished:
        rules.checkFinish(true, now);
        break;
    case race::race_state::caught:
        rules.setState(race::race_state::caught, now);
        break;
    case race::race_state::timeOut:
        rules.tickClock(now + rules.clock());
        break;
    case race::race_state::moneyOut:
        rules.drainMoney(rules.money() + 1, now);
        break;
    case race::race_state::fuelOut:
        rules.useFuel(rules.fuel(), now);
        break;
    case race::race_state::paused:
        rules.setState(race::race_state::paused, now);
        break;
    default:
        SDL_Log("--race-state %d is not a state that can be forced", state);
        break;
    }
}

const char* stateName(int state)
{
    static constexpr std::array<const char*, 10> names{
        "racing", "fly-in", "start lights", "finished", "caught",
        "time out", "money out", "fuel out", "exit", "paused"};
    return state >= 0 && state < 10 ? names[static_cast<std::size_t>(state)] : "?";
}

// Removes the scene layer when the view ends, however it ends, and puts
// back the palette the race replaced.
struct SceneLayerGuard {
    Display& display;
    std::vector<std::uint8_t> palette = display.palette();
    ~SceneLayerGuard()
    {
        display.setSceneLayer({});
        display.setPalette(palette);
    }
};

} // namespace

Task<> runRaceView(Runtime& runtime, int stage, int startPiece)
{
    if (stage < 0 || stage > 5) {
        SDL_Log("Race stage %d does not exist.", stage);
        co_return;
    }
    Stage world;
    std::string error;
    if (!world.load(runtime, stage, {}, error)) {
        SDL_Log("Could not load race stage %d: %s", stage, error.c_str());
        co_return;
    }

    // Start on the given piece, facing along the stage's driving direction.
    // A car heading h faces (-sin h, cos h), so h = piece heading - 0x100.
    const auto& pieces = world.pieces();
    const bool forward = race::stageTable()[static_cast<std::size_t>(stage)].forward;
    const auto piece = static_cast<std::size_t>(
        std::clamp(startPiece, 0, static_cast<int>(pieces.size()) - 1));
    race::Vec3 camera = pieces[piece].centre.position;
    camera.y += 20.0F;
    float heading = static_cast<float>(pieces[piece].heading - 0x100 + (forward ? 0 : 0x200));
    float pitch = -20.0F;

    runtime.display.discard(Surface::front);
    const auto freeView = [](const race::Vec3& position, float pitch, int heading) {
        return race::CameraView{position, static_cast<int>(std::lround(pitch)), heading, 0};
    };
    // The pickups and roadworks, from a fixed seed.
    race::Lcg random;
    race::Pickups pickups;
    pickups.placeRoadworks(pieces, forward, random);
    pickups.placeBonuses(pieces, forward, random);
    pickups.runCallbacks();
    std::vector<DynamicObject> dynamics;
    for (const auto& bonus : pickups.bonuses()) {
        dynamics.push_back({bonus.objectType, bonus.position, bonus.matrix});
    }
    for (const auto& pion : pickups.pions()) {
        dynamics.push_back({pion.objectType, pion.position, pion.matrix});
    }
    // The traffic where the race starts it, standing.
    race::PlayerCar player;
    const int pieceCount = static_cast<int>(pieces.size());
    player.placeOnGrid(pieces, forward, forward ? 15 : pieceCount - 15, 6);
    race::Traffic traffic;
    setUpTraffic(world, traffic, player, forward, forward ? 15 : pieceCount - 15, {1, 2, 3, 4, 5},
                 loadRaceSettings(runtime), random);
    traffic.runCallbacks(pieces);
    const std::vector<RaceRenderer::Object> carObjects = trafficObjects(world, traffic, traffic.places(world.pieces()));
    RaceRenderer::Frame frame = world.frame(freeView(camera, pitch, static_cast<int>(heading)),
                                            static_cast<int>(heading), camera.x, nullptr, dynamics,
                                            carObjects);
    runtime.display.setSceneLayer([&world, &frame](const ScreenTransform& transform, int, int pixelHeight) {
        world.renderer().draw(frame, transform, pixelHeight);
    });
    SceneLayerGuard guard{runtime.display};

    std::uint64_t last = runtime.now();
    while (!runtime.quitRequested && !runtime.keyDown(vk::escape)) {
        co_await runtime.frame();
        const float seconds = static_cast<float>(runtime.now() - last) / 1000.0F;
        last = runtime.now();
        const float boost = runtime.keyDown(vk::shift) ? 4.0F : 1.0F;
        const float move = 600.0F * boost * seconds;
        const float turn = 256.0F * seconds;
        // Headings grow to the left: heading h faces (-sin h, cos h).
        if (runtime.keyDown(vk::left)) heading += turn;
        if (runtime.keyDown(vk::right)) heading -= turn;
        if (runtime.keyDown(vk::home)) pitch += turn / 2.0F;
        if (runtime.keyDown(vk::end)) pitch -= turn / 2.0F;
        const int h = static_cast<int>(std::lround(heading));
        const float fx = -race::angleSin(h);
        const float fz = race::angleCos(h);
        if (runtime.keyDown(vk::up)) camera += race::Vec3{fx, 0.0F, fz} * move;
        if (runtime.keyDown(vk::down)) camera += race::Vec3{fx, 0.0F, fz} * -move;
        if (runtime.keyDown('A')) camera.y += move / 2.0F;
        if (runtime.keyDown('Z')) camera.y -= move / 2.0F;
        frame = world.frame(freeView(camera, pitch, h), h, camera.x, nullptr, dynamics, carObjects);
    }
}

Task<> runRace(Runtime& runtime, int forceState)
{
    // "setting up program": load_infio (0x410688) and load_a2_ini (0x40c2f4).
    Infio infio = readInfio(runtime);
    race::ensureValidPlacements(infio);
    const int stage = infio.stage >= 0 && infio.stage <= 5 ? infio.stage : 0;
    const race::RaceSettings settings = loadRaceSettings(runtime);

    Stage world;
    std::string error;
    if (!world.load(runtime, stage, stageOptions(infio), error)) {
        // The original returns without writing INFIO.INI when start-up fails.
        SDL_Log("Could not load race stage %d: %s", stage, error.c_str());
        co_return;
    }
    const auto& pieces = world.pieces();
    const int pieceCount = static_cast<int>(pieces.size());
    const auto& stageInfo = race::stageTable()[static_cast<std::size_t>(stage)];
    const bool forward = stageInfo.forward;
    SceneLayerGuard guard{runtime.display};

    // "Loading Pallette": the HUD draws in the game palette too.
    if (const IndexedImage* palette = world.assets().texture("surscrn.pcx")) {
        runtime.display.setPalette(palette->paletteRgb);
    }
    // "Initializing dashboard" and "Initializing radar".
    const bool dashboard = race::hudDashboard(infio);
    RaceHud hud;
    if (!hud.load(runtime.display, runtime.directory, world.assets(), dashboard, error)) {
        SDL_Log("Could not load the race HUD: %s", error.c_str());
        co_return;
    }
    // DAT_00458c24: the radar starts on; F5 turns it off and on.
    bool radar = true;
    bool f5Down = false;

    // clock_seconds (0x4290b9): wall seconds, rounded at 500 ms.
    const auto seconds = [&runtime] {
        return static_cast<std::int32_t>((runtime.now() + 500) / 1000);
    };
    // create_main_window seeds lcg_rand with the wall clock in seconds
    // (0x4290a9), so each race differs.
    race::Lcg random{static_cast<std::uint32_t>(std::time(nullptr))};
    race::RaceRules rules(infio, settings.stageSeconds[static_cast<std::size_t>(stage)],
                          settings.bonusSeconds, seconds());
    // The places until the opponents rank the racers: the starting places.
    rules.setPlaces({infio.position, infio.racerPosition[0], infio.racerPosition[1],
                     infio.racerPosition[2], infio.racerPosition[3], infio.racerPosition[4]});

    // "Initializing player object" and stage set-up (FUN_00411296): the
    // grid is behind piece 15 of the driving direction; the player starts at
    // the place of the previous stage.
    const Stage::LoadedCar carMesh = world.loadPlayerCar();
    race::PlayerCar car;
    car.setBox(carMesh.vertices);
    car.setMass(race::carTypes()[19].mass);
    const int startPiece = forward ? 15 : pieceCount - 15;
    car.placeOnGrid(pieces, forward, startPiece, infio.position);
    race::RaceCamera camera;
    camera.startFlyIn(car);
    std::array<int, 5> racerPlaces{};
    for (std::size_t i = 0; i < racerPlaces.size(); ++i) {
        racerPlaces[i] = infio.racerPosition[i];
    }
    race::Traffic traffic;
    setUpTraffic(world, traffic, car, forward, startPiece, racerPlaces, settings, random);
    // "set all objects": random_pion_chunk places the roadworks.
    race::Pickups pickups;
    pickups.placeRoadworks(pieces, forward, random);
    rules.takeEvents();
    SDL_Log("Race camera position 1");

    CarControls controls(infio);
    // Development: OPENA2_RACE_AUTOPILOT=<speed> holds the gas below that
    // speed (units per frame, up to 15), straight ahead.
    const char* autopilotValue = SDL_getenv("OPENA2_RACE_AUTOPILOT");
    const float autopilot = autopilotValue != nullptr ? static_cast<float>(SDL_atof(autopilotValue)) : 0.0F;
    runtime.display.discard(Surface::front);
    RaceRenderer::Frame frame;
    runtime.display.setSceneLayer([&world, &frame](const ScreenTransform& transform, int, int pixelHeight) {
        world.renderer().draw(frame, transform, pixelHeight);
    });

    // "Starting music".
    RaceAudio audio(runtime.sound, world.assets(), race::SoundSettings::fromInfio(infio));
    audio.startMusic(stageInfo.music);
    // "Dump all bonus stuff".
    pickups.placeBonuses(pieces, forward, random);
    // DAT_004729bc: the engine voices start once the clock runs.
    bool engineStarted = false;

    // The original runs its logic once per rendered frame without a limiter;
    // the port runs it at a fixed rate, by default 20 frames a second, a 1997
    // PC's rate (chosen by eye from the pickup spin, kept after play), and
    // renders every frame. The start window can change the rate; everything
    // moves a step per tick, so it is also the game speed.
    const double tickMilliseconds = 1000.0 / std::clamp(runtime.raceTickRate, 1, 1000);
    double nextTick = static_cast<double>(runtime.now());
    const auto handleEvents = [&rules, &car, &camera, &audio] {
        const race::RaceEvents events = rules.takeEvents();
        for (const int sound : events.sounds) {
            audio.effect(sound);
        }
        if (events.stopCar) {
            car.stop();
        }
        if (events.camera != 0) {
            camera.setPosition(events.camera);
        }
    };

    int lastState = -1;
    while (rules.state() != race::race_state::exit && !runtime.quitRequested) {
        co_await runtime.frame();
        while (static_cast<double>(runtime.now()) >= nextTick &&
               rules.state() != race::race_state::exit) {
            nextTick += tickMilliseconds;
            const std::int32_t now = seconds();
            const race::RaceKeys keys{runtime.keyIsPressed(vk::enter),
                                      runtime.keyIsPressed(vk::space), runtime.keyIsPressed('P')};

            if (rules.state() == race::race_state::paused) {
                // The loop dispatches state 9 until it ends; P must be
                // released before the race resumes.
                if (keys.p || keys.enter || keys.space) {
                    while (runtime.keyDown('P') && !runtime.quitRequested) {
                        co_await runtime.frame();
                    }
                    rules.dispatch(keys, seconds());
                    nextTick = static_cast<double>(runtime.now());
                }
                continue;
            }

            // init_player_opponent (0x407193): input, movement, then the
            // camera update, which also follows the road.
            // FUN_004034bb: start_race_sounds once the race clock runs.
            if (!engineStarted && rules.clockRunning()) {
                audio.startEngine();
                engineStarted = true;
            }
            race::CarEvents carEvents;
            race::CarInput input = controls.read(runtime);
            if (autopilot > 0.0F) {
                input.gas = car.speed() < autopilot;
            }
            car.control(input, rules.playerControl(), carEvents);
            // FUN_00403273: the gear change, then the voices while the clock runs.
            if (carEvents.bandChanged) {
                audio.changeGear(carEvents.bandUp);
            }
            if (carEvents.speedStep && rules.clockRunning()) {
                audio.updateEngine(car.soundIndex(), carEvents.gas);
            }
            race::TrafficEvents trafficEvents;
            traffic.step(pieces, car, rules.clockRunning(), rules.state(), random, trafficEvents);
            carEvents.bump = carEvents.bump || trafficEvents.player.bump;
            car.followRoad(pieces);
            if (rules.state() == race::race_state::flyIn) {
                camera.setHeight(rules.flyIn());
            }
            camera.update(car);
            if (carEvents.enteredPiece) {
                rules.useFuel(1, now);
            }
            if (carEvents.backwards) {
                audio.effect(0x2f);
            }
            if (carEvents.bump) {
                audio.bump(11, 0);
            }
            // The other cars' sounds, by their Manhattan distance to the player.
            const auto distanceTo = [&car](const race::Vec3& p) {
                const race::Vec3 d = p - car.position();
                return static_cast<int>(std::nearbyint(std::abs(d.z) + std::abs(d.y) + std::abs(d.x)));
            };
            for (const auto& crash : trafficEvents.crashes) {
                audio.bump(10, distanceTo(crash.position),
                           static_cast<int>(std::nearbyint(crash.speed * 25.0F)));
            }
            for (const auto& wall : trafficEvents.wallBumps) {
                audio.bump(11, distanceTo(wall));
            }
            audio.updateTraffic(traffic.engineSlots());
            if (trafficEvents.caught) {
                rules.setState(race::race_state::caught, now);
            }
            rules.setPlaces(traffic.places(pieces));
            for (const int racer : trafficEvents.finished) {
                rules.setFinishTime(racer, now);
            }
            // The object callbacks of the draw pass, then bonus_dispatch
            // and the pion update (FUN_00413e88) for the player.
            pickups.runCallbacks();
            traffic.runCallbacks(pieces);
            switch (pickups.takeBonus(car.piece(), car.lane())) {
            case race::bonus_type::time:
                rules.addTimeBonus();
                break;
            case race::bonus_type::fuel:
                rules.addFuel();
                break;
            case race::bonus_type::money:
                rules.addMoney();
                break;
            default:
                break;
            }
            {
                // Racer +0x18 and +0x20 are the direction (cos, sin) of
                // heading + 0x100, times the speed +0x10.
                const float speed = car.speed();
                const race::PionHitter hitter{
                    car.piece(), car.lane(), car.position(),
                    {-race::angleSin(car.heading()) * speed, 0.0F, race::angleCos(car.heading()) * speed}};
                race::Vec3 bumpAt;
                if (pickups.updatePions(hitter, random, bumpAt)) {
                    audio.bump(9, distanceTo(bumpAt));
                }
                // FUN_00413e88: then for every racer and police car.
                const auto cars = traffic.cars();
                for (std::size_t slot = 1; slot < cars.size(); ++slot) {
                    const race::TrafficCar& other = cars[slot];
                    if (other.kind != race::driver_kind::racer && other.kind != race::driver_kind::police) {
                        continue;
                    }
                    const race::PionHitter otherHitter{other.body.piece, other.body.lane,
                                                       other.body.position,
                                                       traffic.velocity(static_cast<int>(slot))};
                    if (pickups.updatePions(otherHitter, random, bumpAt)) {
                        audio.bump(9, distanceTo(bumpAt));
                    }
                }
            }

            rules.tickClock(now);
            rules.dispatch(keys, now);
            if (forceState >= 0 && rules.state() == race::race_state::racing) {
                forceRaceState(rules, forceState, now);
                forceState = -1;
            }

            if (keys.p && rules.state() == race::race_state::racing) {
                while (runtime.keyDown('P') && !runtime.quitRequested) {
                    co_await runtime.frame();
                }
                rules.setState(race::race_state::paused, seconds());
                nextTick = static_cast<double>(runtime.now());
            }
            rules.checkFinish(race::raceOver(car.piece(), pieceCount, forward), now);
            if (runtime.keyIsPressed(vk::f1)) {
                camera.setPosition(2);
            }
            if (runtime.keyIsPressed(vk::f2)) {
                camera.setPosition(1);
            }
            // F5 toggles the radar. The original toggles on every pass the
            // key is held; the port toggles once per press.
            const bool f5 = runtime.keyIsPressed(vk::f5);
            if (f5 && !f5Down) {
                radar = !radar;
            }
            f5Down = f5;
            if (runtime.keyIsPressed(vk::escape)) {
                rules.escape();
            }
            // A debug chord: Z, O, Ctrl and F12 give the high camera.
            if (runtime.keyIsPressed('Z') && runtime.keyIsPressed('O') &&
                runtime.keyIsPressed(vk::control) && runtime.keyIsPressed(vk::f12)) {
                camera.setPosition(4);
            }
            handleEvents();
        }

        if (rules.state() != lastState) {
            lastState = rules.state();
            SDL_Log("Race state %d (%s)", lastState, stateName(lastState));
        }
        const RaceRenderer::Object carObject{carMesh.mesh, carMesh.texture, car.worldMatrix()};
        std::vector<DynamicObject> dynamics;
        for (const auto& bonus : pickups.bonuses()) {
            if (bonus.type != race::bonus_type::none) {
                dynamics.push_back({bonus.objectType, bonus.position, bonus.matrix});
            }
        }
        for (const auto& pion : pickups.pions()) {
            if (pion.active) {
                dynamics.push_back({pion.objectType, pion.position, pion.matrix});
            }
        }
        const auto cars = traffic.cars();
        const std::vector<RaceRenderer::Object> carObjects = trafficObjects(world, traffic, rules.shownPlaces());
        frame = world.frame(camera.view(), car.heading(), car.position().x,
                            camera.carVisible() ? &carObject : nullptr, dynamics, carObjects);
        frame.viewportHeight = static_cast<float>(race::viewportHeight(dashboard));

        // radar_dispatch and draw_hud.
        race::HudValues values;
        values.speed = car.speed();
        values.soundIndex = car.soundIndex();
        values.steerIndex = car.steerTarget();
        values.fuel = rules.fuel();
        values.clock = rules.clock();
        values.money = rules.money();
        values.place = rules.shownPlace();
        runtime.display.discard(Surface::front);
        // draw_radar_pointer: the cars in the lanes of the 20 pieces around
        // the player.
        std::vector<race::RadarCar> radarCars;
        for (int piece = car.piece() - 10; piece < car.piece() + 10; ++piece) {
            if (piece < 0 || piece >= pieceCount - 1) {
                continue;
            }
            for (int lane = 0; lane < 11; ++lane) {
                const int slot = traffic.laneEntry(piece, lane);
                if (slot > 0) {
                    const auto& other = cars[static_cast<std::size_t>(slot)];
                    radarCars.push_back({other.body.position, other.kind});
                }
            }
        }
        hud.draw(values, radar, car.position(), car.heading(), radarCars);
        hud.drawBlits(race::stateBlits(dashboard, rules.state(), rules.startStep(), rules.fine(),
                                       rules.finishFrame()));
    }

    // "Check if finisched OK" (0x401c30) and save_infio (0x4109da).
    rules.writeResult(infio, random);
    writeInfio(runtime, infio);
    SDL_Log("Race over: finished %d, end reason %d, time %d, place %d, money %d",
            infio.raceFinished, infio.finishedBy, infio.time, infio.position, infio.money);
}

} // namespace opena2::game
