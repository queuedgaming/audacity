/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "framework/global/modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "trackedit/iprojecthistory.h"

#include "au3wrap/au3types.h"

#include "automation/iparamautomationinteraction.h"

namespace au::au3 {
struct ParamLane;
class ParamAutomationData;
}

namespace au::automation {
class Au3ParamAutomationInteraction : public IParamAutomationInteraction, public muse::Contextable
{
    muse::ContextInject<au::context::IGlobalContext> globalContext{ this };
    muse::ContextInject<au::trackedit::IProjectHistory> projectHistory{ this };

public:
    Au3ParamAutomationInteraction(const muse::modularity::ContextPtr& ctx);

    std::vector<ParamTarget> availableParams(trackedit::TrackId trackId) const override;
    bool isShown(trackedit::TrackId trackId) const override;
    void setShown(trackedit::TrackId trackId, bool shown) override;
    std::optional<ParamTarget> activeTarget(trackedit::TrackId trackId) const override;
    bool setActiveTarget(trackedit::TrackId trackId, const ParamTarget& target) override;
    AutomationPoints activePoints(trackedit::TrackId trackId) const override;
    std::string valueText(trackedit::TrackId trackId, double value) const override;
    int addPoint(trackedit::TrackId trackId, double t, double value, bool completed) override;
    bool setPoint(trackedit::TrackId trackId, int index, double t, double value, bool completed) override;
    bool removePoint(trackedit::TrackId trackId, int index, bool completed) override;
    void cancelEdit(trackedit::TrackId trackId) override;
    void reinstall() override;
    muse::async::Channel<trackedit::TrackId> changed() const override;

private:
    au3::Au3Project* project() const;
    au3::ParamAutomationData* data(trackedit::TrackId trackId) const;
    au3::ParamLane* activeLane(trackedit::TrackId trackId) const;
    void afterEdit(trackedit::TrackId trackId, bool completed, const std::string& what);

    muse::async::Channel<trackedit::TrackId> m_changed;
    bool m_editPending = false;
};
}
