#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace portfolio::protocol
{
    constexpr std::uint16_t kHeaderSize = 4;
    constexpr std::uint16_t kMaxPacketSize = 4096;

    enum class PacketId : std::uint16_t
    {
        LoginRequest = 1,
        LoginResponse = 2,
        PingRequest = 3,
        PingResponse = 4,
    };

    struct Header { std::uint16_t size{}; std::uint16_t id{}; };

    inline void AppendU16(std::vector<std::uint8_t>& output, const std::uint16_t value)
    {
        output.push_back(static_cast<std::uint8_t>(value & 0xff));
        output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    }

    inline bool ReadU16(const std::span<const std::uint8_t> input, std::size_t& offset, std::uint16_t& value)
    {
        if (offset + 2 > input.size()) return false;
        value = static_cast<std::uint16_t>(input[offset]) |
            static_cast<std::uint16_t>(input[offset + 1] << 8);
        offset += 2;
        return true;
    }

    inline Header ReadHeader(const std::span<const std::uint8_t> input)
    {
        Header header{};
        if (input.size() >= kHeaderSize)
        {
            std::size_t offset = 0;
            ReadU16(input, offset, header.size);
            ReadU16(input, offset, header.id);
        }
        return header;
    }

    inline void AppendString(std::vector<std::uint8_t>& output, const std::string_view value)
    {
        AppendU16(output, static_cast<std::uint16_t>(value.size()));
        output.insert(output.end(), value.begin(), value.end());
    }

    inline bool ReadString(const std::span<const std::uint8_t> input, std::size_t& offset, std::string& value)
    {
        std::uint16_t length = 0;
        if (!ReadU16(input, offset, length) || offset + length > input.size()) return false;
        value.assign(reinterpret_cast<const char*>(input.data() + offset), length);
        offset += length;
        return true;
    }

    inline std::vector<std::uint8_t> MakePacket(const PacketId id, const std::span<const std::uint8_t> payload)
    {
        std::vector<std::uint8_t> packet;
        packet.reserve(kHeaderSize + payload.size());
        AppendU16(packet, static_cast<std::uint16_t>(kHeaderSize + payload.size()));
        AppendU16(packet, static_cast<std::uint16_t>(id));
        packet.insert(packet.end(), payload.begin(), payload.end());
        return packet;
    }

    inline std::vector<std::uint8_t> MakeLoginRequest(
        const std::string_view account, const std::string_view password)
    {
        std::vector<std::uint8_t> payload;
        AppendString(payload, account);
        AppendString(payload, password);
        return MakePacket(PacketId::LoginRequest, payload);
    }
}
