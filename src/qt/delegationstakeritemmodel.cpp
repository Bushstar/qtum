#include <qt/delegationstakeritemmodel.h>
#include <qt/walletmodel.h>
#include <interfaces/wallet.h>
#include <validation.h>
#include <qt/bitcoinunits.h>
#include <interfaces/node.h>
#include <interfaces/handler.h>
#include <algorithm>

#include <QDateTime>
#include <QFont>
#include <QDebug>
#include <QThread>

class DelegationStakerItemEntry
{
public:
    DelegationStakerItemEntry()
    {}

    DelegationStakerItemEntry(const interfaces::DelegationStakerInfo &delegationStakerInfo)
    {
        delegateAddress = QString::fromStdString(delegationStakerInfo.delegate_address);
        stakerAddress = QString::fromStdString(delegationStakerInfo.staker_address);
        PoD = QString::fromStdString(delegationStakerInfo.PoD);
        fee = delegationStakerInfo.fee;
        time = delegationStakerInfo.time;
        blockNumber = delegationStakerInfo.block_number;
        hash = delegationStakerInfo.hash;
    }

    DelegationStakerItemEntry(const DelegationStakerItemEntry &obj)
    {
        delegateAddress = obj.delegateAddress;
        stakerAddress = obj.stakerAddress;
        PoD = obj.PoD;
        fee = obj.fee;
        time = obj.time;
        blockNumber = obj.blockNumber;
        hash = obj.hash;
    }

    ~DelegationStakerItemEntry()
    {}

    QString delegateAddress;
    QString stakerAddress;
    QString PoD;
    quint8 fee = 0;
    qint64 time = 0;
    qint64 blockNumber = -1;
    uint160 hash;
};

struct DelegationStakerItemEntryLessThan
{
    bool operator()(const DelegationStakerItemEntry &a, const DelegationStakerItemEntry &b) const
    {
        return a.hash < b.hash;
    }
    bool operator()(const DelegationStakerItemEntry &a, const uint160 &b) const
    {
        return a.hash < b;
    }
    bool operator()(const uint160 &a, const DelegationStakerItemEntry &b) const
    {
        return a < b.hash;
    }
};

class DelegationStakerItemPriv
{
public:
    QList<DelegationStakerItemEntry> cachedDelegationStakerItem;
    DelegationStakerItemModel *parent;

    DelegationStakerItemPriv(DelegationStakerItemModel *_parent):
        parent(_parent) {}

    void refreshDelegationStakerItem(interfaces::Wallet& wallet)
    {
        cachedDelegationStakerItem.clear();
        {
            for(interfaces::DelegationStakerInfo delegationStaker : wallet.getDelegationsStakers())
            {
                DelegationStakerItemEntry delegationStakerItem(delegationStaker);
                cachedDelegationStakerItem.append(delegationStakerItem);
            }
        }
        std::sort(cachedDelegationStakerItem.begin(), cachedDelegationStakerItem.end(), DelegationStakerItemEntryLessThan());
    }

    void updateEntry(const DelegationStakerItemEntry &item, int status)
    {
        // Find delegation staker in model
        QList<DelegationStakerItemEntry>::iterator lower = qLowerBound(
            cachedDelegationStakerItem.begin(), cachedDelegationStakerItem.end(), item, DelegationStakerItemEntryLessThan());
        QList<DelegationStakerItemEntry>::iterator upper = qUpperBound(
            cachedDelegationStakerItem.begin(), cachedDelegationStakerItem.end(), item, DelegationStakerItemEntryLessThan());
        int lowerIndex = (lower - cachedDelegationStakerItem.begin());
        int upperIndex = (upper - cachedDelegationStakerItem.begin());
        bool inModel = (lower != upper);

        switch(status)
        {
        case CT_NEW:
            if(inModel)
            {
                qWarning() << "DelegationStakerItemPriv::updateEntry: Warning: Got CT_NEW, but entry is already in model";
                break;
            }
            parent->beginInsertRows(QModelIndex(), lowerIndex, lowerIndex);
            cachedDelegationStakerItem.insert(lowerIndex, item);
            parent->endInsertRows();
            break;
        case CT_UPDATED:
            if(!inModel)
            {
                qWarning() << "DelegationStakerItemPriv::updateEntry: Warning: Got CT_UPDATED, but entry is not in model";
                break;
            }
            cachedDelegationStakerItem[lowerIndex] = item;
            parent->emitDataChanged(lowerIndex);
            break;
        case CT_DELETED:
            if(!inModel)
            {
                qWarning() << "DelegationStakerItemPriv::updateEntry: Warning: Got CT_DELETED, but entry is not in model";
                break;
            }
            parent->beginRemoveRows(QModelIndex(), lowerIndex, upperIndex-1);
            cachedDelegationStakerItem.erase(lower, upper);
            parent->endRemoveRows();
            break;
        }
    }

    int size()
    {
        return cachedDelegationStakerItem.size();
    }

    DelegationStakerItemEntry *index(int idx)
    {
        if(idx >= 0 && idx < cachedDelegationStakerItem.size())
        {
            return &cachedDelegationStakerItem[idx];
        }
        else
        {
            return 0;
        }
    }
};

DelegationStakerItemModel::DelegationStakerItemModel(WalletModel *parent):
    QAbstractItemModel(parent),
    walletModel(parent),
    priv(0)
{
    columns << tr("Date") << tr("Delegate") << tr("Fee") << tr("PoD");

    priv = new DelegationStakerItemPriv(this);
    priv->refreshDelegationStakerItem(walletModel->wallet());

    subscribeToCoreSignals();
}

DelegationStakerItemModel::~DelegationStakerItemModel()
{
    unsubscribeFromCoreSignals();

    if(priv)
    {
        delete priv;
        priv = 0;
    }
}

QModelIndex DelegationStakerItemModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    DelegationStakerItemEntry *data = priv->index(row);
    if(data)
    {
        return createIndex(row, column, priv->index(row));
    }
    return QModelIndex();
}

QModelIndex DelegationStakerItemModel::parent(const QModelIndex &child) const
{
    Q_UNUSED(child);
    return QModelIndex();
}

int DelegationStakerItemModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int DelegationStakerItemModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant DelegationStakerItemModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();

    DelegationStakerItemEntry *rec = static_cast<DelegationStakerItemEntry*>(index.internalPointer());

    switch (role) {
    case Qt::DisplayRole:
        switch(index.column())
        {
        case Date:
            return QDateTime::fromTime_t(static_cast<uint>(rec->time));
        case Delegate:
            return rec->delegateAddress;
        case Fee:
            return rec->fee;
        case PoD:
            return rec->PoD;
        default:
            break;
        }
        break;
    case DelegationStakerItemModel::HashRole:
        return QString::fromStdString(rec->hash.ToString());
        break;
    case DelegationStakerItemModel::DelegateRole:
        return rec->delegateAddress;
        break;
    case DelegationStakerItemModel::StakerRole:
        return rec->stakerAddress;
        break;
    case DelegationStakerItemModel::PoDRole:
        return rec->PoD;
        break;
    case DelegationStakerItemModel::FeeRole:
        return rec->fee;
        break;
    case DelegationStakerItemModel::DateRole:
        return QDateTime::fromTime_t(static_cast<uint>(rec->time));
        break;
    case DelegationStakerItemModel::BlockNumberRole:
        return rec->blockNumber;
        break;
    default:
        break;
    }

    return QVariant();
}

void DelegationStakerItemModel::updateDelegationStakerData(const QString &hash, int status, bool showDelegationStaker)
{
    // Find delegationStaker in wallet
    uint160 updated;
    updated.SetHex(hash.toStdString());
    interfaces::DelegationStakerInfo delegationStaker =walletModel->wallet().getDelegationStaker(updated);
    showDelegationStaker &= delegationStaker.hash == updated;

    DelegationStakerItemEntry delegationStakerEntry;
    if(showDelegationStaker)
    {
        delegationStakerEntry = DelegationStakerItemEntry(delegationStaker);
    }
    else
    {
        delegationStakerEntry.hash = updated;
    }
    priv->updateEntry(delegationStakerEntry, status);
}

void DelegationStakerItemModel::emitDataChanged(int idx)
{
    Q_EMIT dataChanged(index(idx, 0, QModelIndex()), index(idx, columns.length()-1, QModelIndex()));
}

struct DelegationStakerNotification
{
public:
    DelegationStakerNotification() {}
    DelegationStakerNotification(uint160 _hash, ChangeType _status, bool _showDelegationStaker):
        hash(_hash), status(_status), showDelegationStaker(_showDelegationStaker) {}

    void invoke(QObject *tim)
    {
        QString strHash = QString::fromStdString(hash.GetHex());
        qDebug() << "NotifyDelegationsStakerChanged: " + strHash + " status= " + QString::number(status);

        QMetaObject::invokeMethod(tim, "updateDelegationStakerData", Qt::QueuedConnection,
                                  Q_ARG(QString, strHash),
                                  Q_ARG(int, status),
                                  Q_ARG(bool, showDelegationStaker));
    }
private:
    uint160 hash;
    ChangeType status;
    bool showDelegationStaker;
};

static void NotifyDelegationsStakerChanged(DelegationStakerItemModel *tim, const uint160 &hash, ChangeType status)
{
    DelegationStakerNotification notification(hash, status, true);
    notification.invoke(tim);
}

void DelegationStakerItemModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    m_handler_delegationsstaker_changed = walletModel->wallet().handleDelegationsStakerChanged(boost::bind(NotifyDelegationsStakerChanged, this, _1, _2));
}

void DelegationStakerItemModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    m_handler_delegationsstaker_changed->disconnect();
}
