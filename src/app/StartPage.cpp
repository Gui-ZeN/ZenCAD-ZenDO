// src/app/StartPage.cpp
#include "app/StartPage.hpp"
#include "zenio/ProjectIo.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QFrame>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
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

namespace cad {
namespace {

// Card clicável (frame com sinal de clique) — hover vem do QSS (#projCard).
class CardFrame : public QWidget {
public:
    explicit CardFrame(QWidget* parent = nullptr) : QWidget(parent) {
        setCursor(Qt::PointingHandCursor);
    }
    std::function<void()> onClick;

protected:
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && onClick)
            onClick();
    }
};

// Miniatura com cantos arredondados (ou placeholder com motivo de mira).
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
        p.drawPixmap((size.width()  - pm.width())  / 2,
                     (size.height() - pm.height()) / 2, pm);
    } else {
        p.fillRect(out.rect(), QColor(18, 21, 25));
        QPen brass(QColor(194, 160, 99, 70), 1.0);
        p.setPen(brass);
        const QPointF c(size.width() / 2.0, size.height() / 2.0);
        const double r = size.height() * 0.30;
        p.drawEllipse(c, r, r);
        p.drawLine(QPointF(c.x() - r * 1.5, c.y()), QPointF(c.x() + r * 1.5, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - r * 1.5), QPointF(c.x(), c.y() + r * 1.5));
    }
    return out;
}

} // namespace

StartPage::StartPage(QWidget* parent) : QWidget(parent) {
    setObjectName("startPage");

    // QSS local da página (soma ao tema global).
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
    sv->setContentsMargins(32, 44, 32, 28);
    sv->setSpacing(10);

    const QString logoPath = QCoreApplication::applicationDirPath() + "/assets/logo.png";
    if (QFileInfo::exists(logoPath)) {
        auto* pic = new QLabel;
        pic->setPixmap(QPixmap(logoPath).scaledToWidth(248, Qt::SmoothTransformation));
        pic->setAlignment(Qt::AlignHCenter);
        sv->addWidget(pic);
    } else {
        auto* word = new QLabel(
            "<span style=\"font-size:44px; letter-spacing:1px;\">"
            "<span style=\"color:#c2a063; font-weight:600;\">Zen</span>"
            "<span style=\"color:#c9ccd1; font-weight:600;\">CAD</span></span>");
        word->setAlignment(Qt::AlignHCenter);
        sv->addWidget(word);
        auto* tag = new QLabel("DESENHE. PROJETE. CONSTRUA.");
        tag->setObjectName("sideTag");
        tag->setAlignment(Qt::AlignHCenter);
        sv->addWidget(tag);
    }
    sv->addSpacing(26);

    auto* btNew = new QPushButton("Novo desenho");
    btNew->setObjectName("startPrimary");
    btNew->setMinimumHeight(46);
    sv->addWidget(btNew);
    auto* btOpen = new QPushButton("Abrir projeto…");
    btOpen->setObjectName("startGhost");
    btOpen->setMinimumHeight(44);
    sv->addWidget(btOpen);
    sv->addStretch(1);

    auto* foot = new QLabel("ZenCAD · Sumi & Washi");
    foot->setObjectName("cardMeta");
    foot->setAlignment(Qt::AlignHCenter);
    sv->addWidget(foot);

    root->addWidget(side);

    connect(btNew,  &QPushButton::clicked, this, [this] { emit newRequested(); });
    connect(btOpen, &QPushButton::clicked, this, [this] { emit openRequested(); });

    // ===== Conteúdo: recentes ==============================================
    auto* content = new QVBoxLayout;
    content->setContentsMargins(44, 44, 44, 24);
    content->setSpacing(18);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel("PROJETOS RECENTES");
    title->setObjectName("secTitle");
    header->addWidget(title);
    header->addStretch(1);
    auto* btGo = new QPushButton("Ir para o desenho  →");
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

QWidget* StartPage::makeCard(const QString& path) {
    auto* card = new CardFrame;
    card->setObjectName("projCard");
    card->setAttribute(Qt::WA_StyledBackground, true);   // QSS pinta o fundo/hover
    card->setFixedSize(288, 234);
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(12, 12, 12, 10);
    v->setSpacing(6);

    auto* thumb = new QLabel;
    thumb->setFixedSize(264, 156);
    thumb->setPixmap(roundedThumb(projectThumbnail(path), QSize(264, 156)));
    v->addWidget(thumb);

    const QFileInfo fi(path);
    auto* name = new QLabel(fi.completeBaseName());
    name->setObjectName("cardName");
    v->addWidget(name);

    const QString when = fi.exists()
        ? fi.lastModified().toString("dd/MM/yyyy HH:mm") : QStringLiteral("—");
    auto* meta = new QLabel(when + "   ·   " +
        QFontMetrics(font()).elidedText(fi.absolutePath(), Qt::ElideMiddle, 220));
    meta->setObjectName("cardMeta");
    v->addWidget(meta);

    card->onClick = [this, path] { emit recentRequested(path); };
    return card;
}

void StartPage::refresh(const QStringList& recents) {
    // Limpa a grade e reconstrói os cards.
    while (QLayoutItem* it = m_grid->takeAt(0)) {
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }
    if (recents.isEmpty()) {
        auto* empty = new QLabel(
            "Nenhum projeto recente ainda.\n"
            "Crie um Novo desenho e salve com Ctrl+S — ele aparecerá aqui.");
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

void StartPage::paintEvent(QPaintEvent*) {
    QPainter p(this);
    // Fundo de PRANCHETA: tinta profunda + grade sutil.
    p.fillRect(rect(), QColor(13, 15, 18));
    p.setPen(QColor(255, 255, 255, 9));
    constexpr int kStep = 46;
    for (int x = 0; x < width(); x += kStep) p.drawLine(x, 0, x, height());
    for (int y = 0; y < height(); y += kStep) p.drawLine(0, y, width(), y);

    // Motivo de LINHAS DE CONSTRUÇÃO em latão fraco (eco das guias da logo).
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(194, 160, 99, 22), 1.2));
    const QPointF c(width() * 0.72, height() * 0.30);
    const double R = std::min(width(), height()) * 0.46;
    p.drawEllipse(c, R, R);
    p.drawEllipse(c, R * 0.62, R * 0.62);
    p.drawLine(QPointF(c.x() - R * 1.18, c.y()), QPointF(c.x() + R * 1.18, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - R * 1.18), QPointF(c.x(), c.y() + R * 1.18));
    for (int i = 0; i < 4; ++i) {                     // marcas a 45°
        const double a = (45.0 + i * 90.0) * 3.14159265358979 / 180.0;
        const QPointF d(std::cos(a), std::sin(a));
        p.drawLine(c + d * (R * 0.94), c + d * (R * 1.06));
    }
}

} // namespace cad
