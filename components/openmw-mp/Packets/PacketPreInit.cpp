#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketPreInit.hpp"

mwmp::PacketPreInit::PacketPreInit(RakNet::RakPeerInterface *peer)
    : BasePacket(peer)
    , checksums(nullptr)
    , groundcoverChecksums(&emptyGroundcover)
    , startLocation("default")
    , manifestPhase(PHASE_REQUEST)
    , manifestSyncSupported(false)
{
    packetID = ID_GAME_PREINIT;
}

void mwmp::PacketPreInit::Packet(RakNet::BitStream *newBitstream, bool send)
{
    BasePacket::Packet(newBitstream, send);

    if (checksums == nullptr)
    {
        packetValid = false;
        return;
    }

    const RakNet::BitSize_t packetSize = bs->GetNumberOfBytesUsed();
    uint32_t expectedPacketSize = BasePacket::headerSize() + sizeof(uint32_t);
    if (!send && expectedPacketSize > packetSize)
    {
        LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong packet size %d when expected %d", packetSize, expectedPacketSize);
        packetValid = false;
        return;
    }

    uint32_t numberOfChecksums = checksums->size();
    RW(numberOfChecksums, send);

    if (numberOfChecksums > maxPlugins)
    {
        LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong number of checksums %d when maximum is %d", numberOfChecksums, maxPlugins);
        packetValid = false;
        return;
    }

    struct NAS
    {
        uint32_t hashN;
        uint32_t strSize;
    };

    std::vector<NAS> NumberOfHashesAndStrSizes(numberOfChecksums);
    PluginContainer::const_iterator checksumIt = checksums->begin();

    for (auto &&nas : NumberOfHashesAndStrSizes)
    {
        if (send)
        {
            nas.strSize = checksumIt->first.size();
            nas.hashN = checksumIt++->second.size();
        }
        RW(nas, send);

        expectedPacketSize += nas.strSize + nas.hashN;

        if (nas.strSize > pluginNameMaxLength)
            LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong string length %d when maximum length is %d",
                        nas.strSize, pluginNameMaxLength);
        else if (nas.hashN > maxHashes)
            LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong number of hashes %d when maximum is %d", nas.hashN, maxHashes);
        else
            continue;
        packetValid = false;
        return;
    }

    // Older servers accepted the plugin list by sending a completely empty
    // payload after the legacy checksum section.
    if (!send && expectedPacketSize == packetSize)
        return;

    if (!send && expectedPacketSize > packetSize)
    {
        LOG_MESSAGE(TimedLog::LOG_ERROR, "Wrong packet size %d when expected %d", packetSize, expectedPacketSize);
        packetValid = false;
        return;
    }

    checksums->resize(numberOfChecksums);
    auto numberOfHashesIt = NumberOfHashesAndStrSizes.cbegin();
    for (auto &&checksum : *checksums)
    {
        RW(checksum.first, send, false, numberOfHashesIt->strSize);
        checksum.second.resize(numberOfHashesIt->hashN);
        for (auto &&hash : checksum.second)
            RW(hash, send);
        ++numberOfHashesIt;
    }

    // startLocation was the first ArenaMP extension and remains optional for
    // compatibility with older TES3MP peers.
    if (!send && bs->GetNumberOfUnreadBits() == 0)
        return;

    if (!RW(startLocation, send, false, startLocationMaxLength))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Failed to read the pre-init start location");
        packetValid = false;
        return;
    }

    // FIX12 extension. A peer without this byte uses the old one-step data-file
    // enforcement. New ArenaMP peers use a two-phase manifest -> verify flow.
    if (!send && bs->GetNumberOfUnreadBits() == 0)
        return;

    if (!RW(manifestPhase, send))
    {
        packetValid = false;
        return;
    }
    manifestSyncSupported = true;

    uint32_t groundcoverCount = groundcoverChecksums != nullptr
        ? static_cast<uint32_t>(groundcoverChecksums->size()) : 0;
    if (!RW(groundcoverCount, send) || groundcoverCount > maxPlugins)
    {
        packetValid = false;
        return;
    }

    if (!send)
        groundcoverChecksums->resize(groundcoverCount);

    for (uint32_t i = 0; i < groundcoverCount; ++i)
    {
        PluginPair& entry = (*groundcoverChecksums)[i];
        if (!RW(entry.first, send, false, pluginNameMaxLength))
        {
            packetValid = false;
            return;
        }

        uint32_t hashCount = send ? static_cast<uint32_t>(entry.second.size()) : 0;
        if (!RW(hashCount, send) || hashCount > maxHashes)
        {
            packetValid = false;
            return;
        }
        if (!send)
            entry.second.resize(hashCount);
        for (uint32_t h = 0; h < hashCount; ++h)
        {
            if (!RW(entry.second[h], send))
            {
                packetValid = false;
                return;
            }
        }
    }
}

void mwmp::PacketPreInit::setChecksums(mwmp::PacketPreInit::PluginContainer *newChecksums)
{
    checksums = newChecksums;
}

void mwmp::PacketPreInit::setGroundcoverChecksums(mwmp::PacketPreInit::PluginContainer *newChecksums)
{
    groundcoverChecksums = newChecksums != nullptr ? newChecksums : &emptyGroundcover;
}

void mwmp::PacketPreInit::setStartLocation(const std::string& location)
{
    startLocation = location.empty() ? "default" : location.substr(0, startLocationMaxLength);
}

const std::string& mwmp::PacketPreInit::getStartLocation() const
{
    return startLocation;
}

void mwmp::PacketPreInit::setManifestPhase(uint8_t phase)
{
    manifestPhase = phase;
    manifestSyncSupported = true;
}

uint8_t mwmp::PacketPreInit::getManifestPhase() const
{
    return manifestPhase;
}

bool mwmp::PacketPreInit::supportsManifestSync() const
{
    return manifestSyncSupported;
}
