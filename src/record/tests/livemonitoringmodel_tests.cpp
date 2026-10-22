/*
* Audacity: A Digital Audio Editor
*/
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "qml/Audacity/Record/livemonitoringmodel.h"

#include "context/tests/mocks/globalcontextmock.h"
#include "trackedit/tests/mocks/selectioncontrollermock.h"
#include "mocks/recordconfigurationmock.h"
#include "mocks/recordmock.h"

using namespace au;
using namespace au::record;
using ::testing::NiceMock;
using ::testing::Return;

class LiveMonitoringModelTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = std::make_shared<NiceMock<context::GlobalContextMock> >();
        m_record = std::make_shared<NiceMock<RecordMock> >();
        m_config = std::make_shared<NiceMock<RecordConfigurationMock> >();
        ON_CALL(*m_context, currentProjectChanged()).WillByDefault(Return(m_projectChanged));
        ON_CALL(*m_record, monitoringStatusChanged()).WillByDefault(Return(m_statusChanged));
        ON_CALL(*m_config, monitorGainDbChanged()).WillByDefault(Return(m_gainChanged));
        ON_CALL(*m_config, monitorThroughEffects()).WillByDefault(Return(true));
        m_selection = std::make_shared<NiceMock<trackedit::SelectionControllerMock> >();
        ON_CALL(*m_selection, tracksSelected()).WillByDefault(Return(m_tracksSelected));
        m_model = std::make_unique<LiveMonitoringModel>();
        m_model->selectionController.set(m_selection);
        m_model->globalContext.set(m_context);
        m_model->record.set(m_record);
        m_model->configuration.set(m_config);
    }

    std::shared_ptr<NiceMock<context::GlobalContextMock> > m_context;
    std::shared_ptr<NiceMock<RecordMock> > m_record;
    std::shared_ptr<NiceMock<RecordConfigurationMock> > m_config;
    std::shared_ptr<NiceMock<trackedit::SelectionControllerMock> > m_selection;
    muse::async::Channel<trackedit::TrackIdList> m_tracksSelected;
    std::unique_ptr<LiveMonitoringModel> m_model;
    muse::async::Notification m_projectChanged, m_statusChanged, m_gainChanged;
};

TEST_F(LiveMonitoringModelTests, SettersWriteConfigurationWithClamping)
{
    EXPECT_CALL(*m_config, setIsInputMonitoringOn(true));
    EXPECT_CALL(*m_config, setMonitorThroughEffects(false));
    EXPECT_CALL(*m_config, setLowLatencyMonitoring(true));
    EXPECT_CALL(*m_config, setLowLatencyThresholdMs(500.0));
    EXPECT_CALL(*m_config, setMonitorGainDb(-60.0));
    EXPECT_CALL(*m_config, setMonitorMuted(true));
    m_model->setMonitorInput(true);
    m_model->setThroughEffects(false);
    m_model->setLowLatency(true);
    m_model->setThresholdMs(9000);
    m_model->setGainDb(-200);
    m_model->toggleMute();
}

TEST_F(LiveMonitoringModelTests, ReadsConfiguration)
{
    ON_CALL(*m_config, isInputMonitoringOn()).WillByDefault(Return(true));
    ON_CALL(*m_config, lowLatencyMonitoring()).WillByDefault(Return(true));
    ON_CALL(*m_config, lowLatencyThresholdMs()).WillByDefault(Return(25.0));
    ON_CALL(*m_config, monitorGainDb()).WillByDefault(Return(-3.0));
    ON_CALL(*m_config, monitorMuted()).WillByDefault(Return(true));
    EXPECT_TRUE(m_model->monitorInput());
    EXPECT_TRUE(m_model->throughEffects());
    EXPECT_TRUE(m_model->lowLatency());
    EXPECT_DOUBLE_EQ(m_model->thresholdMs(), 25.0);
    EXPECT_DOUBLE_EQ(m_model->gainDb(), -3.0);
    EXPECT_TRUE(m_model->muted());
}

TEST_F(LiveMonitoringModelTests, StatusTexts)
{
    MonitoringStatus st;
    st.enabled = true;
    st.targetTrack = "Vocals";
    st.effects = { "EQ", "Compressor" };
    st.bypassed = { "Breath removal" };
    st.deviceLatencyMs = 20;
    st.effectLatencyMs = 5;
    ON_CALL(*m_record, monitoringStatus()).WillByDefault(Return(st));

    EXPECT_DOUBLE_EQ(m_model->latencyMs(), 25.0);
    EXPECT_EQ(m_model->latencyText().toStdString(), "Monitoring latency ≈ 25 ms (device 20 ms + effects 5 ms)");
    EXPECT_EQ(m_model->effectsText().toStdString(), "Effects heard (Vocals + master): EQ, Compressor");
    EXPECT_TRUE(m_model->hasBypassed());
    EXPECT_EQ(m_model->bypassedText().toStdString(), "Bypassed for low latency: Breath removal");

    ON_CALL(*m_config, monitorThroughEffects()).WillByDefault(Return(false));
    EXPECT_EQ(m_model->effectsText().toStdString(), "Hearing the dry input");
}

TEST_F(LiveMonitoringModelTests, NotificationsAndRefresh)
{
    EXPECT_CALL(*m_record, refreshMonitoring()).Times(0); // no project open
    m_model->init();
    int settings = 0, status = 0;
    QObject::connect(m_model.get(), &LiveMonitoringModel::settingsChanged, [&] { ++settings; });
    QObject::connect(m_model.get(), &LiveMonitoringModel::statusChanged, [&] { ++status; });
    m_gainChanged.notify();
    m_statusChanged.notify();
    EXPECT_EQ(settings, 1);
    EXPECT_EQ(status, 1);
    m_tracksSelected.send({}); // selection change re-plans the chain (status re-emitted)
    EXPECT_EQ(status, 2);
}
