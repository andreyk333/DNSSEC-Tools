/****************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of Nokia Corporation and its Subsidiary(-ies) nor
**     the names of its contributors may be used to endorse or promote
**     products derived from this software without specific prior written
**     permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
** $QT_END_LICENSE$
**
****************************************************************************/

#include "graphwidget.h"
#include "edge.h"
#include "node.h"
#include "DNSData.h"

#include <QtGui>
#include <qdebug.h>

#include <math.h>

#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <validator/resolver.h>
#include <validator/validator.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <QTimer>

static QStringList val_log_strings;
void val_collect_logs(struct val_log *logp, int level, const char *buf)
{
    Q_UNUSED(logp);
    Q_UNUSED(level);
    val_log_strings.push_back(buf);
}

GraphWidget::GraphWidget(QWidget *parent, QLineEdit *editor, const QString &fileName, QHBoxLayout *infoBox)
    : QGraphicsView(parent), timerId(0), m_editor(editor), m_libValDebugLog(fileName),
      m_nodeScale(2), m_localScale(false), m_lockNodes(false), m_shownsec3(false),
      m_logFile(0), m_logStream(0), m_timer(0),
      m_layoutType(springyLayout), m_childSize(30),
      m_validatedRegexp("Verified a RRSIG for ([^ ]+) \\(([^\\)]+)\\)"),
      m_lookingUpRegexp("looking for \\{([^ ]+) .* ([^\\(]+)\\([0-9]+\\)\\}"),
      m_bogusRegexp("Validation result for \\{([^,]+),.*BOGUS"),                                     // XXX: type not listed; fix in libval
      m_trustedRegexp("Validation result for \\{([^,]+),.*: (VAL_IGNORE_VALIDATION|VAL_PINSECURE)"), // XXX: type not listed; fix in libval
      m_pinsecureRegexp("Setting proof status for ([^ ]+) to: VAL_NONEXISTENT_TYPE_NOCHAIN"),
      m_dneRegexp("Validation result for \\{([^,]+),.*VAL_NONEXISTENT_(NAME|TYPE):"),
      m_maybeDneRegexp("Validation result for \\{([^,]+),.*VAL_NONEXISTENT_NAME_NOCHAIN:"),
      m_infoBox(infoBox)
{
    myScene = new QGraphicsScene(this);
    myScene->setItemIndexMethod(QGraphicsScene::NoIndex);
    myScene->setSceneRect(-300, -300, 600, 600);
    setScene(myScene);
    setCacheMode(CacheBackground);
    setViewportUpdateMode(BoundingRectViewportUpdate);
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setWindowTitle(tr("DNSSEC Nodes"));
    scaleWindow();

    createStartingNode();

    m_infoBox->addWidget(m_infoLabel = new QLabel(""));

    parseLogFile();

    setLayoutType(circleLayout);

    connect(m_editor, SIGNAL(returnPressed()), this, SLOT(doLookupFromLineEdit()));

    val_log_add_cb(NULL, 99, &val_collect_logs);
}

void GraphWidget::resizeEvent(QResizeEvent *event) {
    Q_UNUSED(event);
    scaleWindow();
}

void GraphWidget::scaleWindow() {

    if (m_localScale)
        return;

    // get rid of the current scale
    qreal oldScale = 1.0 / transform().mapRect(QRectF(0, 0, 1, 1)).height();
    scale(oldScale, oldScale);

    // calculate the new scale
    QSize windowSize = size();
    qDebug() << "window: " << windowSize;
    qreal newscale = qMin(windowSize.width() / myScene->sceneRect().width(), windowSize.height() / myScene->sceneRect().width());

    // apply it
    scale(.95 * newscale, .95 * newscale);
    qDebug() << "scale: " << newscale;
}

void GraphWidget::doLookupFromLineEdit() {
    doLookup(m_editor->text());
    reLayout();
}

void GraphWidget::itemMoved()
{
    if (!m_lockNodes && !timerId)
        timerId = startTimer(1000 / 25);
}

void GraphWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Up:
        centerNode->moveBy(0, -20);
        break;
    case Qt::Key_Down:
        centerNode->moveBy(0, 20);
        break;
    case Qt::Key_Left:
        centerNode->moveBy(-20, 0);
        break;
    case Qt::Key_Right:
        centerNode->moveBy(20, 0);
        break;
    case Qt::Key_Plus:
        zoomIn();
        break;
    case Qt::Key_Minus:
        zoomOut();
        break;
    case Qt::Key_Space:
    case Qt::Key_Enter:
        shuffle();
        break;
    default:
        QGraphicsView::keyPressEvent(event);
    }
}

void GraphWidget::timerEvent(QTimerEvent *event)
{
    Q_UNUSED(event);

    if (m_lockNodes)
        return;

    QList<Node *> nodes;
    foreach (QGraphicsItem *item, scene()->items()) {
        if (Node *node = qgraphicsitem_cast<Node *>(item))
            nodes << node;
    }

    if (m_layoutType == springyLayout)
        foreach (Node *node, nodes)
            node->calculateForces();

    bool itemsMoved = false;
    foreach (Node *node, nodes) {
        if (node->advance()) {
            itemsMoved = true;
        }
    }

    if (!itemsMoved) {
        killTimer(timerId);
        timerId = 0;
    }
}

void GraphWidget::wheelEvent(QWheelEvent *event)
{
    scaleView(pow((double)2, -event->delta() / 240.0));
}

void GraphWidget::drawBackground(QPainter *painter, const QRectF &rect)
{
    Q_UNUSED(rect);

    // Shadow
    QRectF sceneRect = this->sceneRect();
    QRectF rightShadow(sceneRect.right(), sceneRect.top() + 5, 5, sceneRect.height());
    QRectF bottomShadow(sceneRect.left() + 5, sceneRect.bottom(), sceneRect.width(), 5);
    if (rightShadow.intersects(rect) || rightShadow.contains(rect))
	painter->fillRect(rightShadow, Qt::darkGray);
    if (bottomShadow.intersects(rect) || bottomShadow.contains(rect))
	painter->fillRect(bottomShadow, Qt::darkGray);

    // Fill
    QLinearGradient gradient(sceneRect.topLeft(), sceneRect.bottomRight());
    gradient.setColorAt(0, Qt::white);
    gradient.setColorAt(1, Qt::lightGray);
    painter->fillRect(rect.intersect(sceneRect), gradient);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(sceneRect);
}

void GraphWidget::scaleView(qreal scaleFactor)
{
    qreal factor = transform().scale(scaleFactor, scaleFactor).mapRect(QRectF(0, 0, 1, 1)).width();
    if (factor < 0.07 || factor > 100)
        return;

    scale(scaleFactor, scaleFactor);
}

void GraphWidget::shuffle()
{
    foreach (QGraphicsItem *item, scene()->items()) {
        if (qgraphicsitem_cast<Node *>(item))
            item->setPos(-150 + qrand() % 300, -150 + qrand() % 300);
    }
}

void GraphWidget::zoomIn()
{
    m_localScale = true;
    scaleView(qreal(1.2));
}

void GraphWidget::zoomOut()
{
    m_localScale = true;
    scaleView(1 / qreal(1.2));
}

Node *GraphWidget::node(const QString &nodeName) {
    if (! m_nodes.contains(nodeName))
        m_nodes[nodeName] = new Node(this, nodeName);
    return m_nodes[nodeName];
}

Node *GraphWidget::addNodes(const QString &nodeName) {
    int count = 1;
    Node *returnNode = 0, *tmpNode = 0;

    QStringList nodeNameList = nodeName.split(".");
    QString completeString = QString("");

    QStringList::iterator node = nodeNameList.end();
    QStringList::iterator firstItem = nodeNameList.begin();
    do {
        node--;
        //qDebug() << "  doing node (" << nodeName << "): " << *node << "/" << completeString << " at " << count;
        if (! m_nodes.contains(*node + "." + completeString)) {
            qDebug() << "    " << (*node + "." + completeString) << " DNE!";
            returnNode = addNode(*node, completeString, count);
        } else {
            returnNode = m_nodes[*node + "." + completeString];
        }
        completeString = *node + "." + completeString;
        count++;
    } while(node != firstItem);
    return returnNode;
}

Node *GraphWidget::addNode(const QString &nodeName, const QString &parentName, int depth) {
    Edge *edge;
    QString parentString("<root>");
    QString suffixString(".");

    if (parentName.length() != 0) {
        parentString = parentName;
        suffixString = "." + parentName;
    }

    Node *newNode = m_nodes[nodeName + suffixString] = new Node(this, nodeName, depth);
    Node *parent = node(parentString);
    newNode->setPos(parent->pos() + QPointF(50 - qrand() % 101, 50 - qrand() % 101));
    myScene->addItem(newNode);
    myScene->addItem(edge = new Edge(newNode, parent));
    m_edges[QPair<QString, QString>(parentName, nodeName)] = edge;
    parent->addChild(newNode);
    newNode->addParent(parent);
    return newNode;
}

void GraphWidget::reLayout() {
    if (m_lockNodes)
        return;
    switch(m_layoutType) {
    case treeLayout:
        layoutInTree();
        break;
    case circleLayout:
        layoutInCircles();
        break;
    default:
        break;
    }
}

void GraphWidget::switchToTree() {
    itemMoved();
    layoutInTree();
}


void GraphWidget::switchToCircles() {
    itemMoved();
    layoutInCircles();
}

void GraphWidget::layoutInTree() {
    m_layoutType = treeLayout;
    QRectF rect = myScene->sceneRect();
    int farRightX = layoutTreeNode(node("<root>"), rect.left() + m_childSize, rect.top() + m_childSize);

    if (farRightX > myScene->sceneRect().right()) {
        myScene->setSceneRect(rect.left(), rect.top(), farRightX - rect.left(), rect.height());
        scaleWindow();
    }
}

int GraphWidget::layoutTreeNode(Node *node, int minX, int minY) {
    if (!scene() || scene()->mouseGrabberItem() == node) {
        return minX;
    }

    QList<Node *> childNodes = node->children();
    int runningMinX = minX;

    foreach(Node *child, childNodes) {
        runningMinX = layoutTreeNode(child, runningMinX, minY + m_childSize) + m_childSize;
    }
    if (childNodes.count() > 0)
        runningMinX -= m_childSize;

    node->setNewPos(QPointF(minX + (runningMinX - minX)/2, minY));
    return runningMinX;
}

void GraphWidget::layoutInCircles() {
    m_layoutType = circleLayout;
    QRectF rect;
    layoutCircleNode(node("<root>"), qreal(rect.left() + (rect.right() - rect.left())/2), qreal(rect.top() + (rect.top() - rect.bottom())/2), 0, 2*3.1415);

    // XXX: test growth size into borders
}

void GraphWidget::layoutCircleNode(Node *node, qreal startX, qreal startY, qreal startingDegrees, qreal maxDegrees) {
    QList<Node *> childNodes = node->children();
    const int childSize = 30;
    int numChildren = childNodes.count();

    qreal degreesPerChild;
    if (numChildren > 0) {
        degreesPerChild = maxDegrees / (numChildren);

        startingDegrees = startingDegrees - maxDegrees/2 + maxDegrees/(numChildren+1);
        foreach(Node *child, childNodes) {
            qreal childX = startX + childSize*2*cos(startingDegrees);
            qreal childY = startY + childSize*2*sin(startingDegrees);
            layoutCircleNode(child, childX, childY, startingDegrees, degreesPerChild);
            startingDegrees += degreesPerChild;
        }
    }

    if (scene() && scene()->mouseGrabberItem() == node) {
        return;
    }

    node->setNewPos(QPointF(startX, startY));
}

void GraphWidget::doLookup(QString src) {
    doActualLookup(src);
}

void GraphWidget::addRootNode(QString newNode) {
    myScene->addItem(new Node(this, newNode));
}

void GraphWidget::doActualLookup(const QString &lookupString)
{
    val_status_t val_status;
    struct addrinfo *aitop = NULL;
    int ret;
    u_char buf[4096];

    busy();

    // perform the lookup
    ret = val_res_query(NULL, lookupString.toUtf8(), ns_c_in,
                        ns_t_a, buf, sizeof(buf), &val_status);

    // do something with the results
    if (ret <= 0) {
        // XXX: indicate an error somehow

        if (!val_istrusted(val_status)) {
            // untrusted error for host
        }
        if (!val_istrusted(val_status)) {
            // untrusted for ip address
        }

        //setSecurityStatus(val_status);
    } else {
        QColor color;
        addNodes(lookupString);
        if (val_isvalidated(val_status)) {
            color = Qt::green;
        } else if (val_istrusted(val_status)) {
            color = Qt::yellow;
        } else {
            color = Qt::red;
        }
        node(lookupString + ".")->setColor(color);
        //setSecurityStatus(val_status);
    }

    QString lastInterestingString;

    foreach(QString logMessage, val_log_strings) {
        parseLogMessage(logMessage);
    }
    val_log_strings.clear();

    freeaddrinfo(aitop);
    unbusy();
}

void GraphWidget::setShowNSEC3Records(bool newVal) {
    m_shownsec3 = newVal;
}

void GraphWidget::parseLogMessage(QString logMessage) {
    QColor color;
    QString nodeName;
    QString additionalInfo = "";
    QList<DNSData> dnsDataNodes;
    Node *thenode;

    // qDebug() << logMessage;

    if (m_lookingUpRegexp.indexIn(logMessage) > -1) {
        nodeName = m_lookingUpRegexp.cap(1);
        dnsDataNodes.push_back(DNSData(m_lookingUpRegexp.cap(2), DNSData::UNKNOWN));
        logMessage.replace(m_lookingUpRegexp, "<b>looking up \\1</b>  ");
    } else if (m_validatedRegexp.indexIn(logMessage) > -1) {
        if (!m_shownsec3 && m_validatedRegexp.cap(2) == "NSEC3")
            return;
        if (m_validatedRegexp.cap(2) == "NSEC")
            return; // never show 'good' for something missing
        nodeName = m_validatedRegexp.cap(1);
        logMessage.replace(m_validatedRegexp, "<b><font color=\"green\">Verified a \\2 record for \\1 </font></b>");
        additionalInfo = "The data for this node has been Validated";
        dnsDataNodes.push_back(DNSData(m_validatedRegexp.cap(2), DNSData::VALIDATED));
        color = Qt::green;
    } else if (m_bogusRegexp.indexIn(logMessage) > -1) {
        nodeName = m_bogusRegexp.cap(1);
        logMessage.replace(m_bogusRegexp, "<b><font color=\"red\">BOGUS Record found for \\1 </font></b>");
        additionalInfo = "DNSSEC Security for this Node Failed";
        color = Qt::red;
    } else if (m_trustedRegexp.indexIn(logMessage) > -1) {
        nodeName = m_trustedRegexp.cap(1);
        logMessage.replace(m_trustedRegexp, "<b><font color=\"brown\">Trusting result for \\1 </font></b>");
        additionalInfo = "Data is trusted, but not proven to be secure";
        color = Qt::yellow;
    } else if (m_pinsecureRegexp.indexIn(logMessage) > -1) {
        nodeName = m_pinsecureRegexp.cap(1);
        logMessage.replace(m_pinsecureRegexp, ":<b><font color=\"brown\"> \\1 is provably insecure </font></b>");
        additionalInfo = "This node has been proven to be <b>not</b> DNSEC protected";
        color = Qt::yellow;
    } else if (m_dneRegexp.indexIn(logMessage) > -1) {
        nodeName = m_dneRegexp.cap(1);
        logMessage.replace(m_dneRegexp, ":<b><font color=\"brown\"> \\1 provably does not exist </font></b>");
        additionalInfo = "This node has been proven to not exist in the DNS";
        color = Qt::blue;
    } else if (m_maybeDneRegexp.indexIn(logMessage) > -1) {
        nodeName = m_maybeDneRegexp.cap(1);
        additionalInfo = "This node supposedly doesn't exist, but its non-existence can't be proven.";
        logMessage.replace(m_maybeDneRegexp, ":<b><font color=\"brown\"> \\1 does not exist, but can't be proven' </font></b>");
        color = Qt::cyan;
    } else {
        return;
    }
    if (nodeName == ".")
        return;
    thenode = addNodes(nodeName);
    if (thenode && !dnsDataNodes.isEmpty()) {
        foreach(DNSData data, dnsDataNodes) {
            qDebug() << "here: " << nodeName << "=" << data.recordType();
            thenode->addSubData(data);
        }
    }
    if (color.isValid())
        node(nodeName + ".")->setColor(color);
    if (additionalInfo.length() > 0)
        node(nodeName+ ".")->setAdditionalInfo(additionalInfo);
    node(nodeName + ".")->addLogMessage(logMessage);
}

void GraphWidget::parseLogFile(const QString &fileToOpen) {
    QString fileName = fileToOpen;
    if (fileName.length() == 0)
        fileName = m_libValDebugLog;

    if (fileName.length() == 0)
        return;

    // qDebug() << "Trying to open: " << fileName;

    // start the timer to keep reading/trying the log file
    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, SIGNAL(timeout()), this, SLOT(parseTillEnd()));
        m_timer->start(1000);
    }

    m_logFile = new QFile(fileName);
    if (!m_logFile->exists() || !m_logFile->open(QIODevice::ReadOnly | QIODevice::Text)) {
        delete m_logFile;
        m_logFile = 0;
        return;
    }

    m_logStream = new QTextStream(m_logFile);

    // qDebug() << "Opened: " << fileName;

    parseTillEnd();
}

void GraphWidget::parseTillEnd() {
    if (!m_logStream) {
        parseLogFile();
        if (!m_logStream)
            return;
    }

    while (!m_logStream->atEnd()) {
        QString line = m_logStream->readLine();
        parseLogMessage(line);
    }
    reLayout();
}

void GraphWidget::unbusy() {
    setCursor(Qt::ArrowCursor);
}

void GraphWidget::busy() {
    setCursor(Qt::WaitCursor);
}

void GraphWidget::setLockedNodes(bool newVal) {
    m_lockNodes = newVal;
    if (!m_lockNodes)
        itemMoved();
}

void GraphWidget::toggleLockedNodes() {
    m_lockNodes = !m_lockNodes;
    if (!m_lockNodes)
        itemMoved();
}

void GraphWidget::openLogFile() {
    QSettings settings("DNSSEC-Tools", "dnssec-nodes");
    QString defaultFile = settings.value("logFile", QString("/var/log/libval.log")).toString();

    QFileDialog dialog;
    dialog.selectFile(defaultFile);
    dialog.setFileMode(QFileDialog::AnyFile);
    if (!dialog.exec())
        return;

    m_libValDebugLog = dialog.selectedFiles()[0];
    settings.setValue("logFile", m_libValDebugLog);
    if (m_libValDebugLog.length() > 0) {
        parseLogFile(m_libValDebugLog);
    }
}

void GraphWidget::reReadLogFile() {
    if (m_logFile) {
        m_logFile->close();
        delete m_logFile;
        m_logFile = 0;
    }
    if (m_logStream) {
        delete m_logStream;
        m_logStream = 0;
    }
    parseLogFile();
}

void GraphWidget::setLayoutType(LayoutType layoutType)
{
    m_layoutType = layoutType;
    reLayout();
}

void GraphWidget::setInfo(const QString &text)
{
    m_infoLabel->setText(text);
}

void GraphWidget::setInfo(Node *node) {
    QString buildString;
    Node *nodeIterator = node;
    while (nodeIterator && nodeIterator->nodeName() != "<root>") {
        buildString = buildString + "." + nodeIterator->nodeName();
        nodeIterator = nodeIterator->parent();
    }
    if (node->additionalInfo().length() > 0) {
        buildString += " (" + node->additionalInfo() + ")";
    }
    buildString += + "[" + node->getSubData() + "]";
    setInfo(buildString);
}

void GraphWidget::clear()
{
    foreach(Node *aNode, m_nodes) {
        delete aNode;
    }

    foreach(Edge *anEdge, m_edges) {
        delete anEdge;
    }

    m_nodes.clear();
    m_edges.clear();

    // add back in the starting node
    createStartingNode();
}

void GraphWidget::createStartingNode()
{
    m_nodes["<root>"] = new Node(this, "<root>", 0);
    scene()->addItem(m_nodes["<root>"]);
    m_nodes["<root>"]->setColor(QColor(Qt::green));
}
