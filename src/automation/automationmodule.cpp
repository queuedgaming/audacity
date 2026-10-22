#include "automationmodule.h"

#include "internal/au3/au3clipgaininteraction.h"
#include "internal/au3/au3paramautomationinteraction.h"
#include "internal/au3/au3busroutinginteraction.h"

#include "view/clipgainmodel.h"
#include "view/paramautomationlanemodel.h"
#include "view/busroutinggraphmodel.h"
#include "view/mixerpanelmodel.h"

#include <QtQml/qqml.h>

using namespace au::automation;

static const std::string mname("automation");

AutomationModule::AutomationModule()
{}

std::string AutomationModule::moduleName() const
{
    return mname;
}

void AutomationModule::registerUiTypes()
{
    qmlRegisterType<ClipGainModel>("Audacity.Automation", 1, 0, "ClipGainModel");
    qmlRegisterType<ParamAutomationLaneModel>("Audacity.Automation", 1, 0, "ParamAutomationLaneModel");
    qmlRegisterType<BusRoutingGraphModel>("Audacity.Automation", 1, 0, "BusRoutingGraphModel");
    qmlRegisterType<MixerPanelModel>("Audacity.Automation", 1, 0, "MixerPanelModel");
}

muse::modularity::IContextSetup* AutomationModule::newContext(
    const muse::modularity::ContextPtr& ctx) const
{
    return new AutomationContext(ctx);
}

// =====================================================
// ProjectSceneContext
// =====================================================

void AutomationContext::registerExports()
{
    ioc()->registerExport<IClipGainInteraction>(mname, std::make_shared<Au3ClipGainInteraction>(iocContext()));
    ioc()->registerExport<IParamAutomationInteraction>(mname, std::make_shared<Au3ParamAutomationInteraction>(iocContext()));
    ioc()->registerExport<IBusRoutingInteraction>(mname, std::make_shared<Au3BusRoutingInteraction>(iocContext()));
}
