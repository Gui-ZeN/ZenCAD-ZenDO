// src/zendo/StartPage3D.cpp
#include "StartPage3D.hpp"
#include "app/ProjectIo.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr double kPi = 3.14159265358979;

// Card clicável (frame com sinal de clique) — hover vem do QSS (#projCard).
class CardFrame : public QWidget {
public:
    explicit CardFrame(QWidget* parent = nullptr) : QWidget(parent) {
        setCursor(Qt::PointingHandCursor);
    }
    std::function<void()> onClick;

protected:
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) &&
            onClick)
            onClick();
    }
};

// O rosto do Zendo pintado ao vivo: ensō prata (pincelada de espessura
// variável, abertura no nordeste) com o cubo isométrico em latão dentro.
void drawEnsoCube(QPainter& p, const QRectF& r) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const double S = std::min(r.width(), r.height());
    const double cx = r.center().x(), cy = r.center().y(), R = S * 0.40;
    const double a0 = 58.0 * kPi / 180.0, a1 = a0 + 325.0 * kPi / 180.0;
    p.setPen(Qt::NoPen);
    const int N = 240;
    for (int i = 0; i < N; ++i) {
        const double t = i / double(N - 1);
        const double a = a0 + (a1 - a0) * t;
        double w = (S * 0.024) *
                   (0.35 + 0.9 * std::pow(std::sin(kPi * std::min(1.0, t * 1.12)),
                                          0.8));
        if (t > 0.94) w *= std::max(0.12, (1.0 - t) / 0.06);  // cauda seca
        const int sh = 190 + int(45 * std::sin(kPi * t));
        p.setBrush(QColor(sh, sh, sh + 6));
        p.drawEllipse(QPointF(cx + R * std::cos(a), cy + R * std::sin(a)), w, w);
    }
    const double e = S * 0.205, h = e * 0.62, zc = cy + S * 0.012;
    const QPointF top(cx, zc - h * 2.05);
    const QPointF left(cx - e, zc - h * 1.03), right(cx + e, zc - h * 1.03);
    const QPointF mid(cx, zc - h * 0.05);
    const QPointF bl(cx - e, zc + h * 1.30), br(cx + e, zc + h * 1.30);
    const QPointF bot(cx, zc + h * 2.30);
    p.setBrush(QColor(222, 190, 128));
    p.drawPolygon(QPolygonF{top, right, mid, left});
    p.setBrush(QColor(154, 122, 70));
    p.drawPolygon(QPolygonF{left, mid, bot, bl});
    p.setBrush(QColor(194, 160, 99));
    p.drawPolygon(QPolygonF{mid, right, br, bot});
    p.setPen(QPen(QColor(255, 236, 190), std::max(1.0, S * 0.008)));
    p.setBrush(Qt::NoBrush);
    for (const auto& ln : {QLineF(top, right), QLineF(right, mid),
                           QLineF(mid, left), QLineF(left, top),
                           QLineF(left, bl), QLineF(bl, bot), QLineF(bot, mid),
                           QLineF(bot, br), QLineF(br, right)})
        p.drawLine(ln);
    p.restore();
}

QPixmap zendoLogoPixmap(int px, qreal dpr) {
    QPixmap pm(int(px * dpr), int(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    drawEnsoCube(p, QRectF(0, 0, px, px));
    return pm;
}

// Miniatura do recente: .zendo tem a imagem embutida (chave "thumb",
// JPEG base64); .zencad usa a do próprio projeto (ProjectIo).
QImage recentThumbnail(const QString& path) {
    if (path.endsWith(QLatin1String(".zencad"), Qt::CaseInsensitive))
        return cad::projectThumbnail(path);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QByteArray b64 = root.value(QLatin1String("thumb"))
                               .toString().toLatin1();
    if (b64.isEmpty()) return {};
    return QImage::fromData(QByteArray::fromBase64(b64));
}

// Miniatura com cantos arredondados (ou placeholder com o cubo em latão).
QPixmap roundedThumb(const QImage& img, const QSize& size) {
    QPixmap out(size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), 5, 5);
    p.setClipPath(clip);
    if (!img.isNull()) {
        const QPixmap pm = QPixmap::fromImage(img).scaled(
            size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        p.drawPixmap((size.width() - pm.width()) / 2,
                     (size.height() - pm.height()) / 2, pm);
    } else {
        p.fillRect(out.rect(), QColor(18, 21, 25));
        p.setOpacity(0.45);
        const double s = size.height() * 0.72;
        drawEnsoCube(p, QRectF(size.width() / 2.0 - s / 2,
                               size.height() / 2.0 - s / 2, s, s));
    }
    return out;
}

} // namespace

ZendoStartPage::ZendoStartPage(QWidget* parent) : QWidget(parent) {
    setObjectName("startPage");

    // QSS local da página (soma ao tema global) — o mesmo traço do ZenCAD.
    setStyleSheet(R"(
        QFrame#startSide {
            background: rgba(16, 18, 22, 235);
            border-right: 1px solid #23262b;
        }
        QPushButton#startPrimary {
            background: #c2a063; color: #16181c; border: none; border-radius: 4px;
            font-size: 14px; font-weight: 600; padding: 12px 18px; letter-spacing: 0.5px;
        }
        QPushButton#startPrimary:hover  { background: #d4b579; }
        QPushButton#startPrimary:pressed{ background: #ab8c52; }
        QPushButton#startGhost {
            background: transparent; color: #d8d2c6; border: 1px solid #3a3e44;
            border-radius: 4px; font-size: 14px; padding: 11px 18px;
        }
        QPushButton#startGhost:hover { border-color: #c2a063; color: #c2a063; }
        QPushButton#startLink {
            background: transparent; color: #c2a063; border: none;
            font-size: 13px; padding: 4px 8px;
        }
        QPushButton#startLink:hover { color: #d4b579; }
        QWidget#projCard {
            background: #16191d; border: 1px solid #262a30; border-radius: 8px;
        }
        QWidget#projCard:hover { border: 1px solid #c2a063; background: #1a1e23; }
        QLabel#cardName { color: #e4ded2; font-size: 13px; font-weight: 600; }
        QLabel#cardMeta { color: #7c8188; font-size: 11px; }
        QLabel#sideTag  { color: #8a8f96; letter-spacing: 2px; font-size: 10px; }
        QLabel#secTitle { color: #8a8f96; letter-spacing: 3px; font-size: 12px; }
        QScrollArea#startScroll { background: transparent; border: none; }
    )");

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ===== Sidebar da marca ================================================
    auto* side = new QFrame;
    side->setObjectName("startSide");
    side->setFixedWidth(320);
    auto* sv = new QVBoxLayout(side);
    sv->setContentsMargins(32, 40, 32, 28);
    sv->setSpacing(10);

    auto* pic = new QLabel;
    pic->setPixmap(zendoLogoPixmap(168, devicePixelRatioF()));
    pic->setAlignment(Qt::AlignHCenter);
    sv->addWidget(pic);
    auto* word = new QLabel(
        "<span style=\"font-size:44px; letter-spacing:1px;\">"
        "<span style=\"color:#c2a063; font-weight:600;\">Zen</span>"
        "<span style=\"color:#c9ccd1; font-weight:600;\">do</span></span>");
    word->setAlignment(Qt::AlignHCenter);
    sv->addWidget(word);
    auto* tag = new QLabel("MODELE. ESTUDE. CONSTRUA.");
    tag->setObjectName("sideTag");
    tag->setAlignment(Qt::AlignHCenter);
    sv->addWidget(tag);
    sv->addSpacing(22);

    auto* btNew = new QPushButton("Novo estudo");
    btNew->setObjectName("startPrimary");
    btNew->setMinimumHeight(46);
    sv->addWidget(btNew);
    auto* btStudy = new QPushButton("Abrir estudo 3D…");
    btStudy->setObjectName("startGhost");
    btStudy->setMinimumHeight(44);
    sv->addWidget(btStudy);
    auto* btPlan = new QPushButton("Abrir planta do ZenCAD…");
    btPlan->setObjectName("startGhost");
    btPlan->setMinimumHeight(44);
    sv->addWidget(btPlan);
    sv->addStretch(1);

    auto* foot = new QLabel("Zendo · o espaço 3D do ecossistema Zen");
    foot->setObjectName("cardMeta");
    foot->setAlignment(Qt::AlignHCenter);
    sv->addWidget(foot);

    root->addWidget(side);

    connect(btNew,   &QPushButton::clicked, this, [this] { emit newRequested(); });
    connect(btStudy, &QPushButton::clicked, this,
            [this] { emit openStudyRequested(); });
    connect(btPlan,  &QPushButton::clicked, this,
            [this] { emit openPlanRequested(); });

    // ===== Conteúdo: recentes ==============================================
    auto* content = new QVBoxLayout;
    content->setContentsMargins(44, 44, 44, 24);
    content->setSpacing(18);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel("ESTUDOS RECENTES");
    title->setObjectName("secTitle");
    header->addWidget(title);
    header->addStretch(1);
    auto* btGo = new QPushButton("Ir para o espaço  →");
    btGo->setObjectName("startLink");
    btGo->setCursor(Qt::PointingHandCursor);
    header->addWidget(btGo);
    content->addLayout(header);
    connect(btGo, &QPushButton::clicked, this, [this] { emit dismissed(); });

    auto* scroll = new QScrollArea;
    scroll->setObjectName("startScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_gridHost = new QWidget;
    m_gridHost->setStyleSheet("background: transparent;");
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(20);
    m_grid->setVerticalSpacing(20);
    scroll->setWidget(m_gridHost);
    content->addWidget(scroll, 1);

    root->addLayout(content, 1);
}

QWidget* ZendoStartPage::makeCard(const QString& path) {
    auto* card = new CardFrame;
    card->setObjectName("projCard");
    card->setAttribute(Qt::WA_StyledBackground, true);   // QSS pinta o fundo
    card->setFixedSize(288, 234);
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(12, 12, 12, 10);
    v->setSpacing(6);

    auto* thumb = new QLabel;
    thumb->setFixedSize(264, 156);
    thumb->setPixmap(roundedThumb(recentThumbnail(path), QSize(264, 156)));
    v->addWidget(thumb);

    const QFileInfo fi(path);
    auto* name = new QLabel(fi.completeBaseName());
    name->setObjectName("cardName");
    v->addWidget(name);

    const bool isPlan =
        path.endsWith(QLatin1String(".zencad"), Qt::CaseInsensitive);
    const QString when = fi.exists()
        ? fi.lastModified().toString("dd/MM/yyyy HH:mm") : QStringLiteral("—");
    auto* meta = new QLabel(
        (isPlan ? QStringLiteral("planta 2D · ") : QStringLiteral("estudo 3D · ")) +
        when + "   ·   " +
        QFontMetrics(font()).elidedText(fi.absolutePath(), Qt::ElideMiddle, 150));
    meta->setObjectName("cardMeta");
    v->addWidget(meta);

    card->onClick = [this, path] { emit recentRequested(path); };
    return card;
}

void ZendoStartPage::refresh(const QStringList& recents) {
    // Limpa a grade e reconstrói os cards.
    while (QLayoutItem* it = m_grid->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
    if (recents.isEmpty()) {
        auto* empty = new QLabel(
            "Nenhum estudo recente ainda.\n"
            "Comece um Novo estudo ou abra uma planta do ZenCAD — "
            "seus trabalhos aparecerão aqui.");
        empty->setObjectName("cardMeta");
        empty->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_grid->addWidget(empty, 0, 0);
        return;
    }
    int row = 0, col = 0;
    constexpr int kCols = 3;
    for (const QString& p : recents) {
        m_grid->addWidget(makeCard(p), row, col);
        if (++col == kCols) { col = 0; ++row; }
    }
    // Empurra a grade para o topo/esquerda.
    m_grid->setRowStretch(row + 1, 1);
    m_grid->setColumnStretch(kCols, 1);
}

void ZendoStartPage::paintEvent(QPaintEvent*) {
    QPainter p(this);
    // Fundo de PRANCHETA: tinta profunda + grade sutil.
    p.fillRect(rect(), QColor(13, 15, 18));
    p.setPen(QColor(255, 255, 255, 9));
    constexpr int kStep = 46;
    for (int x = 0; x < width(); x += kStep) p.drawLine(x, 0, x, height());
    for (int y = 0; y < height(); y += kStep) p.drawLine(0, y, width(), y);

    // Motivo de CONSTRUÇÃO em latão fraco: o ensō e o cubo em wireframe.
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(194, 160, 99, 22), 1.2));
    const QPointF c(width() * 0.72, height() * 0.32);
    const double R = std::min(width(), height()) * 0.46;
    p.drawEllipse(c, R, R);
    const double e = R * 0.52, h = e * 0.62;
    const QPointF top(c.x(), c.y() - h * 2.05);
    const QPointF left(c.x() - e, c.y() - h * 1.03);
    const QPointF right(c.x() + e, c.y() - h * 1.03);
    const QPointF mid(c.x(), c.y() - h * 0.05);
    const QPointF bl(c.x() - e, c.y() + h * 1.30);
    const QPointF br(c.x() + e, c.y() + h * 1.30);
    const QPointF bot(c.x(), c.y() + h * 2.30);
    for (const auto& ln : {QLineF(top, right), QLineF(right, mid),
                           QLineF(mid, left), QLineF(left, top),
                           QLineF(left, bl), QLineF(bl, bot), QLineF(bot, mid),
                           QLineF(bot, br), QLineF(br, right),
                           QLineF(top, mid), QLineF(mid, bot)})   // diagonais
        p.drawLine(ln);
}
