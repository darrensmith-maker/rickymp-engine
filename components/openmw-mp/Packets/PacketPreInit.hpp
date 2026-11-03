#ifndef OPENMW_PACKETPREINIT_HPP
#define OPENMW_PACKETPREINIT_HPP

#include <vector>
#include "BasePacket.hpp"

namespace mwmp
{
    class PacketPreInit : public BasePacket
    {
    public:
        typedef std::vector<uint32_t> HashList;
        typedef std::pair<std::string, HashList> PluginPair;
        typedef std::vector<PluginPair> PluginContainer;

        enum ManifestPhase : uint8_t
        {
            PHASE_REQUEST = 0,
            PHASE_MANIFEST = 1,
            PHASE_VERIFY = 2,
            PHASE_ACCEPTED = 3,
            PHASE_REJECTED = 4
        };

        PacketPreInit(RakNet::RakPeerInterface *peer);

        virtual void Packet(RakNet::BitStream *newBitstream, bool send);
        void setChecksums(PluginContainer *checksums);
        void setGroundcoverChecksums(PluginContainer *checksums);
        void setStartLocation(const std::string& location);
        const std::string& getStartLocation() const;
        void setManifestPhase(uint8_t phase);
        uint8_t getManifestPhase() const;
        bool supportsManifestSync() const;

    private:
        PluginContainer *checksums;
        PluginContainer *groundcoverChecksums;
        PluginContainer emptyGroundcover;
        std::string startLocation;
        uint8_t manifestPhase;
        bool manifestSyncSupported;
        const static uint32_t maxPlugins = 1000;
        const static uint32_t pluginNameMaxLength = 256;
        const static uint32_t maxHashes = 50;
        const static uint32_t startLocationMaxLength = 256;
    };
}

#endif //OPENMW_PACKETPREINIT_HPP
