#include "ui/SearchMissingFilesDialog.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QStorageInfo>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <atomic>

namespace openvegas {

class SearchWorker : public QObject {
    Q_OBJECT
public:
    SearchWorker(QString fileName, QStringList roots)
        : m_fileName(std::move(fileName))
        , m_roots(std::move(roots))
    {
    }

public slots:
    void run()
    {
        m_stop = false;
        for (const QString &root : m_roots) {
            if (m_stop.load()) {
                break;
            }
            walk(root);
        }
        emit finished();
    }

    void requestStop() { m_stop = true; }

signals:
    void hit(const QString &path, qint64 sizeBytes, const QString &modified);
    void status(const QString &text);
    void finished();

private:
    void walk(const QString &dirPath)
    {
        if (m_stop.load()) {
            return;
        }
        emit status(tr("Searching %1").arg(QDir::toNativeSeparators(dirPath)));
        QDir dir(dirPath);
        if (!dir.exists()) {
            return;
        }

        const QFileInfoList files = dir.entryInfoList(
            QStringList{m_fileName}, QDir::Files | QDir::Readable | QDir::Hidden, QDir::Name);
        for (const QFileInfo &fi : files) {
            if (m_stop.load()) {
                return;
            }
            if (fi.fileName().compare(m_fileName, Qt::CaseInsensitive) == 0) {
                emit hit(fi.absoluteFilePath(), fi.size(),
                         fi.lastModified().toString(QStringLiteral("dd.MM.yyyy HH:mm")));
            }
        }

        const QFileInfoList dirs =
            dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
        for (const QFileInfo &sub : dirs) {
            if (m_stop.load()) {
                return;
            }
            const QString name = sub.fileName().toLower();
            if (name == QLatin1String("$recycle.bin") || name == QLatin1String("system volume information")
                || name == QLatin1String("windows") || name == QLatin1String("winsxs")
                || name == QLatin1String("node_modules") || name == QLatin1String(".git")) {
                continue;
            }
            walk(sub.absoluteFilePath());
        }
    }

    QString m_fileName;
    QStringList m_roots;
    std::atomic_bool m_stop{false};
};

SearchMissingFilesDialog::SearchMissingFilesDialog(const QString &fileName, QWidget *parent)
    : QDialog(parent)
    , m_fileName(QFileInfo(fileName).fileName())
{
    setWindowTitle(tr("Search for Missing Files"));
    setModal(true);
    resize(720, 380);
    setObjectName(QStringLiteral("searchMissingFilesDialog"));
    setStyleSheet(QStringLiteral(
        "#searchMissingFilesDialog { background:#2a2a2a; color:#e0e0e0; }"
        "QLabel { color:#ddd; }"
        "QComboBox, QTreeWidget {"
        "  background:#1e1e1e; color:#eee; border:1px solid #555; }"
        "QTreeWidget::item:selected { background:#0078d7; }"
        "QHeaderView::section { background:#333; color:#ddd; padding:3px; border:none; }"
        "QPushButton { background:#3a3a3a; color:#eee; border:1px solid #555;"
        "  padding:4px 12px; min-width:90px; }"
        "QPushButton:hover { background:#4a4a4a; }"
        "QPushButton:disabled { color:#666; }"));

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto *left = new QVBoxLayout;
    left->setSpacing(8);

    auto *namedRow = new QHBoxLayout;
    namedRow->addWidget(new QLabel(tr("Named:"), this));
    auto *named = new QLabel(m_fileName, this);
    named->setTextInteractionFlags(Qt::TextSelectableByMouse);
    namedRow->addWidget(named, 1);
    left->addLayout(namedRow);

    auto *lookRow = new QHBoxLayout;
    lookRow->addWidget(new QLabel(tr("Look in:"), this));
    m_lookIn = new QComboBox(this);
    rebuildLookIn();
    lookRow->addWidget(m_lookIn, 1);
    left->addLayout(lookRow);

    m_results = new QTreeWidget(this);
    m_results->setColumnCount(4);
    m_results->setHeaderLabels({tr("Name"), tr("In Folder"), tr("Size"), tr("Modified")});
    m_results->setRootIsDecorated(false);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setAlternatingRowColors(true);
    m_results->header()->setStretchLastSection(false);
    m_results->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_results->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    left->addWidget(m_results, 1);

    m_status = new QLabel(tr("Ready."), this);
    left->addWidget(m_status);

    root->addLayout(left, 1);

    auto *btns = new QVBoxLayout;
    m_searchBtn = new QPushButton(tr("Search Now"), this);
    m_stopBtn = new QPushButton(tr("Stop Search"), this);
    m_okBtn = new QPushButton(tr("OK"), this);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    m_stopBtn->setEnabled(false);
    m_okBtn->setEnabled(false);
    btns->addWidget(m_searchBtn);
    btns->addWidget(m_stopBtn);
    btns->addWidget(m_okBtn);
    btns->addWidget(cancelBtn);
    btns->addStretch(1);
    root->addLayout(btns);

    connect(m_searchBtn, &QPushButton::clicked, this, &SearchMissingFilesDialog::startSearch);
    connect(m_stopBtn, &QPushButton::clicked, this, &SearchMissingFilesDialog::stopSearch);
    connect(m_okBtn, &QPushButton::clicked, this, &SearchMissingFilesDialog::acceptSelection);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_results, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *, int) { acceptSelection(); });
    connect(m_results, &QTreeWidget::itemSelectionChanged, this, [this]() {
        m_okBtn->setEnabled(!m_results->selectedItems().isEmpty());
    });
}

SearchMissingFilesDialog::~SearchMissingFilesDialog()
{
    stopSearch();
}

void SearchMissingFilesDialog::rebuildLookIn()
{
    m_lookIn->clear();
    QStringList drives;
    for (const QStorageInfo &s : QStorageInfo::mountedVolumes()) {
        if (!s.isValid() || !s.isReady()) {
            continue;
        }
        QString root = QDir::toNativeSeparators(s.rootPath());
        if (root.endsWith(QLatin1Char('\\')) || root.endsWith(QLatin1Char('/'))) {
            root.chop(1);
        }
        if (!root.isEmpty()) {
            drives << root;
        }
    }
    drives.removeDuplicates();
    if (drives.isEmpty()) {
        drives << QStringLiteral("C:");
    }
    m_lookIn->addItem(tr("Local Hard Drives (%1)").arg(drives.join(QLatin1Char(','))),
                      drives.join(QLatin1Char('|')));
    for (const QString &d : drives) {
        m_lookIn->addItem(d, d);
    }
}

void SearchMissingFilesDialog::startSearch()
{
    stopSearch();
    m_results->clear();
    m_okBtn->setEnabled(false);
    m_selectedPath.clear();

    QStringList roots;
    const QString data = m_lookIn->currentData().toString();
    if (data.contains(QLatin1Char('|'))) {
        for (QString d : data.split(QLatin1Char('|'))) {
            if (!d.endsWith(QLatin1Char('/')) && !d.endsWith(QLatin1Char('\\'))) {
                d += QLatin1Char('/');
            }
            roots << d;
        }
    } else {
        QString d = data;
        if (!d.endsWith(QLatin1Char('/')) && !d.endsWith(QLatin1Char('\\'))) {
            d += QLatin1Char('/');
        }
        roots << d;
    }

    m_workerThread = new QThread(this);
    m_worker = new SearchWorker(m_fileName, roots);
    m_worker->moveToThread(m_workerThread);
    connect(m_workerThread, &QThread::started, m_worker, &SearchWorker::run);
    connect(m_worker, &SearchWorker::hit, this, &SearchMissingFilesDialog::onHit);
    connect(m_worker, &SearchWorker::status, this, &SearchMissingFilesDialog::onStatus);
    connect(m_worker, &SearchWorker::finished, this, &SearchMissingFilesDialog::onFinished);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_searchBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_lookIn->setEnabled(false);
    m_workerThread->start();
}

void SearchMissingFilesDialog::stopSearch()
{
    if (m_worker) {
        m_worker->requestStop();
    }
    if (m_workerThread) {
        if (m_workerThread->isRunning()) {
            m_workerThread->quit();
            m_workerThread->wait(4000);
        }
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }
    m_worker = nullptr;
    m_searchBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    m_lookIn->setEnabled(true);
}

void SearchMissingFilesDialog::onHit(const QString &path, qint64 sizeBytes, const QString &modified)
{
    auto *item = new QTreeWidgetItem(m_results);
    const QFileInfo fi(path);
    item->setText(0, fi.fileName());
    item->setText(1, QDir::toNativeSeparators(fi.absolutePath()));
    item->setText(2, QLocale().formattedDataSize(sizeBytes));
    item->setText(3, modified);
    item->setData(0, Qt::UserRole, path);
    if (m_results->topLevelItemCount() == 1) {
        m_results->setCurrentItem(item);
        m_okBtn->setEnabled(true);
    }
}

void SearchMissingFilesDialog::onStatus(const QString &text)
{
    m_status->setText(text);
}

void SearchMissingFilesDialog::onFinished()
{
    m_searchBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    m_lookIn->setEnabled(true);
    if (m_results->topLevelItemCount() == 0) {
        m_status->setText(tr("No matches found."));
    } else {
        m_status->setText(tr("Found %1 match(es).").arg(m_results->topLevelItemCount()));
    }
}

void SearchMissingFilesDialog::acceptSelection()
{
    const auto selected = m_results->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    m_selectedPath = selected.first()->data(0, Qt::UserRole).toString();
    if (!m_selectedPath.isEmpty()) {
        accept();
    }
}

} // namespace openvegas

#include "SearchMissingFilesDialog.moc"
