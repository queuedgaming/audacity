/*
 * Audacity: A Digital Audio Editor
 */
#include "projectkey.h"

#include "au3-xml/XMLAttributeValueView.h"
#include "au3-xml/XMLWriter.h"

#include <algorithm>

using namespace au::au3;

static const AttachedProjectObjects::RegisteredFactory projectKeyFactory
{
    [](AudacityProject&) {
        return std::make_shared<ProjectKey>();
    }
};

ProjectKey& ProjectKey::Get(AudacityProject& project)
{
    return project.AttachedObjects::Get<ProjectKey&>(projectKeyFactory);
}

const ProjectKey& ProjectKey::Get(const AudacityProject& project)
{
    return Get(const_cast<AudacityProject&>(project));
}

static ProjectFileIORegistry::AttributeWriterEntry entry {
    [](const AudacityProject& project, XMLWriter& xmlFile){
        const auto& k = ProjectKey::Get(project).key();
        if (!k) {
            return;
        }
        xmlFile.WriteAttr(wxT("musical_key_tonic"), k->tonic);
        xmlFile.WriteAttr(wxT("musical_key_minor"), k->minor);
        xmlFile.WriteAttr(wxT("musical_key_tuning_cents"), k->tuningCents);
    }
};

static ProjectFileIORegistry::AttributeReaderEntries entries {
    (ProjectKey & (*)(AudacityProject&)) & ProjectKey::Get, {
        { "musical_key_tonic", [](auto& pk, auto value) {
                auto k = pk.key().value_or(ProjectKey::Key {});
                k.tonic = std::clamp(value.Get(0), 0, 11);
                pk.setKey(k);
            } },
        { "musical_key_minor", [](auto& pk, auto value) {
                auto k = pk.key().value_or(ProjectKey::Key {});
                k.minor = value.Get(false);
                pk.setKey(k);
            } },
        { "musical_key_tuning_cents", [](auto& pk, auto value) {
                auto k = pk.key().value_or(ProjectKey::Key {});
                k.tuningCents = std::clamp(value.Get(0.0), -50.0, 50.0);
                pk.setKey(k);
            } }
    }
};
