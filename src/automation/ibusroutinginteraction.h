/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <string>
#include <vector>

#include "async/channel.h"
#include "async/notification.h"
#include "modularity/imoduleinterface.h"

#include "trackedit/trackedittypes.h"

namespace au::automation {
struct RoutingSend {
    std::string busId;
    float level = 1.0f;
};

//! One track or bus of the routing graph (master is implicit)
struct RoutingNode {
    trackedit::TrackId trackId = trackedit::INVALID_TRACK;
    std::string name;
    bool isBus = false;
    std::string busId;       //!< buses only
    std::string outputBusId; //!< empty: master
    std::vector<RoutingSend> sends;
};

//! Editing of the bus routing graph: bus tracks, per-track outputs and sends
class IBusRoutingInteraction : MODULE_EXPORT_INTERFACE
{
    INTERFACE_ID(IBusRoutingInteraction)

public:
    virtual ~IBusRoutingInteraction() = default;

    //! Wave tracks and buses in track order
    virtual std::vector<RoutingNode> nodes() const = 0;

    //! Adds an empty stereo bus track; returns its track id (INVALID_TRACK on failure)
    virtual trackedit::TrackId addBus() = 0;

    //! Whether `trackId`'s output or a send may go to `busId` (exists, not itself, no cycle)
    virtual bool canRouteTo(trackedit::TrackId trackId, const std::string& busId) const = 0;

    //! Output to a bus (or master with an empty id). Undoable. False if refused.
    virtual bool setOutput(trackedit::TrackId trackId, const std::string& busId) = 0;
    //! Add/update a send. `completed` = false while dragging a level (one undo step at the end).
    virtual bool setSend(trackedit::TrackId trackId, const std::string& busId, float level, bool completed) = 0;
    virtual bool removeSend(trackedit::TrackId trackId, const std::string& busId) = 0;

    virtual muse::async::Notification changed() const = 0;
};
}
