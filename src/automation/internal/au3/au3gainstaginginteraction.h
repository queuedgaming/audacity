/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "framework/global/modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "trackedit/iprojecthistory.h"

#include "au3wrap/au3types.h"

#include "automation/igainstaginginteraction.h"

namespace au::automation {
class Au3GainStagingInteraction : public IGainStagingInteraction, public muse::Contextable
{
    muse::ContextInject<au::context::IGlobalContext> globalContext{ this };
    muse::ContextInject<au::trackedit::IProjectHistory> projectHistory{ this };

public:
    Au3GainStagingInteraction(const muse::modularity::ContextPtr& ctx);

    double trimDb(trackedit::TrackId trackId) const override;
    bool setTrim(trackedit::TrackId trackId, double db, bool completed) override;
    int applyTrims(const std::map<trackedit::TrackId, double>& trims) override;
    std::vector<TrackLevels> analyze() const override;
    muse::async::Notification changed() const override;

private:
    au3::Au3Project* project() const;
    void notifyTrack(trackedit::TrackId trackId);

    muse::async::Notification m_changed;
};
}
