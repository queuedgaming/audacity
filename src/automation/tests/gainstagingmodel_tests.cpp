/*
* Audacity: A Digital Audio Editor
*/
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "automation/view/gainstagingmodel.h"
#include "mocks/mixermocks.h"

using namespace ::testing;

namespace au::automation {
class GainStagingModelTest : public Test
{
protected:
    void SetUp() override
    {
        m_gain = std::make_shared<NiceMock<GainStagingInteractionMock> >();
        m_model = std::make_unique<GainStagingModel>();
        m_model->gainStaging.set(m_gain);

        TrackLevels vox;
        vox.trackId = 1;
        vox.name = "Vox";
        vox.channels = 1;
        vox.peakDb = -3.0;
        vox.truePeakDb = -2.5;
        vox.rmsDb = -21.0;
        vox.lufs = -20.0;
        vox.silent = false;
        TrackLevels drums = vox;
        drums.trackId = 2;
        drums.name = "Drums";
        drums.channels = 2;
        drums.peakDb = -0.2;
        drums.truePeakDb = 0.4;
        drums.rmsDb = -12.0;
        drums.lufs = -11.0;
        drums.trimDb = 2.0;
        TrackLevels empty;
        empty.trackId = 3;
        empty.name = "Empty";
        ON_CALL(*m_gain, analyze()).WillByDefault(Return(std::vector<TrackLevels> { vox, drums, empty }));
    }

    QVariantMap row(int i) const { return m_model->rows().at(i).toMap(); }

    std::shared_ptr<NiceMock<GainStagingInteractionMock> > m_gain;
    std::unique_ptr<GainStagingModel> m_model;
};
}

using namespace au::automation;

TEST_F(GainStagingModelTest, SuggestsTrimsForTheTargetAndAppliesThemAsOneStep)
{
    EXPECT_EQ(m_model->targetKind(), int(GainStagingModel::Rms));
    EXPECT_DOUBLE_EQ(m_model->target(), -18.0);
    m_model->analyze();
    ASSERT_EQ(m_model->rows().size(), 3);
    EXPECT_EQ(row(0).value("name").toString(), "Vox");
    EXPECT_DOUBLE_EQ(row(0).value("rmsDb").toDouble(), -21.0);
    EXPECT_DOUBLE_EQ(row(0).value("suggestedDb").toDouble(), 3.0);  // -18 - (-21)
    EXPECT_DOUBLE_EQ(row(1).value("suggestedDb").toDouble(), -6.0); // -18 - (-12)
    EXPECT_DOUBLE_EQ(row(1).value("trimDb").toDouble(), 2.0);
    EXPECT_FALSE(row(2).value("hasSuggestion").toBool()); // silent: nothing to suggest
    EXPECT_FALSE(row(2).value("use").toBool());
    EXPECT_EQ(m_model->applicableCount(), 2);
    // Vox +3 dB: true peak -2.5 -> +0.5 dBTP, flagged
    EXPECT_TRUE(row(0).value("overAfter").toBool());
    EXPECT_DOUBLE_EQ(row(0).value("truePeakAfterDb").toDouble(), 0.5);
    EXPECT_FALSE(row(1).value("overAfter").toBool());

    // user-set target
    m_model->setTarget(-20.0);
    EXPECT_DOUBLE_EQ(row(0).value("suggestedDb").toDouble(), 1.0);
    // loudness target, default -23 LUFS
    m_model->setTargetKind(GainStagingModel::Lufs);
    EXPECT_DOUBLE_EQ(m_model->target(), -23.0);
    EXPECT_DOUBLE_EQ(row(0).value("suggestedDb").toDouble(), -3.0);
    EXPECT_DOUBLE_EQ(row(1).value("suggestedDb").toDouble(), -12.0);
    // true-peak target, default -1 dBTP
    m_model->setTargetKind(GainStagingModel::Peak);
    EXPECT_DOUBLE_EQ(m_model->target(), -1.0);
    EXPECT_DOUBLE_EQ(row(1).value("suggestedDb").toDouble(), -1.4);

    // untick Drums: only Vox is applied, in one call (= one undo step)
    m_model->setTargetKind(GainStagingModel::Rms);
    m_model->setUse(1, false);
    EXPECT_EQ(m_model->applicableCount(), 1);
    std::map<au::trackedit::TrackId, double> applied;
    EXPECT_CALL(*m_gain, applyTrims(_)).WillOnce([&](const std::map<au::trackedit::TrackId, double>& t) {
        applied = t;
        return int(t.size());
    });
    EXPECT_EQ(m_model->apply(), 1);
    ASSERT_EQ(applied.size(), 1u);
    EXPECT_DOUBLE_EQ(applied.at(1), 3.0);
    EXPECT_DOUBLE_EQ(row(0).value("trimDb").toDouble(), 3.0); // the table shows the new trim

    // both ticked: both in a single call
    m_model->setUse(1, true);
    EXPECT_CALL(*m_gain, applyTrims(SizeIs(2))).WillOnce(Return(2));
    EXPECT_EQ(m_model->apply(), 2);
}
