/*
* Audacity: A Digital Audio Editor
*/
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "au3-realtime-effects/RealtimeEffectState.h"

#include "automation/view/mixerpanelmodel.h"
#include "mocks/mixermocks.h"
#include "context/tests/mocks/globalcontextmock.h"
#include "trackedit/tests/mocks/projecthistorymock.h"
#include "playback/tests/mocks/playbackmock.h"

using namespace ::testing;
using namespace au::automation;
namespace mx = au::au3::mixer;
namespace effects = au::effects;

namespace au::automation {
class MixerPanelModelTest : public Test
{
protected:
    void SetUp() override
    {
        m_fx = std::make_shared<NiceMock<RealtimeEffectServiceMock> >();
        m_view = std::make_shared<NiceMock<EffectViewControllerMock> >();
        m_ctrl = std::make_shared<NiceMock<TrackPlaybackControlMock> >();
        m_routing = std::make_shared<NiceMock<BusRoutingInteractionMock> >();
        m_model = std::make_unique<MixerPanelModel>();
        m_model->realtimeEffectService.set(m_fx);
        m_model->effectViewController.set(m_view);
        m_model->trackPlaybackControl.set(m_ctrl);
        m_model->routing.set(m_routing);
        m_model->globalContext.set(std::make_shared<NiceMock<context::GlobalContextMock> >());
        m_model->projectHistory.set(std::make_shared<NiceMock<trackedit::ProjectHistoryMock> >());
        m_model->playback.set(std::make_shared<NiceMock<playback::PlaybackMock> >());
        m_model->setStripsProvider([this] { return m_strips; });

        ON_CALL(*m_fx, effectStack(_)).WillByDefault(Return(std::nullopt));
        ON_CALL(*m_fx, effectName(_)).WillByDefault([](const effects::RealtimeEffectStatePtr& s) {
            return std::optional<std::string>("FX:" + s->GetID().ToStdString());
        });
        ON_CALL(*m_routing, canRouteTo(_, _)).WillByDefault(Return(true));
    }

    static mx::Strip track(long long id, const std::string& name)
    {
        mx::Strip s;
        s.trackId = id;
        s.kind = mx::StripKind::Track;
        s.name = name;
        s.channels = 1;
        return s;
    }

    static mx::Strip bus(long long id, const std::string& name, const std::string& busId)
    {
        mx::Strip s = track(id, name);
        s.kind = mx::StripKind::Bus;
        s.busId = busId;
        s.channels = 2;
        return s;
    }

    static mx::Strip master()
    {
        mx::Strip s;
        s.name = "Master";
        return s;
    }

    QVariantMap strip(int i) const { return m_model->strips().at(i).toMap(); }

    static QStringList names(const QVariantList& list, const char* key = "name")
    {
        QStringList out;
        for (const auto& v : list) {
            out << v.toMap().value(key).toString();
        }
        return out;
    }

    std::shared_ptr<NiceMock<RealtimeEffectServiceMock> > m_fx;
    std::shared_ptr<NiceMock<EffectViewControllerMock> > m_view;
    std::shared_ptr<NiceMock<TrackPlaybackControlMock> > m_ctrl;
    std::shared_ptr<NiceMock<BusRoutingInteractionMock> > m_routing;
    std::unique_ptr<MixerPanelModel> m_model;
    std::vector<mx::Strip> m_strips;
};
}

TEST_F(MixerPanelModelTest, StripsCarryRoutingChoicesWithoutLoopsOrDuplicates)
{
    auto guitar = track(1, "Guitar");
    guitar.outputBusId = "bus1";
    guitar.sends = { { "bus2", 0.5f } };
    guitar.volumeDb = -6.0;
    guitar.pan = 0.25f;
    m_strips = { guitar, track(2, "Vox"), bus(3, "Drums", "bus1"), bus(4, "Verb", "bus2"), bus(5, "Fx", "bus3"), master() };
    // Fx -> Guitar's chain would loop
    ON_CALL(*m_routing, canRouteTo(1, std::string("bus3"))).WillByDefault(Return(false));

    m_model->reload();
    ASSERT_EQ(m_model->strips().size(), 6);
    EXPECT_EQ(m_model->meters().size(), 6);

    const auto g = strip(0);
    EXPECT_EQ(g["kind"].toString(), "track");
    EXPECT_EQ(g["outputName"].toString(), "Drums");
    EXPECT_DOUBLE_EQ(g["volumeDb"].toDouble(), -6.0);
    EXPECT_DOUBLE_EQ(g["pan"].toDouble(), 0.25);
    // outputs: master + every bus reachable without a loop
    EXPECT_EQ(names(g["outputs"].toList()), (QStringList { "Master", "Drums", "Verb" }));
    // send targets: not the output bus, not an existing send, no loops
    EXPECT_TRUE(names(g["sendTargets"].toList()).isEmpty());
    ASSERT_EQ(g["sends"].toList().size(), 1);
    EXPECT_EQ(g["sends"].toList()[0].toMap()["name"].toString(), "Verb");

    const auto vox = strip(1);
    EXPECT_EQ(vox["outputName"].toString(), "Master");
    EXPECT_EQ(names(vox["sendTargets"].toList()), (QStringList { "Drums", "Verb", "Fx" }));

    EXPECT_EQ(strip(2)["kind"].toString(), "bus");
    EXPECT_EQ(strip(5)["kind"].toString(), "master");
    EXPECT_FALSE(strip(5).contains("outputs"));
}

TEST_F(MixerPanelModelTest, InsertsAreNamedAndEditedThroughTheRealtimeEffectService)
{
    auto guitar = track(1, "Guitar");
    guitar.inserts = { { "comp", true }, { "eq", false }, { "verb", true } };
    auto m = master();
    m.inserts = { { "limiter", true } };
    m_strips = { guitar, m };

    const auto comp = RealtimeEffectState::make_shared(wxT("comp"));
    const auto eq = RealtimeEffectState::make_shared(wxT("eq"));
    const auto verb = RealtimeEffectState::make_shared(wxT("verb"));
    const auto limiter = RealtimeEffectState::make_shared(wxT("limiter"));
    ON_CALL(*m_fx, effectStack(1)).WillByDefault(Return(std::vector<effects::RealtimeEffectStatePtr> { comp, eq, verb }));
    ON_CALL(*m_fx, effectStack(effects::IRealtimeEffectService::masterTrackId))
    .WillByDefault(Return(std::vector<effects::RealtimeEffectStatePtr> { limiter }));

    m_model->reload();
    const auto inserts = strip(0)["inserts"].toList();
    EXPECT_EQ(names(inserts), (QStringList { "FX:comp", "FX:eq", "FX:verb" }));
    EXPECT_FALSE(inserts[1].toMap()["active"].toBool());
    EXPECT_EQ(names(strip(1)["inserts"].toList()), (QStringList { "FX:limiter" }));

    // reorder: up/down map to moveRealtimeEffect on the right state; out-of-range moves are ignored
    EXPECT_CALL(*m_fx, moveRealtimeEffect(eq, 0)).Times(1);
    m_model->moveInsert(1, 1, 0);
    EXPECT_CALL(*m_fx, moveRealtimeEffect(verb, _)).Times(0);
    m_model->moveInsert(1, 2, 3);

    // bypass, remove (closing its editor first), open editor
    EXPECT_CALL(*m_fx, setIsActive(eq, true)).Times(1);
    m_model->setInsertActive(1, 1, true);
    {
        InSequence seq;
        EXPECT_CALL(*m_view, hideEffect(comp)).Times(1);
        EXPECT_CALL(*m_fx, removeRealtimeEffect(1, comp)).Times(1);
    }
    m_model->removeInsert(1, 0);
    EXPECT_CALL(*m_view, showEffect(limiter)).Times(1);
    m_model->showInsert(effects::IRealtimeEffectService::masterTrackId, 0);
    EXPECT_CALL(*m_fx, setTrackEffectsActive(effects::IRealtimeEffectService::masterTrackId, false)).Times(1);
    m_model->setInsertsActive(effects::IRealtimeEffectService::masterTrackId, false);

    // bad indexes do nothing
    EXPECT_CALL(*m_fx, removeRealtimeEffect(_, _)).Times(0);
    m_model->removeInsert(1, 7);
    m_model->removeInsert(42, 0);
}

TEST_F(MixerPanelModelTest, FaderPanMuteSoloAndRoutingGoThroughTheExistingServices)
{
    m_strips = { track(1, "Guitar"), bus(2, "Drums", "bus1"), master() };
    m_model->reload();

    EXPECT_CALL(*m_ctrl, setVolume(1, FloatEq(-12.0f), false)).Times(1);
    EXPECT_CALL(*m_ctrl, setVolume(1, FloatEq(-12.0f), true)).Times(1);
    m_model->setVolume(1, -12.0, false);
    m_model->setVolume(1, -12.0, true);

    EXPECT_CALL(*m_ctrl, setPan(2, FloatEq(1.0f), true)).Times(1); // clamped
    m_model->setPan(2, 3.0, true);
    EXPECT_CALL(*m_ctrl, setPan(_, _, _)).Times(0);                 // master has no pan
    m_model->setPan(mx::Strip::MasterId, 0.5, true);

    EXPECT_CALL(*m_ctrl, setMuted(2, true, false)).Times(1);
    m_model->setMute(2, true);
    EXPECT_CALL(*m_ctrl, setSolo(1, true, false)).Times(1);
    m_model->setSolo(1, true);

    EXPECT_CALL(*m_routing, setOutput(1, std::string("bus1"))).WillOnce(Return(true));
    EXPECT_TRUE(m_model->setOutput(1, "bus1"));
    EXPECT_CALL(*m_routing, setSend(1, std::string("bus1"), FloatEq(0.5f), true)).WillOnce(Return(true));
    EXPECT_TRUE(m_model->setSend(1, "bus1", 0.5, true));
    EXPECT_CALL(*m_routing, removeSend(1, std::string("bus1"))).WillOnce(Return(false));
    EXPECT_FALSE(m_model->removeSend(1, "bus1"));
}
