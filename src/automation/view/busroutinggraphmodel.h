/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <QObject>
#include <QVariantList>

#include "framework/global/async/asyncable.h"
#include "framework/global/modularity/ioc.h"
#include "trackedit/iprojecthistory.h"

#include "automation/ibusroutinginteraction.h"

namespace au::automation {
//! Model of the bus routing node graph panel: tracks -> buses -> master
class BusRoutingGraphModel : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList buses READ buses NOTIFY changed FINAL)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged FINAL)

    muse::ContextInject<IBusRoutingInteraction> routing{ this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory{ this };

public:
    explicit BusRoutingGraphModel(QObject* parent = nullptr);

    QVariantList tracks() const;
    QVariantList buses() const;
    QString message() const { return m_message; }

    Q_INVOKABLE void init();
    Q_INVOKABLE void addBus();
    //! busId "" = master
    Q_INVOKABLE bool canRouteTo(qlonglong trackId, const QString& busId) const;
    Q_INVOKABLE bool setOutput(qlonglong trackId, const QString& busId);
    Q_INVOKABLE bool setSend(qlonglong trackId, const QString& busId, double level, bool completed);
    Q_INVOKABLE bool removeSend(qlonglong trackId, const QString& busId);

signals:
    void changed();
    void messageChanged();

private:
    void reload();
    QVariantMap toMap(const RoutingNode& n) const;
    void setMessage(const QString& m);

    std::vector<RoutingNode> m_nodes;
    QString m_message;
};
}
