/*
 * Audacity: A Digital Audio Editor
 */
#include "projectkey.h"

#include "au3-xml/XMLAttributeValueView.h"
#include "au3-xml/XMLWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "au3-project-history/UndoManager.h"

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

void ProjectKey::setKey(const std::optional<Key>& key)
{
    m_key = key;
    m_keyChanged.notify();
}

std::string ProjectKey::tonicName(int tonic)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[((tonic % 12) + 12) % 12];
}

std::string ProjectKey::shortLabel(const std::optional<Key>& key)
{
    if (!key) {
        return "No key";
    }
    const double hz = 440.0 * std::pow(2.0, key->tuningCents / 1200.0);
    const double rounded = std::round(hz * 10.0) / 10.0;
    char buf[32];
    if (std::fabs(rounded - std::round(rounded)) < 0.05) {
        std::snprintf(buf, sizeof(buf), "A%d", int(std::lround(rounded)));
    } else {
        std::snprintf(buf, sizeof(buf), "A%.1f", rounded);
    }
    return tonicName(key->tonic) + (key->minor ? " min, " : " maj, ") + buf;
}

namespace {
//! Undo/redo: every history state remembers the key it had
class ProjectKeyRestorer final : public UndoStateExtension
{
public:
    explicit ProjectKeyRestorer(AudacityProject& project)
        : m_key(ProjectKey::Get(project).key()) {}

    void RestoreUndoRedoState(AudacityProject& project) override
    {
        auto& pk = ProjectKey::Get(project);
        const auto& cur = pk.key();
        const bool same = cur.has_value() == m_key.has_value()
                          && (!cur || (cur->tonic == m_key->tonic && cur->minor == m_key->minor
                                       && cur->tuningCents == m_key->tuningCents));
        if (!same) {
            pk.setKey(m_key);
        }
    }

private:
    std::optional<ProjectKey::Key> m_key;
};

UndoRedoExtensionRegistry::Entry<ProjectKeyRestorer> sRestorerEntry {
    [](AudacityProject& project) -> std::shared_ptr<UndoStateExtension> {
        return std::make_shared<ProjectKeyRestorer>(project);
    }
};
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
