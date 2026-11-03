#ifndef OPENMW_PROCESSOROBJECTANIMPLAY_HPP
#define OPENMW_PROCESSOROBJECTANIMPLAY_HPP

#include "../ObjectProcessor.hpp"

namespace mwmp
{
    class ProcessorObjectAnimPlay : public ObjectProcessor
    {
    public:
        ProcessorObjectAnimPlay()
        {
            BPP_INIT(ID_OBJECT_ANIM_PLAY)
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            SendToLoadedCell(packet, objectList);
        }
    };
}

#endif //OPENMW_PROCESSOROBJECTANIMPLAY_HPP
