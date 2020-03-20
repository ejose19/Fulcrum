//
// Fulcrum - A fast & nimble SPV Server for Bitcoin Cash
// Copyright (C) 2019-2020  Calin A. Culianu <calin.culianu@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program (see LICENSE.txt).  If not, see
// <https://www.gnu.org/licenses/>.
//
#include "Options.h"

#include <QFile>
#include <QIODevice>
#include <QTextStream>

/* static */ void Options::test()
{
    // paranoia
    static_assert ( maxHistoryMin > 0 && maxHistoryMax > 0 && maxBufferMin > 0 && maxBufferMax > 0
                    && defaultMaxBuffer > 0 && defaultMaxHistory > 0, "maxHistory and/or maxBuffer may not be <= 0" );
}

/*static*/ auto Options::Subnet::fromString(const QString &s) -> Subnet
{
    Subnet sn;
    const auto pair = QHostAddress::parseSubnet(s.trimmed());
    sn.subnet = pair.first;
    sn.mask = pair.second;
    return sn;
}

QString Options::Subnet::toString() const  {
    QString ret;
    if (!isValid())
        ret = "<Invalid Subnet>";
    else
        ret = QString("%1/%2").arg(subnet.toString()).arg(mask);
    return ret;
}

bool Options::isAddrInPerIPLimitExcludeSet(const QHostAddress &addr, Subnet *matched) const
{
    // linearly search through excluded subnets --  this is normally only called if some limit was hit (which is rare)
    // so it should hopefully be fast enough.
    for (const auto & sn : subnetsExcludedFromPerIPLimits) {
        if (addr.isInSubnet(sn.subnet, sn.mask)) {
            if (matched) *matched = sn;
            return true;
        }
    }
    return false;
}

QVariantMap Options::toMap() const
{
    QVariantMap m;

    m["debug"] = verboseDebug.load();
    m["trace"] = verboseTrace.load();
    QVariantList l;
    for (const auto & pair : interfaces)
        l.push_back(QString("%1:%2").arg(pair.first.toString()).arg(pair.second));
    m["tcp"] = l;
    l.clear();
    for (const auto & pair : sslInterfaces)
        l.push_back(QString("%1:%2").arg(pair.first.toString()).arg(pair.second));
    m["ssl"] = l;
    l.clear();
    for (const auto & pair : statsInterfaces)
        l.push_back(QString("%1:%2").arg(pair.first.toString()).arg(pair.second));
    m["stats"] = l;
    l.clear();
    for (const auto & pair : adminInterfaces)
        l.push_back(QString("%1:%2").arg(pair.first.toString()).arg(pair.second));
    m["admin"] = l;
    m["cert"] = certFile;
    m["key"] = keyFile;
    m["bitcoind"] = QString("%1:%2").arg(bitcoind.first).arg(bitcoind.second);
    m["hasIPv6 listener"] = hasIPv6Listener;
    m["rpcuser"] = rpcuser.isNull() ? QVariant() : QVariant("<hidden>");
    m["rpcpassword"] = rpcpassword.isNull() ? QVariant() : QVariant("<hidden>");
    m["datadir"] = datadir;
    m["checkdb"] = doSlowDbChecks;
    m["polltime"] = pollTimeSecs;
    m["donation"] = donationAddress;
    m["banner"] = bannerFile;
    m["peering"] = peerDiscovery;
    m["peering_announce_self"] = peerAnnounceSelf;
    m["peering_enforce_unique_ip"] = peeringEnforceUniqueIPs;
    m["hostname"] = hostName.has_value() ? QVariant(hostName.value()) : QVariant();
    m["public_tcp"] = publicTcp.has_value() ? QVariant(publicTcp.value()) : QVariant();
    m["public_ssl"] = publicSsl.has_value() ? QVariant(publicSsl.value()) : QVariant();
    m["max_clients_per_ip"] = maxClientsPerIP;
    l.clear();
    for (const auto & sn : subnetsExcludedFromPerIPLimits)
        l.push_back(sn.toString());
    m["subnets_to_exclude_from_per_ip_limits"] = l;
    m["max_buffer"] = maxBuffer.load();
    m["max_history"] = maxHistory;
    m["workqueue"] = workQueue;
    m["worker_threads"] = workerThreads;
    m["max_pending_connections"] = maxPendingConnections;
    // tor related
    m["tor_hostname"] = torHostName.has_value() ? QVariant(torHostName.value()) : QVariant();
    m["tor_tcp_port"] = torTcp.has_value() ? QVariant(torTcp.value()) : QVariant();
    m["tor_ssl_port"] = torSsl.has_value() ? QVariant(torSsl.value()) : QVariant();
    m["tor_proxy"] = QString("%1:%2").arg(torProxy.first.toString()).arg(torProxy.second);
    m["tor_user"] = torUser;
    m["tor_pass"] = torPass;
    // /tor related
    // bitcoind_throttle params
    const auto [hi, lo, decay] = bdReqThrottleParams.load();
    m["bitcoind_throttle"] = QVariantList{ hi, lo, decay };
    // max_subs_per_ip & max_subs
    m["max_subs_per_ip"] = maxSubsPerIP;
    m["max_subs"] = maxSubsGlobally;
    return m;
}

bool Options::BdReqThrottleParams::isValid() const noexcept
{
    return hi >= lo && hi >= minBDReqHi && hi <= maxBDReqHi && lo >= minBDReqLo && lo <= maxBDReqLo
            && decay >= minBDReqDecayPerSec && decay <= maxBDReqDecayPerSec;
}

// -- ConfigFile

std::optional<QString> ConfigFile::optValue(const QString &name, Qt::CaseSensitivity cs) const
{
    std::optional<QString> ret;

    if (cs == Qt::CaseSensitive) {
        if (auto it = map.find(name); it != map.end())
            ret = it.value();
    } else {
        for (auto it = map.begin(); it != map.end(); ++it) {
            if (it.key().compare(name, cs) == 0) {
                ret = it.value();
                break;
            }
        }
    }
    return ret;
}

int ConfigFile::remove(const QString &name, Qt::CaseSensitivity cs)
{
    if (cs == Qt::CaseSensitive) {
        return map.remove(name);
    } else {
        int ctr = 0;
        for (auto it = map.begin(); it != map.end(); /* */) {
            if (it.key().compare(name, cs) == 0) {
                it = map.erase(it);
                ++ctr;
            } else
                ++it;
        }
        return ctr;
    }
}

bool ConfigFile::hasValue(const QString &name, Qt::CaseSensitivity cs) const
{
    return optValue(name, cs).has_value();
}

QString ConfigFile::value(const QString &name, const QString & def, Qt::CaseSensitivity cs) const
{
    return optValue(name, cs).value_or(def);
}

QStringList ConfigFile::values(const QString &name, Qt::CaseSensitivity cs) const
{
    QStringList ret;
    if (cs == Qt::CaseSensitive) {
        ret = map.values(name);
    } else {
        for (auto it = map.begin(); it != map.end(); ++it) {
            if (it.key().compare(name, cs) == 0)
                ret.push_back(it.value());
        }
    }
    return ret;
}

bool ConfigFile::open(const QString &filePath)
{
    static const QChar comment('#'), eq('='), bracket('[');
    clear();
    QList<QByteArray> lines;
    {
        QFile f(filePath);
        if (!f.exists() || !f.open(QIODevice::ReadOnly|QIODevice::Text))
            return false;
        auto fileData = f.read(1024*1024*1); // read up to 1 MB
        if (fileData.isEmpty())
            return f.error() == QFile::FileError::NoError;
        lines = fileData.split('\n');
    }
    for (const auto & lineData : lines) {
        QString line = QString::fromUtf8(lineData).trimmed();
        if (!line.isEmpty() && line.at(0) == bracket)
            continue; // ignore "[section]" headers in case the user thinks we support these
        if (const int cpos = line.indexOf(comment); cpos > -1) // find and throw away comments (everything after '#' char)
            line = line.left(cpos).trimmed();
        if (line.isEmpty())
            continue;
        QString name, value;
        if (const int eqpos = line.indexOf(eq); eqpos > -1) {
            name = line.left(eqpos).trimmed();
            if (name.isEmpty())
                continue;
            value = line.mid(eqpos+1).trimmed();
        } else
            // a name by itself on a line with no equal sign, becomes name=(emptystring), which might be useful to
            // indicate the name was present (can act like a bool flag if present)
            name = line;
        // save item
        map.insertMulti(name, value);
    }

    map.squeeze();

    return true;
}

bool ConfigFile::boolValue(const QString & name, bool def, bool *parsedOk, Qt::CaseSensitivity cs) const
{
    return genericParseArithmeticValue(name, def, parsedOk, cs);
}
int ConfigFile::intValue(const QString & name, int def, bool *parsedOk, Qt::CaseSensitivity cs) const
{
    return genericParseArithmeticValue(name, def, parsedOk, cs);
}
int64_t ConfigFile::int64Value(const QString & name, int64_t def, bool *parsedOk, Qt::CaseSensitivity cs) const
{
    return genericParseArithmeticValue(name, def, parsedOk, cs);
}
double ConfigFile::doubleValue(const QString & name, double def, bool *parsedOk, Qt::CaseSensitivity cs) const
{
    return genericParseArithmeticValue(name, def, parsedOk, cs);
}

template <typename Numeric, std::enable_if_t<std::is_arithmetic_v<Numeric>, int> >
Numeric ConfigFile::genericParseArithmeticValue(const QString &name, Numeric def, bool *parsedOk, Qt::CaseSensitivity cs) const
{
    auto Convert = [](QString val, bool *parsedOk) -> auto {
        val = val.toLower();
        using Type = std::remove_cv_t<std::remove_reference_t<Numeric>>;
        if constexpr (std::is_same_v<Type, double>)
            return Type(val.toDouble(parsedOk));
        else if constexpr (std::is_same_v<Type, float>)
            return Type(val.toFloat(parsedOk));
        else if constexpr (std::is_same_v<Type, int>)
            return Type(val.toInt(parsedOk));
        else if constexpr (std::is_same_v<Type, long>)
            return Type(val.toLong(parsedOk));
        else if constexpr (std::is_same_v<Type, int64_t> || std::is_same_v<Type, qlonglong>)
            return Type(val.toLongLong(parsedOk));
        else if constexpr (std::is_same_v<Type, bool>) {
            // special support for "true", "yes", "on", "" for true and "false", "no", "off" for false
            if (val == "true" || val == "yes" || val == "on" || val.isEmpty() /* "" means true! */) { if (parsedOk){*parsedOk = true;}  return true; }
            else if (val == "false" || val == "no" || val == "off") { if (parsedOk){*parsedOk = true;} return false; }
            return Type(val.toInt(parsedOk));
        } else {
            return nullptr; // failed to deduce which method to use
        }
    };
    static_assert(!std::is_same_v<std::nullptr_t, decltype(Convert("", nullptr))>, "Failed to deduce type conversion function!");
    bool dummy;
    bool & ok ( parsedOk ? *parsedOk : dummy );
    ok = false;
    const auto opt = optValue(name, cs);
    if (!opt.has_value())
        return def;
    Numeric ret = Convert(opt.value(), &ok);
    if (!ok) return def;
    return ret;
}
