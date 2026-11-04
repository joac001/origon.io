#include "net/RelayBrowser.h"

#include "net/Protocol.h"
#include "net/RelayProtocol.h"

#include <enet/enet.h>

namespace origon::net {

RelayBrowser::~RelayBrowser() {
    close();
    if (m_enetReady) enet_deinitialize();
}

void RelayBrowser::close() {
    if (!m_host) return;
    if (m_peer) enet_peer_reset(m_peer);
    enet_host_destroy(m_host);
    m_host = nullptr;
    m_peer = nullptr;
}

void RelayBrowser::refresh(const std::string& address, uint16_t port) {
    if (m_host) return;
    if (!m_enetReady) {
        if (enet_initialize() != 0) return;
        m_enetReady = true;
    }
    ENetAddress target{};
    if (enet_address_set_host(&target, address.c_str()) != 0) {
        m_status = "Dirección inválida";
        return;
    }
    target.port = port;
    m_host = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
    if (!m_host) return;
    m_peer = enet_host_connect(m_host, &target, kChannelCount, 0);
    if (!m_peer) {
        close();
        return;
    }
    enet_peer_timeout(m_peer, 0, 2500, 4000);
    m_replied = false;
    if (m_rooms.empty()) m_status = "Consultando...";
}

void RelayBrowser::poll() {
    if (!m_host) return;
    ENetEvent event;
    while (m_host && enet_host_service(m_host, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            Writer w;
            w.put(RelayMessage::List);
            enet_peer_send(m_peer, kChannelReliable, enet_packet_create(w.data.data(), w.data.size(), ENET_PACKET_FLAG_RELIABLE));
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            Reader reader(event.packet->data, event.packet->dataLength);
            if (static_cast<RelayMessage>(reader.get<uint8_t>()) == RelayMessage::Rooms) {
                const auto count = reader.get<uint16_t>();
                std::vector<Room> rooms;
                for (int i = 0; i < count && reader.ok(); ++i) {
                    Room room;
                    room.code = reader.getString();
                    room.name = reader.getString();
                    room.map = reader.getString();
                    room.players = reader.get<uint8_t>();
                    room.maxPlayers = reader.get<uint8_t>();
                    if (reader.ok()) rooms.push_back(std::move(room));
                }
                m_rooms = std::move(rooms);
                m_replied = true;
                m_status = m_rooms.empty() ? "No hay partidas publicadas" : "";
            }
            enet_packet_destroy(event.packet);
            enet_peer_disconnect(m_peer, 0);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
            if (!m_replied) {
                m_rooms.clear();
                m_status = "Sin respuesta del servidor intermedio";
            }
            m_peer = nullptr;
            close();
            break;
        default:
            break;
        }
    }
}

} // namespace origon::net
