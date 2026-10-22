/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "framework/global/modularity/ioc.h"
#include "framework/global/async/asyncable.h"
#include "context/iglobalcontext.h"
#include "trackedit/iprojecthistory.h"

#include "au3wrap/au3types.h"

#include "automation/ibusroutinginteraction.h"

namespace au::automation {
class Au3BusRoutingInteraction : public IBusRoutingInteraction, public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<au::context::IGlobalContext> globalContext{ this };
    muse::ContextInject<au::trackedit::IProjectHistory> projectHistory{ this };

public:
    Au3BusRoutingInteraction(const muse::modularity::ContextPtr& ctx);

    std::vector<RoutingNode> nodes() const override;
    trackedit::TrackId addBus() override;
    bool canRouteTo(trackedit::TrackId trackId, const std::string& busId) const override;
    bool setOutput(trackedit::TrackId trackId, const std::string& busId) override;
    bool setSend(trackedit::TrackId trackId, const std::string& busId, float level, bool completed) override;
    bool removeSend(trackedit::TrackId trackId, const std::string& busId) override;
    muse::async::Notification changed() const override;

private:
    au3::Au3Project* project() const;
    void commit(const std::string& what);

    muse::async::Notification m_changed;
};
}
