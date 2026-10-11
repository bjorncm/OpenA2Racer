#include "opena2/d3d_mesh.hpp"
#include "opena2/indexed_archive.hpp"
#include "opena2/indexed_image.hpp"
#include "opena2/menu_data.hpp"
#include "opena2/race/collision.hpp"
#include "opena2/race/hud.hpp"
#include "opena2/race/pickups.hpp"
#include "opena2/race/player_car.hpp"
#include "opena2/race/race_sound.hpp"
#include "opena2/race/race_math.hpp"
#include "opena2/race/race_state.hpp"
#include "opena2/race/scene.hpp"
#include "opena2/race/stage.hpp"
#include "opena2/race/track.hpp"
#include "opena2/race/traffic.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace opena2::race;
using opena2::Infio;

bool require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

bool near(float a, float b, float tolerance = 1e-4F)
{
    return std::abs(a - b) <= tolerance;
}

void put32(std::vector<std::byte>& bytes, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
    }
}

void putFloat(std::vector<std::byte>& bytes, float value)
{
    put32(bytes, std::bit_cast<std::uint32_t>(value));
}

// A straight track along +z: points at z = 0, 100, 200, three lanes each.
std::vector<std::byte> straightTrack()
{
    std::vector<std::byte> bytes;
    put32(bytes, 3);
    for (int i = 0; i < 3; ++i) {
        putFloat(bytes, 0.0F);
        putFloat(bytes, static_cast<float>(i * 100));
        put32(bytes, 1);
        put32(bytes, 0);
        put32(bytes, 3);
        for (std::uint32_t tile : {8U, 16U, 24U}) {
            put32(bytes, tile + static_cast<std::uint32_t>(i));
        }
        for (std::uint32_t tile : {24U, 16U, 8U}) {
            put32(bytes, tile);
        }
    }
    return bytes;
}

bool testMath()
{
    bool ok = true;
    ok &= require(near(angleSin(256), 1.0F) && near(angleCos(0), 1.0F),
                  "1024 angle steps make a full circle.");
    ok &= require(near(angleSin(256 + 1024), 1.0F), "Angles wrap.");
    ok &= require(headingBetween(0, 0, 0, 10) == 256, "Equal x and rising z is +256.");
    ok &= require(headingBetween(0, 10, 0, 0) == -256, "Equal x and falling z is -256.");
    ok &= require(headingBetween(0, 0, 10, 0) == 0, "Along +x is 0.");
    ok &= require(headingBetween(0, 0, -10, 0) == 512, "Along -x is 512.");
    const Mat4 m = Mat4::translation({1, 2, 3});
    const Vec3 p = transformPoint({1, 1, 1}, m * Mat4::identity());
    ok &= require(p == Vec3{2, 3, 4}, "Row vectors are translated by row 3.");
    return ok;
}

bool testTrack()
{
    bool ok = true;
    std::string error;
    const auto bytes = straightTrack();
    const auto track = decodeTrack(bytes, error);
    if (!require(track.has_value(), "A valid track decodes.")) {
        return false;
    }
    ok &= require(track->points.size() == 3, "Three points.");
    ok &= require(track->points[1].leftTiles == std::vector<std::uint32_t>{9, 17, 25},
                  "Lane data A is the left verge tiles.");
    ok &= require(track->points[1].rightTiles == std::vector<std::uint32_t>{24, 16, 8},
                  "Lane data B is the right verge tiles.");
    auto shortBytes = bytes;
    shortBytes.pop_back();
    ok &= require(!decodeTrack(shortBytes, error), "A truncated track is rejected.");

    auto pieces = buildRoadPieces(*track);
    ok &= require(pieces.size() == 2, "One piece per pair of points.");
    const auto& piece = pieces[0];
    ok &= require(piece.heading == 256, "The piece heads along +z.");
    ok &= require(near(piece.up.y, 1.0F), "A flat road has an upward normal.");
    // side = up x dir = (0,1,0) x (0,0,1) = (1,0,0).
    ok &= require(near(piece.side.x, 1.0F), "The side vector points to +x.");
    ok &= require(near(piece.right.position.x, 120.0F) && near(piece.left.position.x, -120.0F),
                  "The road edges are 120 units from the centre.");
    ok &= require(near(piece.walls[1].position.y, 5.0F) && near(piece.walls[2].position.x, -5.0F),
                  "Walls are 5 units high; the reservation is 5 units wide.");
    ok &= require(piece.leftVerge.size() == 3 && near(piece.leftVerge[2].position.x, -240.0F),
                  "Verge vertices step 60 units outward.");

    computeVergeNormals(pieces[0], pieces[1], 3);
    ok &= require(near(pieces[0].rightVerge[0].normal.y, 1.0F), "Flat verge normals point up.");

    const auto corners = tileCorners(9);
    ok &= require(near(corners[0][0], 0.125F) && near(corners[0][1], 0.25F) &&
                      near(corners[2][0], 0.25F) && near(corners[2][1], 0.125F),
                  "Tile 9 is column 1, row 1 of the 8x8 atlas.");

    const auto mesh = buildRoadSegmentMesh(pieces[0], pieces[1], 3,
                                           track->points[1].leftTiles,
                                           track->points[1].rightTiles);
    // 8 road + 16 wall + 2 * (3 - 1) * 4 verge vertices.
    ok &= require(mesh.vertices.size() == 8 + 16 + 16, "Segment vertex count matches 8n + 16.");
    ok &= require(mesh.roadTriangles.size() == 4, "Four road triangles.");
    ok &= require(mesh.vergeTriangles.size() == 16 + 8, "Walls and verges triangles.");
    ok &= require(near(mesh.vertices[3].u, 1.0F) && near(mesh.vertices[3].v, 1.0F),
                  "Road texture spans one segment.");
    return ok;
}

bool testScenery()
{
    bool ok = true;
    std::vector<std::byte> bytes;
    put32(bytes, 3);
    for (const auto& r : std::vector<std::array<std::int32_t, 4>>{
             {537, 1, 0, 3}, {-1, 0, 0, 3}, {10, 0, 4, 48}}) {
        for (auto value : r) {
            put32(bytes, static_cast<std::uint32_t>(value));
        }
        putFloat(bytes, 59.6F);
    }
    std::string error;
    const auto records = decodeScenery(bytes, error);
    if (!require(records.has_value(), "A valid scenery file decodes.")) {
        return false;
    }
    ok &= require(records->size() == 1, "Records with bad fields are skipped.");
    ok &= require((*records)[0].piece == 537 && (*records)[0].type == 3 &&
                      (*records)[0].distance == 60,
                  "Record fields; the distance is rounded.");
    ok &= require(staticObjectTypes()[1].texture == std::string("50.pcx"),
                  "Static object textures come from the table.");
    ok &= require(stageTable()[3].sceneryFile == std::string("ETAPPE3.OMP") &&
                      !stageTable()[3].forward,
                  "Stage 4 drives track 3 backward with its scenery.");
    ok &= require(carTypes()[12].topSpeed == 18.0F, "Police car type.");
    return ok;
}

// A straight road along +z at x = 1000, from z = 1000, 100 units per piece.
Track longTrack(int points)
{
    Track track;
    for (int i = 0; i < points; ++i) {
        TrackPoint point;
        point.position = {1000.0F, 0.0F, 1000.0F + 100.0F * static_cast<float>(i)};
        point.leftTiles.assign(11, 0);
        point.rightTiles.assign(11, 0);
        track.points.push_back(point);
    }
    return track;
}

bool testScene()
{
    bool ok = true;
    // rotY(0x100) turns +z into +x (D3D row vectors, m[2][0] = sin).
    const Vec3 turned = transformPoint({0.0F, 0.0F, 1.0F}, rotationY(0x100));
    ok &= require(near(turned.x, 1.0F) && near(turned.z, 0.0F), "rotationY turns +z to +x.");
    const Vec3 tilted = transformPoint({0.0F, 1.0F, 0.0F}, rotationX(0x100));
    ok &= require(near(tilted.z, 1.0F), "rotationX turns +y to +z.");
    const Vec3 rolled = transformPoint({1.0F, 0.0F, 0.0F}, rotationZ(0x100));
    ok &= require(near(rolled.y, 1.0F), "rotationZ turns +x to +y.");

    // A camera with heading h looks along (-sin h, 0, cos h), which the view
    // matrix maps onto +z.
    for (const int heading : {0, 100, 300, 700}) {
        const Vec3 camera{500.0F, 30.0F, -200.0F};
        const Vec3 ahead = camera + Vec3{-angleSin(heading), 0.0F, angleCos(heading)} * 10.0F;
        const Vec3 view = transformPoint(ahead, viewMatrix(camera, 0, heading));
        ok &= require(near(view.x, 0.0F, 1e-3F) && near(view.y, 0.0F, 1e-3F) && near(view.z, 10.0F, 1e-3F),
                      "The view matrix looks along the heading.");
    }
    const Mat4 projection = projectionMatrix();
    ok &= require(projection.m[2][3] == 1.0F && projection.m[3][2] == -1.0F && projection.m[0][0] == 2.0F,
                  "Projection matrix of 0x459598.");
    const auto extent = viewportExtent(640, 480);
    ok &= require(near(extent.halfX, 1.0F / 3.0F) && near(extent.halfY, 0.25F), "Viewport extent 640x480.");

    ok &= require(viewSector(0) == 0 && viewSector(0x200) == 4 && viewSector(-0x100) == 2 &&
                      viewSector(0x100) == 6,
                  "View sectors follow the heading.");
    const ViewWindow window = viewWindow({3000.0F, 0.0F, 6000.0F}, 0);
    ok &= require(window.xMin == 1920 && window.xMax == 4080 && window.zMin == 5280 && window.zMax == 7440,
                  "View window ahead of a camera facing +z.");
    const ViewWindow corner = viewWindow({0.0F, 0.0F, 0.0F}, 0x200);
    ok &= require(corner.xMin == 0 && corner.xMax == 2160 && corner.zMin == 0 && corner.zMax == 2160,
                  "The view window is clamped at the map origin.");

    const ViewWedge wedge({1000.0F, 0.0F, 1000.0F}, 0);
    ok &= require(wedge.contains(1000, 1500) && wedge.contains(1250, 1000) && !wedge.contains(1000, 700),
                  "The view wedge opens ahead of the camera.");

    const auto pieces = buildRoadPieces(longTrack(40));
    const auto visible = findVisibleSegments(pieces, ViewWedge({1000.0F, 0.0F, 950.0F}, 0),
                                              viewWindow({1000.0F, 0.0F, 950.0F}, 0));
    ok &= require(visible.flags.size() == 38, "Segments 0 .. points - 3 are tested.");
    // Window z = (round(950 / 30) - 24 .. + 48) * 30 = 240 .. 2400, exclusive.
    ok &= require(visible.first == 0 && visible.flags[0] && visible.flags[13] && !visible.flags[14],
                  "Segments ahead inside the window are flagged.");
    ok &= require(visible.last == 13, "The window ends the flagged range.");

    ok &= require(near(roadHeightAt(pieces[3], 1050.0F, 1320.0F), 0.0F), "Flat road height.");
    const std::vector<Vec3> box{{-3.0F, 0.0F, -4.0F}, {3.0F, 2.0F, 4.0F}};
    ok &= require(near(squaredRadius(box), 25.0F), "Squared radius of the positive maxima.");

    // Piece 2 runs along +z, so its side vector is +x.
    SceneryRecord record{2, 0, 2, 13, 50};
    StaticObject object = placeStaticObject(pieces, record, 10.0F);
    ok &= require(near(object.position.x, 1050.0F) && near(object.position.z, 1200.0F),
                  "Mode 2 places the object to the right.");
    ok &= require(object.yaw == -512, "Side 0, mode 2 yaw is atan2(-sz, -sx).");
    record.mode = 3;
    object = placeStaticObject(pieces, record, 10.0F);
    ok &= require(near(object.position.x, 1000.0F - 110.0F) && object.yaw == 0,
                  "Mode 3 adds the squared radius and twice the distance to the left.");
    record.side = 1;
    record.mode = 0;
    object = placeStaticObject(pieces, record, 10.0F);
    ok &= require(near(object.position.x, 1000.0F) && object.yaw == 0x100,
                  "Side 1 with a zero z component gives 0x100.");
    const Mat4 world = staticObjectMatrix(object);
    ok &= require(near(world.m[3][0], 1000.0F) && near(world.m[3][2], 1200.0F) &&
                      near(world.m[0][0], angleCos(0x3ff)),
                  "The object matrix turns by 0x4ff - yaw.");
    return ok;
}

bool testSettings()
{
    bool ok = true;
    const auto values = opena2::parseIniValues(
        "[RACE]\nPOLICESPEED=17\nRACERSPEED=16\n[BONUS]\nTIME=2\n"
        "[TRACK]\nSTART1=3\nSTART2=5\nSTART3=9\n[TWEAK]\nPOLICE0=10\nPOLICE5=15\n"
        "RACERRANGE=2\nRACERHIGHSPEED=16\nRACERLOWSPEED=13\n");
    const auto settings = RaceSettings::fromIni(values);
    ok &= require(settings.stageSeconds == std::array<std::int32_t, 6>{180, 300, 540, 540, 300, 180},
                  "Stage times are START1..3 minutes, mirrored for backward stages.");
    ok &= require(settings.bonusSeconds == 120, "Bonus time is minutes times 60.");
    ok &= require(settings.policeSpeed == 17 && settings.racerSpeed == 16, "Race speeds.");
    ok &= require(settings.policeCount == 10 && settings.civilCount == 150,
                  "Police and civilian counts are POLICE0 and CIVIL0, 0 giving 150.");
    ok &= require(settings.racerRange == 2 && settings.racerHighSpeed == 16.0F &&
                      settings.racerLowSpeed == 13.0F,
                  "Rubber band tweaks.");
    const RaceSettings defaults;
    ok &= require(defaults.policeCount == 10 && defaults.civilCount == 150 &&
                      defaults.racerSpeed == 16 && defaults.policeSpeed == 17 &&
                      defaults.bonusSeconds == 120 && defaults.stageSeconds[2] == 540,
                  "Without A2.INI the CD's values apply.");
    const auto partial = RaceSettings::fromIni(opena2::parseIniValues("[TWEAK]\nPOLICE0=0\n"));
    ok &= require(partial.policeCount == 8 && partial.racerRange == 2 && partial.racerLowSpeed == 13.0F,
                  "POLICE0=0 becomes 8; missing keys keep the CD's values.");
    return ok;
}

// With a game folder argument: decodes the real stage files and every mesh
// and texture the race uses.
// Fly-in (35 frames), five one-second start-light steps, then racing.
bool testRaceStart()
{
    bool ok = true;
    opena2::Infio infio;
    infio.money = 0;
    RaceRules rules(infio, 600, 300, 1000);
    ok &= require(rules.money() == 1000, "money outside 1..9999 becomes 1000");
    ok &= require(rules.state() == race_state::flyIn, "race starts in the fly-in");
    ok &= require(rules.takeEvents().camera == 1, "fly-in uses camera 1");
    ok &= require(!rules.playerControl() && !rules.clockRunning(), "no control in the fly-in");

    int frames = 0;
    while (rules.state() == race_state::flyIn && frames < 100) {
        rules.dispatch({}, 1000);
        ++frames;
    }
    ok &= require(frames == 35, "fly-in lasts 350 / 10 frames");
    ok &= require(rules.takeEvents().camera == 2, "start lights use camera 2");

    std::vector<int> sounds;
    std::int32_t now = 1000;
    while (rules.state() == race_state::startLights && now < 1020) {
        rules.tickClock(now);
        rules.dispatch({}, now);
        auto events = rules.takeEvents();
        sounds.insert(sounds.end(), events.sounds.begin(), events.sounds.end());
        ++now;
    }
    ok &= require(rules.state() == race_state::racing, "start lights end in racing");
    ok &= require(now == 1006, "start lights take five seconds");
    ok &= require(sounds == std::vector<int>{0x1a, 0x1a, 0x1a, 0x1b}, "start light sounds");
    ok &= require(rules.playerControl() && rules.clockRunning(), "racing gives control");

    // The clock drains whole seconds while it runs, not while paused.
    rules.tickClock(1006);
    rules.tickClock(1016);
    ok &= require(rules.clock() == 589, "clock drains elapsed seconds");
    rules.setState(race_state::paused, 1016);
    rules.tickClock(1100);
    ok &= require(rules.clock() == 589, "paused clock holds");
    rules.dispatch({.p = true}, 1100);
    ok &= require(rules.state() == race_state::racing, "P resumes");
    rules.tickClock(1101);
    ok &= require(rules.clock() == 588, "clock resumes from the pause");

    // Only 0 and 8 may follow another state; 8 is final.
    rules.setState(race_state::caught, 1101);
    rules.setState(race_state::timeOut, 1101);
    ok &= require(rules.state() == race_state::caught, "non-zero state blocks other states");
    rules.setState(race_state::exit, 1101);
    rules.setState(race_state::racing, 1101);
    ok &= require(rules.state() == race_state::exit, "exit is sticky");
    return ok;
}

RaceRules racingRules(std::int32_t clock = 600, std::int32_t money = 1000)
{
    opena2::Infio infio;
    infio.money = money;
    RaceRules rules(infio, clock, 300, 0);
    rules.setState(race_state::racing, 0);
    rules.takeEvents();
    return rules;
}

bool testRaceEnds()
{
    bool ok = true;

    RaceRules time = racingRules(10);
    time.tickClock(10);
    auto events = time.takeEvents();
    ok &= require(time.state() == race_state::timeOut && time.endReason() == end_reason::time,
                  "clock at 0 is a time-out");
    ok &= require(events.stopCar && events.sounds == std::vector<int>{0x26}, "time-out stops the car");
    time.dispatch({.enter = true}, 16);
    ok &= require(time.state() == race_state::timeOut, "time-out ignores keys");
    time.dispatch({}, 17);
    ok &= require(time.state() == race_state::exit, "time-out holds 7 seconds");

    RaceRules fuel = racingRules();
    fuel.useFuel(299, 0);
    ok &= require(fuel.state() == race_state::racing, "fuel left");
    fuel.addFuel();
    fuel.addFuel();
    ok &= require(fuel.fuel() == 151, "fuel bonus adds 75");
    fuel.useFuel(151, 0);
    ok &= require(fuel.state() == race_state::fuelOut && fuel.endReason() == end_reason::fuel,
                  "empty tank is a fuel-out");

    // Caught: fines climb 250, 500, ...; the fine is paid when racing resumes.
    RaceRules caught = racingRules(600, 600);
    caught.setState(race_state::caught, 0);
    ok &= require(caught.fine() == 250, "first fine 250");
    caught.dispatch({}, 6);
    ok &= require(caught.state() == race_state::caught, "fine holds 7 seconds");
    caught.dispatch({}, 7);
    ok &= require(caught.state() == race_state::racing && caught.money() == 350, "fine paid");
    caught.setState(race_state::caught, 7);
    ok &= require(caught.fine() == 500, "second fine 500");
    caught.dispatch({.space = true}, 7);
    caught.takeEvents();
    ok &= require(caught.state() == race_state::moneyOut && caught.money() == 0 &&
                      caught.endReason() == end_reason::money,
                  "fine beyond the money is a money-out");
    caught.dispatch({}, 18);
    ok &= require(caught.state() == race_state::moneyOut, "money-out holds 12 seconds");
    caught.dispatch({}, 19);
    ok &= require(caught.state() == race_state::exit, "money-out ends");

    RaceRules escape = racingRules();
    escape.escape();
    ok &= require(escape.state() == race_state::exit && !escape.finished(), "Esc aborts");

    RaceRules lights = racingRules();
    lights.setState(race_state::paused, 0);
    lights.escape();
    ok &= require(lights.state() == race_state::paused, "Esc only while racing");

    ok &= require(raceOver(85, 100, true) && !raceOver(84, 100, true), "forward finish piece");
    ok &= require(raceOver(15, 100, false), "backward finish piece");
    return ok;
}

bool testRaceResult()
{
    bool ok = true;

    opena2::Infio invalid;
    invalid.position = 2;
    ensureValidPlacements(invalid);
    ok &= require(invalid.position == 6 && invalid.racerPosition[0] == 1, "duplicate places reset");

    RaceRules rules = racingRules(600, 2000);
    rules.addMoney();
    rules.takeEvents();
    rules.setPlaces({3, 1, 2, 4, 5, 6});
    // The two racers ahead crossed the line first.
    rules.setFinishTime(1, 390);
    rules.setFinishTime(2, 395);
    rules.checkFinish(true, 400);
    ok &= require(rules.state() == race_state::finished && rules.finished(), "finish");
    ok &= require(rules.takeEvents().sounds == std::vector<int>{0x2b}, "finish sound");
    rules.dispatch({}, 400);
    ok &= require(rules.takeEvents().stopCar, "result stops the car");
    rules.dispatch({}, 409);
    ok &= require(rules.state() == race_state::finished, "result holds 10 seconds");
    rules.dispatch({}, 410);
    ok &= require(rules.state() == race_state::exit, "result ends");

    opena2::Infio infio;
    infio.finishedBy = 0;
    Lcg random;
    rules.writeResult(infio, random);
    ok &= require(infio.raceFinished == 1 && infio.time == 400, "finished result");
    ok &= require(infio.position == 3 && infio.money == 2250, "place and money");
    ok &= require(infio.racerPosition[0] == 1 && infio.racerPosition[4] == 6, "racer places");
    ok &= require(infio.racerTime[0] == 390 && infio.racerTime[1] == 395, "racers ahead keep their times");
    for (std::size_t i = 2; i < infio.racerTime.size(); ++i) {
        ok &= require(infio.racerTime[i] >= 405 && infio.racerTime[i] <= 464,
                      "racer without time gets player + 5..64");
    }

    // The player stops on the line; the others pass and cross beside or
    // after them. The places stay 1..6, each once, and the HUD keeps the
    // player's place.
    RaceRules passed = racingRules(600, 2000);
    passed.setPlaces({3, 1, 2, 4, 5, 6});
    passed.setFinishTime(1, 390);
    passed.setFinishTime(2, 395);
    passed.checkFinish(true, 400);
    passed.setPlaces({4, 1, 2, 3, 5, 6});
    passed.setFinishTime(3, 401);
    passed.setPlaces({6, 1, 2, 3, 4, 5});
    passed.setFinishTime(4, 403);
    ok &= require(passed.place(0) == 6 && passed.shownPlace() == 3, "the HUD keeps the finishing place");
    ok &= require(passed.shownPlaces() == std::array<int, 6>{3, 1, 2, 4, 5, 6},
                  "the signs show the finishing places, not the live ones");
    opena2::Infio passedInfio;
    passed.writeResult(passedInfio, random);
    ok &= require(passedInfio.position == 3 && passedInfio.racerPosition[0] == 1 &&
                      passedInfio.racerPosition[1] == 2 && passedInfio.racerPosition[2] == 4 &&
                      passedInfio.racerPosition[3] == 5 && passedInfio.racerPosition[4] == 6,
                  "places follow the order over the line, then the road");

    RaceRules aborted = racingRules(600, 2000);
    aborted.addMoney();
    aborted.escape();
    opena2::Infio abortedInfio;
    abortedInfio.money = 2000;
    abortedInfio.time = 99;
    aborted.writeResult(abortedInfio, random);
    ok &= require(abortedInfio.raceFinished == 0 && abortedInfio.time == 0 &&
                      abortedInfio.money == 2000 && abortedInfio.finishedBy == 0,
                  "aborted race keeps the money");

    RaceRules timedOut = racingRules(5);
    timedOut.tickClock(5);
    opena2::Infio timedOutInfio;
    timedOut.writeResult(timedOutInfio, random);
    ok &= require(timedOutInfio.raceFinished == 0 && timedOutInfio.finishedBy == 3,
                  "time-out writes EINDE_DOOR 3");
    return ok;
}

// A straight flat track along +z with `count` points 60 apart.
std::vector<RoadPiece> longStraight(int count)
{
    std::vector<std::byte> bytes;
    put32(bytes, static_cast<std::uint32_t>(count));
    for (int i = 0; i < count; ++i) {
        putFloat(bytes, 0.0F);
        putFloat(bytes, static_cast<float>(i * 60));
        put32(bytes, 1);
        put32(bytes, 0);
        put32(bytes, 2);
        for (std::uint32_t tile : {8U, 16U, 16U, 8U}) {
            put32(bytes, tile);
        }
    }
    std::string error;
    const auto track = decodeTrack(bytes, error);
    return track ? buildRoadPieces(*track) : std::vector<RoadPiece>{};
}

bool testSpeedTable()
{
    bool ok = true;
    const auto& table = speedTable();
    ok &= require(table.size() == 151, "151 speed records");
    ok &= require(table[0].speed == -2.5F && table[0].sound == 50 && table[0].band == 0,
                  "first record: reverse");
    ok &= require(table[idleSpeedIndex].speed == 0.0F && table[idleSpeedIndex].band == 1,
                  "idle record");
    ok &= require(near(table.back().speed, 15.0F - 7.0F / 80.0F, 1e-3F) && table.back().band == 5,
                  "top record");
    for (std::size_t i = 1; i < table.size(); ++i) {
        ok &= require(table[i - 1].speed < table[i].speed, "speeds rise");
    }
    ok &= require(speedIndexFor(0.0F) == idleSpeedIndex, "speed 0 is idle");
    ok &= require(speedIndexFor(100.0F) == 150 && speedIndexFor(-10.0F) == 0, "lookup clamps");

    ok &= require(cameraPitchForHeight(0.0F) == 0, "pitch at height 0");
    ok &= require(cameraPitchForHeight(15.0F) == -41, "chase camera pitch");
    ok &= require(cameraPitchForHeight(350.0F) == -227, "fly-in pitch");
    return ok;
}

bool testPlayerCar()
{
    bool ok = true;
    const auto pieces = longStraight(60);
    if (!require(pieces.size() == 59, "straight track pieces")) {
        return false;
    }

    PlayerCar car;
    car.setBox(std::vector<Vec3>{{-5.0F, 0.0F, -10.0F}, {5.0F, 4.0F, 10.0F}});
    car.placeOnGrid(pieces, true, 15, 6);
    ok &= require(car.piece() == 12 && car.heading() == 0, "grid slot of place 6");
    ok &= require(near(car.position().x, 60.0F) && near(car.position().z, 12 * 60 + 20.0F),
                  "place 6: lane 2, 20 back from the 40 offset");

    // Gas climbs one record per frame; coasting drops one, braking four.
    CarEvents events;
    const CarInput gas{.gas = true};
    for (int i = 0; i < 10; ++i) {
        car.control(gas, true, events);
    }
    ok &= require(car.speedIndex() == idleSpeedIndex + 10, "gas steps");
    ok &= require(events.bandChanged && events.bandUp, "band change up");
    ok &= require(events.speedStep && events.gas, "gas step drives the engine voices");
    events = {};
    car.control({}, true, events);
    ok &= require(events.speedStep && !events.gas, "coasting has no gas boost");
    ok &= require(car.speedIndex() == idleSpeedIndex + 9, "coast step");
    car.control({.brake = true}, true, events);
    ok &= require(car.speedIndex() == idleSpeedIndex + 5, "brake step");
    car.control(gas, false, events);
    ok &= require(car.speedIndex() == idleSpeedIndex + 5, "no control, no step");

    // Driving straight moves along +z and changes piece.
    const float startZ = car.position().z;
    int entered = 0;
    for (int i = 0; i < 30; ++i) {
        events = {};
        car.control(gas, true, events);
        entered += events.enteredPiece ? 1 : 0;
        car.move(pieces, events);
        car.followRoad(pieces);
    }
    ok &= require(car.position().z > startZ + 30.0F && near(car.position().x, 60.0F, 0.5F),
                  "drives straight ahead");

    ok &= require(car.piece() > 12, "enters later pieces");
    ok &= require(entered > 0, "piece changes reported");

    // Steering left turns the heading up; the outer wall holds the car.
    for (int i = 0; i < 300; ++i) {
        events = {};
        car.control({.left = true, .gas = true}, true, events);
        car.move(pieces, events);
        car.followRoad(pieces);
        for (const auto& corner : car.corners()) {
            ok &= require(std::abs(corner.x) <= 120.5F, "walls hold every corner");
        }
        if (!ok) {
            break;
        }
    }
    ok &= require(car.heading() > 0, "left steering raises the heading");

    // The piece search walks to the piece under a point.
    ok &= require(findPiece(pieces, 10, 0.0F, 30 * 60 + 10.0F) == 30, "find piece ahead");
    ok &= require(findPiece(pieces, 30, 0.0F, 5 * 60 + 10.0F) == 5, "find piece behind");
    ok &= require(laneAt(pieces[5], 30.0F, 5 * 60.0F) == 3 || laneAt(pieces[5], 30.0F, 5 * 60.0F) == 7,
                  "lane of an offset point");

    RaceCamera camera;
    camera.setPosition(2);
    camera.update(car);
    ok &= require(!camera.carVisible() && camera.view().heading == car.heading(),
                  "cockpit camera");
    return ok;
}

bool testGameData(const char* folder)
{
    bool ok = true;
    std::string error;
    const auto directory = opena2::GameDataDirectory::open(folder, error);
    if (!require(directory.has_value(), "The game folder opens.")) {
        return false;
    }
    for (const auto& stage : stageTable()) {
        const auto path = directory->findFile(stage.trackFile, error);
        const auto data = path ? opena2::readFileBytes(*path) : std::nullopt;
        if (!require(data.has_value(), stage.trackFile)) {
            ok = false;
            continue;
        }
        const auto track = decodeTrack(std::as_bytes(std::span(*data)), error);
        ok &= require(track.has_value(), stage.trackFile);
        if (track) {
            std::cout << stage.trackFile << ": " << track->points.size() << " points, "
                      << buildRoadPieces(*track).size() << " pieces\n";
        }
        const auto omp = directory->findFile(stage.sceneryFile, error);
        const auto ompData = omp ? opena2::readFileBytes(*omp) : std::nullopt;
        const auto scenery =
            ompData ? decodeScenery(std::as_bytes(std::span(*ompData)), error) : std::nullopt;
        ok &= require(scenery.has_value(), stage.sceneryFile);
    }
    const auto archive = opena2::IndexedArchive::openNamed(*directory, "RCS.IMG", "RCS.IND", error);
    if (!require(archive.has_value(), "RCS archive opens.")) {
        return false;
    }
    auto find = [&](std::string name) -> std::optional<std::vector<std::byte>> {
        for (auto& c : name) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        for (std::size_t i = 0; i < archive->entryCount(); ++i) {
            std::string entry(*archive->entryName(i));
            for (auto& c : entry) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            if (entry == name) {
                return archive->readEntry(i, error);
            }
        }
        return std::nullopt;
    };
    std::vector<std::pair<std::string, std::string>> models;
    for (const auto& type : staticObjectTypes()) {
        models.emplace_back(type.mesh, type.texture);
    }
    for (const auto& type : carTypes()) {
        models.emplace_back(type.mesh, type.texture);
    }
    for (const auto& [meshName, textureName] : models) {
        const auto meshBytes = find(meshName);
        if (!require(meshBytes.has_value(), meshName.c_str())) {
            ok = false;
            continue;
        }
        opena2::decodeD3DMesh(*meshBytes, error);
        ok &= require(error.empty(), meshName.c_str());
        const auto textureBytes = find(textureName);
        ok &= require(textureBytes && opena2::decodePcx(*textureBytes, error), textureName.c_str());
    }
    for (const auto* name : {"2baans.pcx", "grass2.pcx"}) {
        const auto textureBytes = find(name);
        ok &= require(textureBytes && opena2::decodePcx(*textureBytes, error), name);
    }
    return ok;
}

} // namespace

bool testHud()
{
    bool ok = true;
    ok &= require(gaugeFrame(0.0, 8.0 / 3.0, 39) == 0, "zero gauge");
    ok &= require(gaugeFrame(-2.5, 8.0 / 3.0, 39) == 7, "gauge uses the absolute value");
    ok &= require(gaugeFrame(14.9, 8.0 / 3.0, 39) == 39, "gauge clamps");
    ok &= require(gaugeFrame(300, 1.0 / 15.0, 19) == 19, "full fuel");

    HudValues values;
    values.speed = 3.0F;
    values.soundIndex = 35;
    values.steerIndex = 14;
    values.fuel = 150;
    values.clock = 3600 + 23 * 60 + 45;
    values.money = 1234;
    values.place = 6;
    const auto framed = hudBlits(true, values);
    ok &= require(framed.size() == 5 + 2 + 7 + 5, "dashboard blit count");
    ok &= require(framed[0].image == 1 && framed[0].x == 0 && framed[0].y == 0x10b, "dashboard frame");
    ok &= require(framed[1].image == hudAnimation(4)[7] && framed[1].x == 0x2c, "wheel at the centre");
    ok &= require(framed[2].image == hudAnimation(3)[10], "fuel gauge");
    ok &= require(framed[3].image == hudAnimation(1)[8], "speedometer");
    ok &= require(framed[4].image == hudAnimation(2)[10], "tachometer");
    ok &= require(framed[5].image == hudAnimation(10)[5] && framed[5].x == 0x24e &&
                      framed[5].atOrigin && !framed[6].atOrigin,
                  "place 6 at its origin, label at its top-left");
    // Clock 01:23:45: digit 0 is image 31, digits 1..9 are 22..30.
    const std::vector<int> clockImages{31, 22, 23, 24, 25, 26};
    const std::vector<int> clockX{0x42, 0x56, 0x7b, 0x90, 0xb3, 0xc8};
    for (std::size_t i = 0; i < 6; ++i) {
        ok &= require(framed[8 + i].image == clockImages[i] && framed[8 + i].x == clockX[i] &&
                          framed[8 + i].y == 0x18 && framed[8 + i].atOrigin,
                      "clock digit");
    }
    ok &= require(framed[14].image == 326 && framed[14].y == 0x2e, "money label");
    for (std::size_t i = 0; i < 4; ++i) {
        ok &= require(framed[15 + i].image == hudAnimation(14)[i + 1] &&
                          framed[15 + i].x == 0x42 + 0x14 * static_cast<int>(i),
                      "money digit");
    }
    const auto simple = hudBlits(false, values);
    ok &= require(simple.size() == 2 + 2 + 7 + 5 && simple[0].x == 0x1e4 && simple[1].y == 0x193,
                  "simple layout");
    ok &= require(hudDashboard(Infio{}) == false, "dashboard off by default");
    Infio withDashboard;
    withDashboard.dashboard = 1;
    withDashboard.displayMode = 2;
    ok &= require(hudDashboard(withDashboard), "dashboard on");
    withDashboard.displayMode = 1;
    ok &= require(!hudDashboard(withDashboard), "low resolution has no dashboard");

    // state_dispatch overlays.
    const auto lights = stateBlits(true, 2, 3, 0, 0);
    ok &= require(lights.size() == 1 && lights[0].image == 46 && lights[0].x == 0x96 &&
                      lights[0].y == 0xa0 && !lights[0].atOrigin,
                  "start lights step 3");
    ok &= require(stateBlits(false, 2, 0, 0, 0)[0].y == 200, "start lights without the dashboard");
    const auto fine = stateBlits(false, 4, 0, 250, 0);
    ok &= require(fine.size() == 4 && fine[0].image == 302 && fine[1].x == 0x186 + 0xc &&
                      fine[1].image == hudAnimation(28)[2] && fine[1].atOrigin,
                  "fine box and three digits");
    ok &= require(stateBlits(false, 4, 0, 1250, 0).size() == 5, "fine thousands digit");
    ok &= require(stateBlits(false, 3, 0, 0, 15)[0].image == 284, "finish animation frame");
    ok &= require(stateBlits(false, 0, 0, 0, 0).empty(), "nothing while racing");

    // The radar: the dither, the player dot and a car 120 units ahead.
    const RadarLayout layout = radarLayout(false);
    const std::vector<RadarCar> cars{{{0.0F, 0.0F, 120.0F}, 3}};
    const auto pixels = radarPixels(layout, {}, 0, cars);
    const auto at = [&](int x, int y) {
        return pixels[static_cast<std::size_t>(y * layout.width + x)];
    };
    ok &= require(at(0, 0) == radarTransparent && at(1, 0) == 0 && at(0, 1) == 0 &&
                      at(1, 1) == radarTransparent,
                  "radar dither");
    ok &= require(at(0x38, 0x38) == 0x14 && at(0x3b, 0x3b) == 0x14, "player dot, 4x4");
    // Ahead is up: y = height - (centre + 10).
    ok &= require(at(0x38, 0x74 - 0x38 - 10) == 0x3c, "car dot ahead");
    const auto dashRadar = radarPixels(radarLayout(true), {}, 0, {});
    ok &= require(dashRadar[0] == 0 && dashRadar[1] == 0, "solid radar with the dashboard");
    return ok;
}

bool testRaceSound()
{
    bool ok = true;
    ok &= require(sampleNames()[2] == std::string("speed.wav") &&
                      sampleNames()[0x2b] == std::string("finish.wav"),
                  "sample names");
    for (std::size_t i = 0; i < stageTable().size(); ++i) {
        ok &= require(stageTable()[i].music == static_cast<int>(i) + 2, "stage music 2..7");
    }
    const auto& high = engineHighTable();
    const auto& low = engineLowTable();
    ok &= require(high[0].frequency == 11025 && high[0].volume == -2000, "high voice start");
    ok &= require(high[34].frequency == 11059 && high[34].volume == -470, "high voice 34");
    ok &= require(high[35].frequency == 11060 && high[79].frequency == 28660 &&
                      high[79].volume == -515,
                  "high voice top");
    ok &= require(low[0].frequency == 5500 && low[1].volume == -142 && low[35].volume == -5000,
                  "low voice");

    Infio infio;
    infio.sound = 1;
    infio.musicVolume = 3;
    infio.effectsVolume = 4;
    const SoundSettings settings = SoundSettings::fromInfio(infio);
    ok &= require(!settings.music && settings.effects, "GELUID 1: effects only");
    ok &= require(settings.musicVolume() == -600 && settings.effectVolume() == -100 &&
                      settings.engineVolume(-500) == -800,
                  "volumes");
    infio.sound = 7;
    ok &= require(!SoundSettings::fromInfio(infio).music && SoundSettings::fromInfio(infio).effects,
                  "other GELUID: the data defaults");
    return ok;
}

bool testPickups()
{
    bool ok = true;
    const auto pieces = longStraight(200);
    if (!require(pieces.size() == 199, "pickup track pieces")) {
        return false;
    }

    // Roadworks: 4 or 5 groups of a speed camera and ten cones; the first
    // group at piece 199 / 20 = 9, the camera one piece before.
    Lcg random;
    Pickups forward;
    forward.placeRoadworks(pieces, true, random);
    const auto pions = forward.pions();
    ok &= require(pions.size() == 44 || pions.size() == 55, "roadwork groups");
    ok &= require(pions[0].objectType == static_object::speedCamera && pions[0].piece == 8 &&
                      pions[0].lane == 0,
                  "speed camera before the cones");
    for (std::size_t i = 1; i <= 10; ++i) {
        ok &= require(pions[i].objectType == static_object::roadworks2 &&
                          pions[i].piece == 9 + static_cast<int>(i) - 1 && pions[i].lane >= 0 &&
                          pions[i].lane <= 1,
                      "cones in the outer lanes");
    }
    ok &= require(near(pions[1].position.y, 0.1F), "cone height + 0.1");
    ok &= require(pions[11].piece == 22 - 1, "second group at round(9 + 13.5) - 1");

    Lcg reverseRandom;
    Pickups backward;
    backward.placeRoadworks(pieces, false, reverseRandom);
    ok &= require(backward.pions()[0].piece == 10 && backward.pions()[0].lane == 10,
                  "reverse speed camera");
    ok &= require(backward.pions()[1].lane >= 9 && backward.pions()[2].piece == 8,
                  "reverse cones run down");

    // Bonuses: 199 / 27 = 7 of them, lanes 1..3, fuel at most 70 apart.
    forward.placeBonuses(pieces, true, random);
    const auto bonuses = forward.bonuses();
    ok &= require(bonuses.size() == 7, "bonus count");
    int lastFuel = 20;
    for (const auto& bonus : bonuses) {
        ok &= require(bonus.lane >= 1 && bonus.lane <= 3, "bonus lane");
        ok &= require(bonus.type >= 1 && bonus.type <= 3, "bonus type");
        if (bonus.type == bonus_type::fuel) {
            lastFuel = bonus.piece;
        } else {
            ok &= require(std::abs(bonus.piece - lastFuel) <= 70, "fuel gap");
        }
    }
    ok &= require(bonuses[0].piece >= 11 && bonuses[0].piece <= 20, "first bonus from piece 20");
    ok &= require(bonuses[0].yaw == 0x100 - pieces[static_cast<std::size_t>(bonuses[0].piece)].heading,
                  "bonus yaw");

    // The callback turns the bonus a further yaw each frame.
    const BonusObject first = bonuses[0];
    forward.runCallbacks();
    forward.runCallbacks();
    const Mat4 expected = rotationY(first.yaw + 15) * rotationY(first.yaw) *
                          Mat4::translation(first.position);
    ok &= require(near(forward.bonuses()[0].matrix.m[0][0], expected.m[0][0]) &&
                      near(forward.bonuses()[0].matrix.m[0][2], expected.m[0][2]) &&
                      near(forward.bonuses()[0].matrix.m[3][2], first.position.z),
                  "bonus spin accumulates");
    ok &= require(forward.bonuses()[0].yaw == (first.yaw + 30) % 0x400, "yaw steps 15");

    ok &= require(forward.takeBonus(first.piece, first.lane) == first.type, "bonus taken");
    ok &= require(forward.takeBonus(first.piece, first.lane) == bonus_type::none, "only once");
    ok &= require(forward.takeBonus(-1, first.lane) == bonus_type::none, "off the lanes");

    // A cone hit flies off, bumps once, falls and is retired.
    const PionObject cone = forward.pions()[1];
    PionHitter car{cone.piece, cone.lane, cone.position, {0.0F, 0.0F, 6.0F}};
    Vec3 bumpAt;
    ok &= require(forward.updatePions(car, random, bumpAt) && near(bumpAt.z, cone.position.z),
                  "cone hit");
    ok &= require(near(forward.pions()[1].velocity.y, 2.0F) && near(forward.pions()[1].velocity.z, 6.0F),
                  "cone velocity");
    ok &= require(!forward.updatePions(car, random, bumpAt), "bump only once");
    car.piece = 0;
    for (int frame = 0; frame < 100 && forward.pions()[1].active; ++frame) {
        forward.runCallbacks();
        forward.updatePions(car, random, bumpAt);
    }
    ok &= require(!forward.pions()[1].active && forward.pions()[1].piece == -1, "cone retired");
    return ok;
}

bool testCollision()
{
    bool ok = true;
    const std::vector<Vec3> mesh{{-5.0F, 0.0F, -10.0F}, {5.0F, 4.0F, 10.0F}};
    const CarBox box = CarBox::fromVertices(mesh);
    ok &= require(near(box.radiusSquared, 125.0F) && near(box.corners[2].z, 10.0F), "car box");

    CarBody a;
    a.box = box;
    a.mass = 1000.0F;
    a.position = {0.0F, 0.0F, 0.0F};
    a.updateCorners();
    CarBody b = a;
    b.position = {6.0F, 0.0F, 5.0F};
    b.updateCorners();
    ok &= require(collideCars(a, b, false) == contact::hit, "overlapping boxes hit");
    CarBody far = b;
    far.position = {0.0F, 0.0F, 1000.0F};
    far.updateCorners();
    ok &= require(collideCars(a, far, false) == contact::none, "far cars do not touch");
    CarBody beside = b;
    beside.position = {40.0F, 0.0F, 0.0F};
    beside.updateCorners();
    ok &= require(collideCars(a, beside, false) == contact::nearby,
                  "the squared radius sum squared again reaches far");

    a.speed = 10.0F;
    a.step = {0.0F, 0.0F, 3.0F};
    std::vector<Crash> crashes;
    ok &= require(collideCars(a, b, true, &crashes) == contact::hit, "hit with bounce");
    ok &= require(!crashes.empty() && near(a.speed, 7.0F * std::pow(0.7F, static_cast<float>(crashes.size() - 1)), 1e-3F),
                  "a crash slows the moving car to 0.7");
    ok &= require(b.bounce.z > 0.0F && a.bounce.z < 3.0F * 0.6F, "both cars get a bounce");
    ok &= require(std::abs(a.step.z) < 0.01F, "the step stops where the cars touch");
    return ok;
}

// The game loop's order: controls, the traffic step, the road follow and the
// object callbacks.
struct TrafficRun {
    std::vector<RoadPiece> pieces = longStraight(400);
    PlayerCar player;
    Traffic traffic;
    Lcg random;

    TrafficRun(const TrafficSettings& settings, int playerPlace)
    {
        const std::vector<Vec3> mesh{{-5.0F, 0.0F, -10.0F}, {5.0F, 4.0F, 10.0F}};
        player.setBox(mesh);
        player.setMass(1300.0F);
        player.placeOnGrid(pieces, true, 15, playerPlace);
        std::array<CarBox, 23> boxes{};
        boxes.fill(CarBox::fromVertices(mesh));
        std::array<int, 5> places{1, 2, 3, 4, 5};
        if (playerPlace != 6) {
            places = {2, 3, 4, 5, 6};
        }
        traffic.setUp(pieces, true, 15, player, places, boxes, settings, random);
    }

    TrafficEvents frame(const CarInput& input, bool clock)
    {
        CarEvents events;
        player.control(input, clock, events);
        TrafficEvents traffic_events;
        traffic.step(pieces, player, clock, 0, random, traffic_events);
        player.followRoad(pieces);
        traffic.runCallbacks(pieces);
        return traffic_events;
    }
};

bool testTraffic()
{
    bool ok = true;
    TrafficSettings settings;
    settings.police = 2;
    settings.civilians = 6;
    settings.racerSpeed = 16.0F;
    settings.policeSpeed = 17.0F;
    settings.racerRange = 2;
    settings.racerHighSpeed = 16.0F;
    settings.racerLowSpeed = 13.0F;
    TrafficRun run(settings, 6);
    const Traffic& traffic = run.traffic;
    const auto cars = traffic.cars();
    if (!require(cars.size() == 14 && run.pieces.size() == 399, "player, racers, police, civilians")) {
        return false;
    }

    // Racer 1 starts first: row 15 - 1, lane 2 + 1, 20 ahead of the 40 offset.
    const TrafficCar& first = cars[1];
    ok &= require(first.carType == 0x15 && first.kind == driver_kind::racer, "racer 1 is the lotus");
    ok &= require(first.body.piece == 14 && first.body.lane == 3 && first.targetLane == 3,
                  "racer 1 grid slot");
    ok &= require(near(first.body.position.x, 30.0F) && near(first.body.position.z, 14 * 60 + 60.0F),
                  "racer 1 position");
    ok &= require(first.body.heading == 0 && first.waypoint == 16 && first.topSpeed == 0.0F,
                  "racer 1 heading, waypoint, no speed yet");
    ok &= require(traffic.laneEntry(14, 3) == 1, "racer 1 in the lane table");
    const Driver& driver = traffic.drivers()[static_cast<std::size_t>(first.driver)];
    ok &= require(driver.type == driver_kind::racer && driver.lane == 1 && driver.speed == 16.0F &&
                      driver.target == 0,
                  "racer driver");
    for (std::size_t slot = 6; slot < 8; ++slot) {
        ok &= require(cars[slot].kind == driver_kind::police && cars[slot].body.lane <= 1 &&
                          cars[slot].carType == policeCarType,
                      "police on the hard shoulder");
        ok &= require(traffic.drivers()[static_cast<std::size_t>(cars[slot].driver)].speed == 17.0F,
                      "police speed");
    }
    ok &= require(cars[7].body.piece > cars[6].body.piece, "police spread along the road");
    for (std::size_t slot = 8; slot < cars.size(); ++slot) {
        const auto& car = cars[slot];
        const bool sameWay = car.body.forward && (car.body.lane == 2 || car.body.lane == 3);
        const bool otherWay = !car.body.forward && (car.body.lane == 7 || car.body.lane == 8);
        ok &= require(car.kind == driver_kind::civilian && (sameWay || otherWay) &&
                          car.body.piece >= 30 && car.carType != 4 && car.carType != policeCarType,
                      "civilian placement");
        ok &= require(car.topSpeed == carTypes()[static_cast<std::size_t>(car.carType)].topSpeed,
                      "civilians keep their type's top speed");
    }
    ok &= require(cars[8].carType == 0 && cars[12].carType == 5, "civilian types skip 4");

    // Before the start the racers wait; the civilians (slots above 13) do
    // not exist here, so nothing drives.
    run.frame({}, false);
    ok &= require(run.traffic.cars()[1].body.speed == 0.0F, "racers wait for the clock");

    // Racing: the racers pull away, the police wait, nothing breaks.
    for (int frame = 0; frame < 200; ++frame) {
        run.frame({}, true);
    }
    const auto after = run.traffic.cars();
    for (std::size_t slot = 1; slot <= 5; ++slot) {
        const auto& car = after[slot];
        ok &= require(std::isfinite(car.body.position.x) && std::isfinite(car.body.position.z),
                      "finite positions");
        ok &= require(car.body.piece > 30 && std::abs(car.body.position.x) < 120.0F,
                      "racers drive down the road");
        ok &= require(car.body.speed <= 16.0F + 1e-3F, "racers keep to their speed");
    }
    ok &= require(after[6].body.speed == 0.0F && after[6].topSpeed == 0.0F, "police on stake-out");
    const auto places = run.traffic.places(run.pieces);
    ok &= require(places[0] == 6, "the idle player is last");
    std::array<int, 6> sorted = places;
    std::sort(sorted.begin(), sorted.end());
    ok &= require(sorted == std::array<int, 6>{1, 2, 3, 4, 5, 6}, "six places");
    Mat4 sign;
    ok &= require(run.traffic.placeSign(places, 6, sign) && near(sign.m[3][2], run.player.position().z) &&
                      near(sign.m[3][1], run.player.position().y + 10.0F),
                  "sign 6 over the player");
    return ok;
}

bool testPoliceChase()
{
    bool ok = true;
    TrafficSettings settings;
    settings.police = 1;
    settings.racerSpeed = 5.0F;
    settings.policeSpeed = 17.0F;
    settings.racerHighSpeed = 5.0F;
    settings.racerLowSpeed = 5.0F;
    TrafficRun run(settings, 6);
    const int policePiece = run.traffic.cars()[6].body.piece;
    bool chased = false;
    bool caught = false;
    // Over 9 but slow enough that the police get within 10 pieces before the
    // car leaves the active list: at full speed the player escapes.
    for (int frame = 0; frame < 4000 && !caught; ++frame) {
        caught = run.frame({.gas = run.player.speed() < 10.0F}, true).caught;
        chased = chased || run.traffic.police()[0].state == police_state::chase;
    }
    ok &= require(run.player.piece() > policePiece, "the player passes the police car");
    ok &= require(chased, "a fast player passing starts a chase");
    ok &= require(caught, "the police stop the player");
    ok &= require(run.traffic.police()[0].state == police_state::cooldown &&
                      run.traffic.police()[0].target == -1,
                  "after the catch the police cool down");
    return ok;
}

int main(int argc, char** argv)
{
    bool ok = true;
    for (const auto test : {testMath, testTrack, testScenery, testScene, testSettings, testRaceStart,
                            testRaceEnds, testRaceResult, testSpeedTable, testPlayerCar, testHud,
                            testRaceSound, testPickups, testCollision, testTraffic,
                            testPoliceChase}) {
        ok = test() && ok;
    }
    if (argc > 1) {
        ok = testGameData(argv[1]) && ok;
    }
    return ok ? 0 : 1;
}
