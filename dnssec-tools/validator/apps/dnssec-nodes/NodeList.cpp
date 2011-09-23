#include "NodeList.h"

#include <qdebug.h>

NodeList::NodeList(GraphWidget *parent) :
    QObject(parent), m_graphWidget(parent), m_centerNode(0), m_nodes(), m_edges(),
    m_timer(this), m_maxNodes(0), m_accessCounter(0), m_accessDropOlderThan(0)
{
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(limit()));
    m_timer.start(5000); /* clear things out every 5 seconds or so */
}

Node *NodeList::node(const QString &nodeName) {
    if (! m_nodes.contains(nodeName)) {
        Node *newNode = new Node(m_graphWidget, nodeName);
        m_nodes[nodeName] = newNode;
        newNode->setAccessCount(m_accessCounter++);
    }

    return m_nodes[nodeName];
}

Node *NodeList::addNodes(const QString &nodeName) {
    int count = 1;
    Node *returnNode = 0;

    QStringList nodeNameList = nodeName.split(".");
    QString completeString = QString("");

    QStringList::iterator node = nodeNameList.end();
    QStringList::iterator firstItem = nodeNameList.begin();

    while (node != firstItem) {
        node--;
        //qDebug() << "  doing node (" << nodeName << "): " << *node << "/" << completeString << " at " << count;
        if (! m_nodes.contains(*node + "." + completeString)) {
            //qDebug() << "    adding: " << (*node + "." + completeString) << " DNE!";
            returnNode = addNode(*node, completeString, count);
        } else {
            returnNode = m_nodes[*node + "." + completeString];
        }
        completeString = *node + "." + completeString;
        count++;
    }
    return returnNode;
}

Node *NodeList::addNode(const QString &nodeName, const QString &parentName, int depth) {
    Edge *edge;
    QString parentString("<root>");
    QString suffixString(".");
    QString fqdn;

    if (parentName.length() != 0) {
        parentString = parentName;
        suffixString = "." + parentName;
    }

    fqdn = nodeName + suffixString;

    // create a new node, and find a parent for it
    Node *newNode = m_nodes[fqdn] = new Node(m_graphWidget, nodeName, fqdn, depth);
    Node *parent = node(parentString);

    // define the graphics charactistics
    newNode->setPos(parent->pos() + QPointF(50 - qrand() % 101, 50 - qrand() % 101));
    m_graphWidget->addItem(newNode);

    // define the arrow from parent to child
    m_graphWidget->addItem(edge = new Edge(parent, newNode));
    m_edges[QPair<QString, QString>(fqdn, parentName)] = edge;

    // define the relationship
    parent->addChild(newNode);
    newNode->addParent(parent);

    // define the access counts
    newNode->setAccessCount(m_accessCounter++);

    return newNode;
}

int NodeList::nodeCount() {
    return m_nodes.count();
}

int NodeList::edgeCount() {
    return m_edges.count();
}


void NodeList::clear()
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
    m_graphWidget->createStartingNode();
}

void NodeList::limit()
{
    if (m_maxNodes <= 0)
        return;
    if (m_accessCounter <= m_maxNodes)
        return;

    m_accessDropOlderThan = m_accessCounter - m_maxNodes;

    // walk through our list of nodes and drop everything "older"
    limitChildren(m_centerNode);
}

void NodeList::limitChildren(Node *node) {
    foreach (Node *child, node->children()) {
        limitChildren(child);
    }

    if (node->children().count() == 0) {
        if (node->accessCount() < m_accessDropOlderThan) {
            // drop this node because it has no children left and is safe to remove
            qDebug() << "removing: " << node->fqdn() << " #" << node->accessCount() << " / " << m_accessDropOlderThan;

            // remove it from various lists
            m_graphWidget->removeItem(node);
            m_nodes.remove(node->fqdn());

            // remove the edge too
            QPair<QString, QString> edgeNames(node->fqdn(), node->parent()->fqdn());
            Edge *edge = m_edges[edgeNames];
            m_graphWidget->removeItem(edge);
            m_edges.remove(edgeNames);
            // XXX: delete the edge (later)

            // delete the relationship
            node->parent()->removeChild(node);

            // XXX delete node; // (because of the parent loop, we need to delete this later)
        }
    }
}

void  NodeList::setCenterNode(Node *newCenter) {
    qDebug() << "setting center";
    if (m_centerNode) {
        if (m_nodes.contains(ROOT_NODE_NAME))
            m_nodes.remove(ROOT_NODE_NAME);
        delete m_centerNode;
    }

    m_centerNode = newCenter;
    m_nodes[ROOT_NODE_NAME] = newCenter;
    qDebug() << "here after: " << m_nodes.count();
}
