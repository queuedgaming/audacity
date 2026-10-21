/*
* Audacity: A Digital Audio Editor
*/
#include <gtest/gtest.h>

#include "view/tracksitemsview/fadehandles.h"

using namespace au::projectscene::fadehandles;

TEST(FadeHandlesTests, DraggingTowardsTheCentreLengthensEitherFade)
{
    const Fades f = drag({ 0.0, 0.0 }, 4.0, true, 0.5);
    EXPECT_DOUBLE_EQ(f.in, 0.5);
    EXPECT_DOUBLE_EQ(f.out, 0.0);
    const Fades g = drag({ 0.5, 0.0 }, 4.0, false, -1.0);
    EXPECT_DOUBLE_EQ(g.in, 0.5);
    EXPECT_DOUBLE_EQ(g.out, 1.0);
    // and back towards the edge shortens it
    EXPECT_DOUBLE_EQ(drag({ 1.0, 0.0 }, 4.0, true, -0.25).in, 0.75);
    EXPECT_DOUBLE_EQ(drag({ 0.0, 1.0 }, 4.0, false, 0.25).out, 0.75);
}

TEST(FadeHandlesTests, FadesStayWithinTheClipAndNeverOverlap)
{
    EXPECT_DOUBLE_EQ(drag({ 0.3, 0.0 }, 4.0, true, -5.0).in, 0.0);
    EXPECT_DOUBLE_EQ(drag({ 0.0, 0.3 }, 4.0, false, 5.0).out, 0.0);
    EXPECT_DOUBLE_EQ(drag({ 0.0, 0.0 }, 4.0, true, 10.0).in, 4.0);
    // combined in + out <= clip length
    const Fades a = drag({ 0.0, 1.5 }, 4.0, true, 10.0);
    EXPECT_DOUBLE_EQ(a.in, 2.5);
    EXPECT_DOUBLE_EQ(a.out, 1.5);
    const Fades b = drag({ 3.0, 0.0 }, 4.0, false, -10.0);
    EXPECT_DOUBLE_EQ(b.out, 1.0);
    EXPECT_LE(b.in + b.out, 4.0);
    // degenerate clip
    EXPECT_DOUBLE_EQ(drag({ 0.0, 0.0 }, 0.0, true, 1.0).in, 0.0);
}

TEST(FadeHandlesTests, ShiftOnReleaseTogglesTheCurve)
{
    EXPECT_EQ(curveAfterDrag(0, false), 0);
    EXPECT_EQ(curveAfterDrag(1, false), 1);
    EXPECT_EQ(curveAfterDrag(0, true), 1);
    EXPECT_EQ(curveAfterDrag(1, true), 0);
}
