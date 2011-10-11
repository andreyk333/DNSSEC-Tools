#include "Legend.h"

#include <QtGui/QVBoxLayout>
#include <QtGui/QLabel>
#include <QtGui/QDialogButtonBox>
#include <QTableWidget>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtGui/QIcon>
#include <QtGui/QTableWidgetItem>
#include <QtGui/QHeaderView>

#include <qdebug.h>

#include "DNSData.h"
#include "node.h"

Legend::Legend(QWidget *parent) :
    QDialog(parent)
{
    QLabel *label;
    QVBoxLayout *layout = new QVBoxLayout();
    setLayout(layout);

    DNSData d;
    Node *n = new Node(0);

    layout->addWidget(label = new QLabel("<h2>Legend</h2>"));
    label->setAlignment(Qt::AlignHCenter);

    QList<DNSData::Status> statuses;
    statuses << DNSData::UNKNOWN <<  DNSData::TRUSTED <<  DNSData::VALIDATED <<  DNSData::DNE
             << DNSData::FAILED <<  DNSData::IGNORE;

    // Add the legend widget
    QTableWidget *table = new QTableWidget(statuses.count(), 2, this);
    layout->addWidget(table);

    table->setHorizontalHeaderItem(0, new QTableWidgetItem("Node"));
    table->setHorizontalHeaderItem(1, new QTableWidgetItem("Description"));
    table->verticalHeader()->hide();

    QPointF rect = n->boundingRect().bottomRight() - n->boundingRect().topLeft();
    QSize size(rect.x() + 2, rect.y() + 2);
    QPointF br = n->boundingRect().bottomRight();

    int row = 0;
    foreach(DNSData::Status status, statuses) {
        QTableWidgetItem *item = new QTableWidgetItem(d.DNSSECStatusForEnum(status));
        item->setFlags(Qt::ItemIsEnabled);
        item->setBackgroundColor(n->getColorForStatus(status).lighter());
        table->setItem(row, 1, item);

        Node *node = new Node(0);
        QPixmap pm = QPixmap(size);
        QPainter painter(&pm);

        node->addSubData(DNSData("", status));

        painter.setBackground(Qt::white);
        painter.setBrush(Qt::white);
        painter.drawRect(-5, -5, size.width() + 5, size.height() + 5);
        painter.translate(br.x(), br.y());
        node->paint(&painter, 0, 0);

        QIcon icon = QIcon(pm);
        item = new QTableWidgetItem(icon, "");
        item->setFlags(Qt::ItemIsEnabled);
        table->setItem(row, 0, item);

        row++;
    }
    table->setRowCount(row);
    table->setColumnCount(2);
    table->resizeColumnsToContents();
    table->setSelectionMode(QAbstractItemView::NoSelection);

    //
    // closing button box
    //

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, this);
    layout->addWidget(buttonBox);

    setMinimumSize(600,400);

    connect(buttonBox, SIGNAL(rejected()), this, SLOT(accept()));
}
