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

TEST(FadeHandlesTests, SnappingPutsTheFadeEdgeOnTheGridWithinLimits)
{
    // clip on [1, 5]: fade-in edge snapped to 2.0 s -> 1 s fade-in
    Fades f = snapEdge({ 0.93, 0.5 }, 1.0, 5.0, true, 2.0);
    EXPECT_DOUBLE_EQ(f.in, 1.0);
    EXPECT_DOUBLE_EQ(f.out, 0.5);
    // fade-out edge snapped to 4.0 s -> 1 s fade-out
    f = snapEdge({ 0.5, 1.1 }, 1.0, 5.0, false, 4.0);
    EXPECT_DOUBLE_EQ(f.out, 1.0);
    // grid points outside the clip or into the other fade are clamped
    EXPECT_DOUBLE_EQ(snapEdge({ 0.1, 0.0 }, 1.0, 5.0, true, 0.0).in, 0.0);
    EXPECT_DOUBLE_EQ(snapEdge({ 3.0, 1.0 }, 1.0, 5.0, true, 6.0).in, 3.0);
    EXPECT_DOUBLE_EQ(snapEdge({ 1.0, 0.2 }, 1.0, 5.0, false, 1.5).out, 3.0);
}

TEST(FadeHandlesTests, ReadoutUsesMillisecondsUnderASecond)
{
    EXPECT_EQ(readout(true, 0.25, 0, false), "Fade in 250 ms");
    EXPECT_EQ(readout(false, 1.25, 0, false), "Fade out 1.25 s");
    EXPECT_EQ(readout(true, 0.0, 0, false), "Fade in 0 ms");
    EXPECT_EQ(readout(true, 0.9996, 0, false), "Fade in 1.00 s");
    EXPECT_EQ(readout(true, 2.0, 1, true), "Fade in 2.00 s \u00b7 equal power");
    EXPECT_EQ(readout(false, 0.05, 0, true), "Fade out 50 ms \u00b7 linear");
}
