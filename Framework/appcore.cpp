#include "appcore.h"
#include "tools.h"
#include "sqlitework.h"

#include <QDir>
#include <QImage>
#include <QImageWriter>
#include <QUrl>
#include <QJsonDocument>
#include <QDebug>
#include <QImageReader>
#include <QBuffer>
#include <QUuid>
#include <QDesktopServices>
#include <QPrinter>
#include <QPrintDialog>
#include <QProcess>
#include <QGraphicsObject>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

const QString& g_tablename = "xiantao";
const QString& g_dbpath = "/data/data.db3";
const QString& g_templatepath = "/data/template.dot";

CAppCore::CAppCore(QObject* parent)
    : QObject(parent)
    , m_config(new CAppConfig(this))
    , m_infoModel(new CTableModel(this))
    , m_statsModel(new CTableModel(this))
{
    connect(m_infoModel, &CTableModel::refreshData, this, &CAppCore::onRefreshData);
    connect(m_infoModel, &CTableModel::removeData, this, &CAppCore::onDeleteData);
}

CAppCore::~CAppCore()
{

}

void CAppCore::init()
{
    m_config->init();
    initDetailModel();
    initStatsModel();
}

void CAppCore::updateInfomation()
{
    QString error;
    CSqliteWork sqliteWork(qApp->applicationDirPath() + g_dbpath, error);
    if (error.isEmpty())
    {
        sqliteWork.setActivedTable(g_tablename);
        QList<CTableRow> rowList;
        QMap<QString, QString> data;
        sqliteWork.selectAll(data);
        for (auto itr = data.begin(); itr != data.end(); ++itr)
        {
            CTableRow row;
            row.setId(itr.key());
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(itr->toUtf8(), &error);
            row.setData(doc.object().toVariantMap());
            rowList.push_back(row);
        }
        m_infoModel->setRowList(rowList);
    }
    updateStats();
}

QObject* CAppCore::config() const
{
    return m_config;
}

QObject* CAppCore::infoModel() const
{
    return m_infoModel;
}

QObject* CAppCore::statsModel() const
{
    return m_statsModel;
}

QString CAppCore::importExcel(const QString& filePath)
{
    QList<XlsxTool::XlsxRow> xlsxRows;
    QMap<int, XlsxTool::XlsxImage> xlsxImages;
    XlsxTool::getData(QUrl(filePath).toLocalFile(), xlsxRows, xlsxImages);
    int rowCount = xlsxRows.count();
    if (rowCount <= 0) return u8"导入的数据为空！";

    XlsxTool::XlsxRow columnList = xlsxRows[0];
    QMap<int, QString> mapRole;
    for (auto colIndex = 0; colIndex < columnList.count(); ++colIndex)
    {
        QString role = m_config->getItemRole(columnList[colIndex].toString());
        if(role.isEmpty())
        {
            mapRole.clear();
            break;
        }
        mapRole[colIndex] = role;
    }
    if(mapRole.isEmpty()) return u8"导入的格式有误！";
    QList<CTableRow> tableRowList;
    for (auto rowIndex = 1; rowIndex < rowCount; ++rowIndex)
    {
        XlsxTool::XlsxRow xlsxRow = xlsxRows[rowIndex];
        CTableRow tableRow;
        for (auto colIndex = 0; colIndex < xlsxRow.count(); ++colIndex)
        {
            QString role = mapRole.value(colIndex);
            if(role.isEmpty()) continue;
            tableRow.setValue(role, xlsxRow[colIndex].toString());
            if (role == "identity")
            {
                tableRow.setId(xlsxRow[colIndex].toString());
            }
        }
        if(tableRow.id().isEmpty())
        {
            tableRow.setId(QUuid::createUuid().toString());
        }
        tableRowList.push_back(tableRow);
    }
    QPair<int, int> retValue = m_infoModel->refreshRowList(tableRowList);
    return  QString(u8"新增：") + QString::number(retValue.first) + u8"条；更新：" + QString::number(retValue.second) + u8"条";
}

void CAppCore::exportDocument(const QString& savePath, const QString& id)
{
    QString destPath = QUrl(savePath).toLocalFile();
    QFile::remove(destPath);
    QJsonObject rowData = m_infoModel->rowData(id);
    QVariantMap roleData = rowData.toVariantMap();
    QPair<QString, QString> photo;
    auto itrPhoto = roleData.find("photo");
    if (itrPhoto != roleData.end())
    {
        photo = { "photo", qApp->applicationDirPath() + itrPhoto->toString() };
        roleData.erase(itrPhoto);
    }
    WordTool::saveInfomation(qApp->applicationDirPath() + g_templatepath, roleData, photo, destPath);
    QDesktopServices::openUrl(destPath);
}

void CAppCore::exportExcel(const QString& savePath, const QJsonArray& ids, bool open)
{
    QString destPath = QUrl(savePath).toLocalFile();
    QFile::remove(destPath);
    QJsonArray itemColumns = m_config->itemColumns();
    QMap<QString, int> mapCol;
    QList<QPair<QString, double>> columnList;
    for (auto col = 0; col < itemColumns.size(); ++col)
    {
        QJsonObject obj = itemColumns[col].toObject();
        mapCol[obj.value("role").toString()] = col;
        columnList.push_back({ obj.value("name").toString(), obj.value("xlsxWidth").toDouble() });
    }
    QList<QVector<QString>> rowList;
    for (auto val : ids)
    {
        QJsonObject data = m_infoModel->rowData(val.toString());
        if(data.isEmpty()) continue;
        QVector<QString> row(mapCol.size());
        for (auto itr = data.begin(); itr != data.end(); ++itr)
        {
            int col = mapCol.value(itr.key(), -1);
            if(col == -1) continue;
            row[col] = itr->toString();
        }
        rowList.push_back(row);
    }
    XlsxTool::saveData(destPath, u8"信息表", columnList, rowList, m_config->itemXlsxHeight());
    if (open)
    {
        QDesktopServices::openUrl(destPath);
    }
}


QString CAppCore::loadPhoto(const QString& source)
{
    QDir dir(qApp->applicationDirPath());
    dir.mkdir("photos");
    QImageReader reader(QUrl(source).toLocalFile());
    QImage image = reader.read();
    QString photoUrl = qApp->applicationDirPath() + "/photos/" + QUuid::createUuid().toString() + ".png";
    image.save(photoUrl);

    return photoUrl;
}

QString CAppCore::savePhoto(const QString& id, const QString& url)
{
    QString destPhoto = qApp->applicationDirPath() + "/photos/" + id + ".png";
    if (url == destPhoto)
    {
        return "/photos/" + id + ".png";
    }
    QFile::remove(destPhoto);
    if (QFile::copy(url, qApp->applicationDirPath() + "/photos/" + id + ".png"))
    {
        return "/photos/" + id + ".png";
    }
    QFile::remove(url);
    qDebug() << destPhoto << " saved fialed!";
    return "";
}

void CAppCore::printExcel(const QJsonArray& ids)
{
    QPrinter printer;
    QString printerName = printer.printerName();
    if (printerName.isEmpty())
    {
        return;
    }
    QPrintDialog dlg(&printer);
    if (dlg.exec() == QDialog::Accepted)
    {
        QDir dir(qApp->applicationDirPath());
        dir.mkdir("cache");
        QString cacheFile = qApp->applicationDirPath() + "/cache/xlsxprint.xlsx";
        QFile::remove(cacheFile);
        exportExcel(QString("file:///") + cacheFile, ids, false);
        QProcess::startDetached(qApp->applicationDirPath() + "/PrintExcel.exe", QStringList{ cacheFile, u8"信息表"});
    }
}

QString CAppCore::getAppDir() const
{
    return qApp->applicationDirPath();
}

void CAppCore::exportTemplate(const QString& savePath)
{
    exportExcel(savePath, {});
}

QString CAppCore::saveImage(const QString& id, QObject* imageObj)
{
    qDebug() << imageObj;
    QGraphicsObject* item = qobject_cast<QGraphicsObject*>(imageObj);

    if (!item)
    {
        qDebug() << "Item is NULL";
        return "";
    }

    QImage img(item->boundingRect().size().toSize(), QImage::Format_ARGB32);
    img.fill(QColor(255, 255, 255, 0).rgba());
    QPainter painter(&img);
    QStyleOptionGraphicsItem styleOption;
    item->paint(&painter, &styleOption);
    QDir dir(qApp->applicationDirPath());
    dir.mkdir("photos");
    QFile::remove(qApp->applicationDirPath() + "/photos/" + id + ".png");
    img.save(qApp->applicationDirPath() + "/photos/" + id + ".png");
    return "photos/" + id + ".png";
}

void CAppCore::onRefreshData(const QList<CTableRow>& rowList)
{
    QMap<QString, QString> tableData;
    for (auto& row : rowList)
    {
        QJsonDocument doc(QJsonObject::fromVariantMap(row.getData()));
        tableData[row.id()] = QString(doc.toJson());
    }
    QString error;
    CSqliteWork sqliteWork(qApp->applicationDirPath() + g_dbpath, error);
    if (error.isEmpty())
    {
        sqliteWork.setActivedTable(g_tablename);
        sqliteWork.insert(tableData);
    }
    updateStats();
}

void CAppCore::onDeleteData(const QList<CTableRow>& rowList)
{
    QStringList ids;
    for (auto& row : rowList)
    {
        ids.push_back(row.id());
    }
    QString error;
    CSqliteWork sqliteWork(qApp->applicationDirPath() + g_dbpath, error);
    if (error.isEmpty())
    {
        sqliteWork.setActivedTable(g_tablename);
        sqliteWork.del(ids);
    }
    updateStats();
}

void CAppCore::initDetailModel()
{
    QJsonArray itemColumns = m_config->itemColumns();
    QList<CTableModel::TableColumn> columnList;
    for (auto val : itemColumns)
    {
        QJsonObject obj = val.toObject();
        columnList.push_back({ obj["role"].toString(), obj["name"].toString(), CTableModel::Text, obj["pixelWidth"].toInt(), !obj["fixed"].toBool(), obj["sortable"].toBool()});
    }
    m_infoModel->init(columnList, true, true, true);
}

void CAppCore::initStatsModel()
{
    int colWidth = m_config->statsColWidth();
    QJsonArray statsColumns = m_config->statsColumns();
    QList<CTableModel::TableColumn> columnList;
    columnList.push_back({"region", u8"地区名称", CTableModel::Text, 120, false });
    for (auto val : statsColumns)
    {
        QString name = val.toObject().value("name").toString();
        columnList.push_back({ name, name, CTableModel::Text, colWidth, true, true });
    }
    columnList.push_back({ "total", u8"统计", CTableModel::Text, colWidth, true, true });
    m_statsModel->init(columnList, false, false, false);
}

void CAppCore::updateStats()
{
    QJsonArray statsColumns = m_config->statsColumns();
    QJsonArray regions = m_config->regions();
    QList<CTableRow> statsList;
    QList<CTableRow> infoList = m_infoModel->rowList();
    for (auto region : regions)
    {
        CTableRow tableRow;
        tableRow.setId(region.toString());
        int sum = 0;
        for (auto column : statsColumns)
        {
            QJsonObject obj = column.toObject();
            int count = 0;
            QString name = obj.value("name").toString();
            QString role = obj.value("role").toString();
            for (auto& info : infoList)
            {
                if (info.value(role).toString().contains(name) &&
                        info.value("region").toString().contains(region.toString()))
                {
                    ++count;
                }
            }
            sum += count;
            tableRow.setValue(name, count);
        }
        tableRow.setValue(u8"total", sum);
        statsList.push_back(tableRow);
    }
    m_statsModel->refreshRowList(statsList);
}
