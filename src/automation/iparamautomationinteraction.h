/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "async/channel.h"
#include "modularity/imoduleinterface.h"

#include "automationtypes.h"
#include "trackedit/trackedittypes.h"

namespace au::automation {
//! One automatable parameter of one realtime effect slot of a track
struct ParamTarget {
    std::string effectId;
    int occurrence = 0;
    std::string paramKey;
    std::string effectName;
    double minValue = 0.0;
    double maxValue = 1.0;
    double defaultValue = 0.0; //!< current unautomated value of the effect
    bool isInteger = false;

    bool sameAs(const ParamTarget& o) const
    {
        return effectId == o.effectId && occurrence == o.occurrence && paramKey == o.paramKey;
    }
};

//! Editing of plugin parameter automation lanes (per track realtime effects)
class IParamAutomationInteraction : MODULE_EXPORT_INTERFACE
{
    INTERFACE_ID(IParamAutomationInteraction)

public:
    virtual ~IParamAutomationInteraction() = default;

    virtual std::vector<ParamTarget> availableParams(trackedit::TrackId trackId) const = 0;

    virtual bool isShown(trackedit::TrackId trackId) const = 0;
    virtual void setShown(trackedit::TrackId trackId, bool shown) = 0;

    virtual std::optional<ParamTarget> activeTarget(trackedit::TrackId trackId) const = 0;
    virtual bool setActiveTarget(trackedit::TrackId trackId, const ParamTarget& target) = 0;

    //! Points of the active lane (xValue: project time, yValue: real parameter value)
    virtual AutomationPoints activePoints(trackedit::TrackId trackId) const = 0;
    virtual int addPoint(trackedit::TrackId trackId, double t, double value, bool completed) = 0;
    virtual bool setPoint(trackedit::TrackId trackId, int index, double t, double value, bool completed) = 0;
    virtual bool removePoint(trackedit::TrackId trackId, int index, bool completed) = 0;
    virtual void cancelEdit(trackedit::TrackId trackId) = 0;

    //! Re-attach the automation to the realtime effect states (after undo/redo, load, edits)
    virtual void reinstall() = 0;

    virtual muse::async::Channel<trackedit::TrackId> changed() const = 0;
};
}
