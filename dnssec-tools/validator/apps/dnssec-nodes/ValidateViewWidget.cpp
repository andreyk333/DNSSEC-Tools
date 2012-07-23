#include "ValidateViewWidget.h"

#include <QtGui/QGraphicsRectItem>
#include <QtGui/QGraphicsSimpleTextItem>

#include <validator/validator-config.h>
#include <validator/validator.h>

#include <qdebug.h>

#define RES_GET16(s, cp) do { \
        register const u_char *t_cp = (const u_char *)(cp); \
        (s) = ((u_int16_t)t_cp[0] << 8) \
            | ((u_int16_t)t_cp[1]) \
            ; \
        (cp) += NS_INT16SZ; \
} while (0)

// from ns_print.c
extern "C" {
u_int16_t id_calc(const u_char * key, const int keysize);
}

ValidateViewWidget::ValidateViewWidget(QString nodeName, QString recordType, QWidget *parent) :
    QGraphicsView(parent), m_nodeName(nodeName), m_recordType(recordType), m_typeToName()
{
    myScene = new QGraphicsScene(this);
    myScene->setItemIndexMethod(QGraphicsScene::NoIndex);
    myScene->setSceneRect(0, 0, 600, 600);
    setScene(myScene);
    setCacheMode(CacheBackground);
    setViewportUpdateMode(BoundingRectViewportUpdate);
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setWindowTitle(tr("Validation of %1 for %2").arg(nodeName).arg(recordType));
    //scaleWindow();

    m_typeToName[1] = "A";
    m_typeToName[2] = "NS";
    m_typeToName[5] = "CNAME";
    m_typeToName[6] = "SOA";
    m_typeToName[12] = "PTR";
    m_typeToName[15] = "MX";
    m_typeToName[16] = "TXT";
    m_typeToName[28] = "AAAA";
    m_typeToName[33] = "SRV";
    m_typeToName[255] = "ANY";

    m_typeToName[43] = "DS";
    m_typeToName[46] = "RRSIG";
    m_typeToName[47] = "NSEC";
    m_typeToName[48] = "DNSKEY";
    m_typeToName[50] = "NSEC3";
    m_typeToName[32769] = "DLV";

    m_typeToName[3] = "MD";
    m_typeToName[4] = "MF";
    m_typeToName[7] = "MB";
    m_typeToName[8] = "MG";
    m_typeToName[9] = "MR";
    m_typeToName[10] = "NULL";
    m_typeToName[11] = "WKS";
    m_typeToName[13] = "HINFO";
    m_typeToName[14] = "MINFO";
    m_typeToName[17] = "RP";
    m_typeToName[18] = "AFSDB";
    m_typeToName[19] = "X25";
    m_typeToName[20] = "ISDN";
    m_typeToName[21] = "RT";
    m_typeToName[22] = "NSAP";
    m_typeToName[23] = "NSAP_PTR";
    m_typeToName[24] = "SIG";
    m_typeToName[25] = "KEY";
    m_typeToName[26] = "PX";
    m_typeToName[27] = "GPOS";
    m_typeToName[29] = "LOC";
    m_typeToName[30] = "NXT";
    m_typeToName[31] = "EID";
    m_typeToName[32] = "NIMLOC";
    m_typeToName[34] = "ATMA";
    m_typeToName[35] = "NAPTR";
    m_typeToName[36] = "KX";
    m_typeToName[37] = "CERT";
    m_typeToName[38] = "A6";
    m_typeToName[39] = "DNAME";
    m_typeToName[40] = "SINK";
    m_typeToName[41] = "OPT";
    m_typeToName[250] = "TSIG";
    m_typeToName[251] = "IXFR";
    m_typeToName[252] = "AXFR";
    m_typeToName[253] = "MAILB";
    m_typeToName[254] = "MAILA";

    scaleView(.5);
    validateSomething(m_nodeName, m_recordType);
}

void ValidateViewWidget::scaleView(qreal scaleFactor)
{
    qreal factor = transform().scale(scaleFactor, scaleFactor).mapRect(QRectF(0, 0, 1, 1)).width();
    if (factor < 0.07 || factor > 100)
        return;

    scale(scaleFactor, scaleFactor);
}

void ValidateViewWidget::drawArrow(int fromX, int fromY, int toX, int toY) {
    const int arrowHalfWidth = 10;

    QGraphicsLineItem *line = new QGraphicsLineItem(fromX, fromY, toX, toY);
    myScene->addItem(line);

    QPolygon polygon;
    polygon << QPoint(toX, toY)
            << QPoint(toX - arrowHalfWidth, toY - arrowHalfWidth)
            << QPoint(toX + arrowHalfWidth, toY - arrowHalfWidth);
    QGraphicsPolygonItem *polyItem = new QGraphicsPolygonItem(polygon);
    polyItem->setBrush(QBrush(Qt::black));
    polyItem->setFillRule(Qt::OddEvenFill);
    myScene->addItem(polyItem);
}

void ValidateViewWidget::validateSomething(QString name, QString type) {
    val_result_chain                *results = 0;
    struct val_authentication_chain *vrcptr = 0;

    const int spacing = 50;
    const int boxWidth = 400;
    const int boxHeight = 120;
    const int boxTopMargin = 10;
    const int boxLeftMargin = 10;
    const int boxHorizontalSpacing = 30;
    const int verticalBoxDistance = spacing + boxHeight;
    const int boxHorizMiddle = boxLeftMargin + boxWidth/2;
    const int arrowHalfWidth = 10;


    int ret;
    ret = val_resolve_and_check(NULL, name.toAscii().data(), 1, ns_t_a,
                                VAL_QUERY_RECURSE | VAL_QUERY_AC_DETAIL |
                                VAL_QUERY_SKIP_CACHE,
                                &results);
    qDebug() << "got here: result = " << ret;
    if (ret != 0 || !results) {
        qWarning() << "failed to get results..."; // XXX: display SOMETHING!
        return;
    }

    int spot = 0;
    int maxWidth = 0;
    QGraphicsRectItem        *rect = 0;
    QGraphicsSimpleTextItem  *text;
    struct val_rr_rec *rrrec;
    const u_char * rdata;

    QMap<int, QPair<int, int> > dnskeyIdToLocation, dsIdToLocation;

    // for each authentication record, display a horiz row of data
    for(vrcptr = results->val_rc_answer; vrcptr; vrcptr = vrcptr->val_ac_trust) {
        int horizontalSpot = boxLeftMargin;

        // for each rrset in an auth record, display a box
        for(rrrec = vrcptr->val_ac_rrset->val_rrset_data; rrrec; rrrec = rrrec->rr_next) {
            rdata = rrrec->rr_rdata;

            qDebug() << "chain: " << vrcptr->val_ac_rrset->val_rrset_name << " -> " << vrcptr->val_ac_rrset->val_rrset_type;

            rect = new QGraphicsRectItem(horizontalSpot, spot+boxTopMargin, boxWidth, boxHeight);
            rect->setPen(QPen(Qt::black));
            myScene->addItem(rect);

            if (m_typeToName.contains(vrcptr->val_ac_rrset->val_rrset_type))
                text = new QGraphicsSimpleTextItem(m_typeToName[vrcptr->val_ac_rrset->val_rrset_type]);
            else
                text = new QGraphicsSimpleTextItem("(type unknown)");
            text->setPen(QPen(Qt::black));
            text->setPos(boxLeftMargin + horizontalSpot, spot + boxTopMargin*2);
            text->setScale(2.0);
            myScene->addItem(text);

            QString rrsetName = vrcptr->val_ac_rrset->val_rrset_name;
            text = new QGraphicsSimpleTextItem(rrsetName == "." ? "<root>" : rrsetName);
            text->setPen(QPen(Qt::black));
            text->setPos(boxLeftMargin + horizontalSpot, spot + boxHeight/2);
            text->setScale(2.0);
            myScene->addItem(text);

            int keyId;
            QString nextLineText;
            u_int           keyflags, protocol, algorithm, key_id, digest_type;

            switch (vrcptr->val_ac_rrset->val_rrset_type) {
            case ns_t_dnskey:
                if (rrrec->rr_rdata_length < 0U + NS_INT16SZ + NS_INT8SZ + NS_INT8SZ)
                    break;

                /* grab the KeyID */
                keyId = id_calc(rrrec->rr_rdata, rrrec->rr_rdata_length);

                /* get the flags */
                RES_GET16(keyflags, rrrec->rr_rdata);
                protocol = *rdata++;
                algorithm = *rdata++;

                nextLineText = QString(tr("%1, id: %2, proto: %3, alg: %4"))
                        .arg((keyflags & 0x1) ? "KSK" : "ZSK")
                        .arg(keyId)
                        .arg(protocol)
                        .arg(algorithm);
                dnskeyIdToLocation[keyId] = QPair<int,int>(horizontalSpot, spot + boxTopMargin);
                break;

            case ns_t_ds:
                RES_GET16(keyId, rdata);
                algorithm = *rdata++ & 0xF;
                digest_type = *rdata++ & 0xF;

                nextLineText = QString(tr("id: %1, alg: %2, digest: %3"))
                        .arg(keyId)
                        .arg(algorithm)
                        .arg(digest_type);
                dsIdToLocation[keyId] = QPair<int,int>(horizontalSpot, spot + boxTopMargin);

                break;
            }


            if (nextLineText.length() > 0) {
                text = new QGraphicsSimpleTextItem(nextLineText);
                text->setPen(QPen(Qt::black));
                text->setPos(boxLeftMargin + horizontalSpot, spot + boxHeight - boxTopMargin*3);
                text->setScale(2.0);
                myScene->addItem(text);
            }

            if (spot != 0) {
                // add an arrow
                int polyVertStartSpot = spot + boxHeight + spacing + boxTopMargin;

                drawArrow(boxLeftMargin + boxWidth/2,
                          spot + boxHeight + boxTopMargin,
                          boxLeftMargin + boxWidth/2, polyVertStartSpot);

            }

            horizontalSpot += boxWidth + boxHorizontalSpacing;
            maxWidth = qMax(maxWidth, horizontalSpot);
        }

        spot -= verticalBoxDistance;
    }


    myScene->setSceneRect(0, spot + boxHeight, maxWidth, -spot + boxHeight);
    if (rect)
        ensureVisible(rect);
}

#ifdef maybe
QPair<QGraphicsRectItem *, QGraphicsSimpleTextItem *>
ValidateViewWidget::createRecordBox(struct val_authentication_chain *auth_chain, int spot) {
    QGraphicsRectItem        *rect = new QGraphicsRectItem(10,spot,100,100);
    QGraphicsSimpleTextItem  *text = new QGraphicsSimpleTextItem(auth_chain->val_rrset_name);
    return
}
#endif