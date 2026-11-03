#include "net/Protocol.h"

#include <algorithm>
#include <cmath>

namespace origon::net {

void writeCommand(Writer& w, uint32_t tick, const PlayerCommand& command) {
    w.put(static_cast<uint8_t>(MessageType::Command));
    w.put(tick);
    w.put(static_cast<int8_t>(std::lround(std::clamp(command.forward, -1.0f, 1.0f) * 127.0f)));
    w.put(static_cast<int8_t>(std::lround(std::clamp(command.right, -1.0f, 1.0f) * 127.0f)));
    w.put(command.yaw);
    w.put(command.pitch);
    w.put(static_cast<uint16_t>(command.buttons));
}

PlayerCommand readCommand(Reader& r, uint32_t& tick) {
    PlayerCommand command;
    tick = r.get<uint32_t>();
    command.forward = static_cast<float>(r.get<int8_t>()) / 127.0f;
    command.right = static_cast<float>(r.get<int8_t>()) / 127.0f;
    command.yaw = r.get<float>();
    command.pitch = r.get<float>();
    command.buttons = r.get<uint16_t>();
    return command;
}

void writeSnapshot(Writer& w, const Snapshot& s) {
    w.put(static_cast<uint8_t>(MessageType::Snapshot));
    w.put(s.serverTick);
    w.put(s.timeLeft);
    w.put(s.phase);
    w.put(s.winner);
    w.put(static_cast<uint8_t>(s.players.size()));
    for (const PlayerState& p : s.players) {
        w.put(p.id);
        w.put(p.flags);
        w.putVec3(p.position);
        w.putVec3(p.velocity);
        w.put(p.yaw);
        w.put(p.pitch);
        w.put(p.eyeHeight);
        w.put(p.health);
        w.put(p.tier);
        w.put(p.kills);
        w.put(p.deaths);
        w.put(p.ammo);
        w.put(p.reloadTimer);
        w.put(p.respawnTimer);
        w.put(p.spawnProtection);
        w.put(p.viewKick);
        w.put(p.meleeAnimation);
    }
    w.put(static_cast<uint16_t>(s.props.size()));
    for (const PropState& prop : s.props) {
        w.putVec3(prop.position);
        w.put(prop.rotation.x);
        w.put(prop.rotation.y);
        w.put(prop.rotation.z);
        w.put(prop.rotation.w);
    }
}

Snapshot readSnapshot(Reader& r) {
    Snapshot s;
    s.serverTick = r.get<uint32_t>();
    s.timeLeft = r.get<float>();
    s.phase = r.get<uint8_t>();
    s.winner = r.get<int8_t>();
    const auto count = r.get<uint8_t>();
    for (int i = 0; i < count && r.ok(); ++i) {
        PlayerState p;
        p.id = r.get<uint8_t>();
        p.flags = r.get<uint8_t>();
        p.position = r.getVec3();
        p.velocity = r.getVec3();
        p.yaw = r.get<float>();
        p.pitch = r.get<float>();
        p.eyeHeight = r.get<float>();
        p.health = r.get<float>();
        p.tier = r.get<uint8_t>();
        p.kills = r.get<uint16_t>();
        p.deaths = r.get<uint16_t>();
        p.ammo = r.get<uint8_t>();
        p.reloadTimer = r.get<float>();
        p.respawnTimer = r.get<float>();
        p.spawnProtection = r.get<float>();
        p.viewKick = r.get<float>();
        p.meleeAnimation = r.get<float>();
        s.players.push_back(p);
    }
    const auto props = r.get<uint16_t>();
    for (int i = 0; i < props && r.ok(); ++i) {
        PropState prop;
        prop.position = r.getVec3();
        prop.rotation.x = r.get<float>();
        prop.rotation.y = r.get<float>();
        prop.rotation.z = r.get<float>();
        prop.rotation.w = r.get<float>();
        s.props.push_back(prop);
    }
    return s;
}

void writeEvents(Writer& w, const std::vector<GameEvent>& events) {
    w.put(static_cast<uint8_t>(MessageType::Events));
    w.put(static_cast<uint16_t>(events.size()));
    for (const GameEvent& e : events) {
        w.put(static_cast<uint8_t>(e.type));
        w.put(static_cast<int8_t>(e.a));
        w.put(static_cast<int8_t>(e.b));
        w.put(static_cast<uint8_t>(e.tier));
        w.put(static_cast<uint8_t>(e.cause));
        w.put(static_cast<uint8_t>(e.headshot ? 1 : 0));
        w.putString(e.weaponName);
        w.putVec3(e.start);
        w.putVec3(e.end);
        w.putVec3(e.normal);
        w.put(static_cast<uint8_t>(e.surface));
        w.put(e.magnitude);
        w.put(static_cast<uint8_t>(e.category));
    }
}

std::vector<GameEvent> readEvents(Reader& r) {
    std::vector<GameEvent> events;
    const auto count = r.get<uint16_t>();
    for (int i = 0; i < count && r.ok(); ++i) {
        GameEvent e{.type = static_cast<GameEvent::Type>(r.get<uint8_t>())};
        e.a = r.get<int8_t>();
        e.b = r.get<int8_t>();
        e.tier = r.get<uint8_t>();
        e.cause = static_cast<KillCause>(r.get<uint8_t>());
        e.headshot = r.get<uint8_t>() != 0;
        e.weaponName = r.getString();
        e.start = r.getVec3();
        e.end = r.getVec3();
        e.normal = r.getVec3();
        e.surface = r.get<uint8_t>();
        e.magnitude = r.get<float>();
        e.category = static_cast<WeaponCategory>(r.get<uint8_t>());
        events.push_back(e);
    }
    return events;
}

} // namespace origon::net
