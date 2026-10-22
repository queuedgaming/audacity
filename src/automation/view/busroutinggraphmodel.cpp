/*
* Audacity: A Digital Audio Editor
*/
#include "busroutinggraphmodel.h"

#include "translation.h"

using namespace au::automation;

BusRoutingGraphModel::BusRoutingGraphModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this)) {}

void BusRoutingGraphModel::init()
{
    if (routing()) {
        routing()->changed().onNotify(this, [this]() { reload(); });
    }
    if (projectHistory()) {
        projectHistory()->historyChanged().onReceive(this, [this](const auto&) { reload(); });
    }
    reload();
}

void BusRoutingGraphModel::reload()
{
    m_nodes = routing() ? routing()->nodes() : std::vector<RoutingNode> {};
    emit changed();
}

QVariantMap BusRoutingGraphModel::toMap(const RoutingNode& n) const
{
    QVariantMap m;
    m["trackId"] = qlonglong(n.trackId);
    m["name"] = QString::fromStdString(n.name);
    m["isBus"] = n.isBus;
    m["busId"] = QString::fromStdString(n.busId);
    m["output"] = QString::fromStdString(n.outputBusId);
    QVariantList sends;
    for (const auto& s : n.sends) {
        QVariantMap sm;
        sm["busId"] = QString::fromStdString(s.busId);
        sm["level"] = double(s.level);
        sends << sm;
    }
    m["sends"] = sends;
    return m;
}

QVariantList BusRoutingGraphModel::tracks() const
{
    QVariantList list;
    for (const auto& n : m_nodes) {
        if (!n.isBus) {
            list << toMap(n);
        }
    }
    return list;
}

QVariantList BusRoutingGraphModel::buses() const
{
    QVariantList list;
    for (const auto& n : m_nodes) {
        if (n.isBus) {
            list << toMap(n);
        }
    }
    return list;
}

void BusRoutingGraphModel::setMessage(const QString& m)
{
    if (m_message != m) {
        m_message = m;
        emit messageChanged();
    }
}

void BusRoutingGraphModel::addBus()
{
    if (routing()) {
        routing()->addBus();
        setMessage({});
    }
}

bool BusRoutingGraphModel::canRouteTo(qlonglong trackId, const QString& busId) const
{
    return routing() && routing()->canRouteTo(trackedit::TrackId(trackId), busId.toStdString());
}

bool BusRoutingGraphModel::setOutput(qlonglong trackId, const QString& busId)
{
    if (!routing()) {
        return false;
    }
    if (!routing()->canRouteTo(trackedit::TrackId(trackId), busId.toStdString())) {
        setMessage(muse::qtrc("automation", "Not connected: this route would create a feedback loop"));
        return false;
    }
    const bool ok = routing()->setOutput(trackedit::TrackId(trackId), busId.toStdString());
    setMessage({});
    return ok;
}

bool BusRoutingGraphModel::setSend(qlonglong trackId, const QString& busId, double level, bool completed)
{
    if (!routing()) {
        return false;
    }
    if (!routing()->canRouteTo(trackedit::TrackId(trackId), busId.toStdString())) {
        setMessage(muse::qtrc("automation", "Not connected: this send would create a feedback loop"));
        return false;
    }
    const bool ok = routing()->setSend(trackedit::TrackId(trackId), busId.toStdString(), float(level), completed);
    if (!ok) {
        setMessage(muse::qtrc("automation", "A send cannot go to the bus the output already goes to"));
    } else {
        setMessage({});
    }
    return ok;
}

bool BusRoutingGraphModel::removeSend(qlonglong trackId, const QString& busId)
{
    return routing() && routing()->removeSend(trackedit::TrackId(trackId), busId.toStdString());
}
