// src/app/StatusBar.cpp
#include "app/StatusBar.hpp"

#include <QLabel>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QMouseEvent>
#include <cmath>

#include "core/geometry/SnapPoint.hpp"

namespace cad {

namespace {

// Botão de alternância (checkable). O VISUAL vem do QSS global (tema ZenCAD):
// QToolButton + QToolButton:checked (acento latão). Sem estilo inline -> adapta
// ao tema claro/escuro.
QToolButton* makeToggle(const QString& text, bool checked) {
    auto* b = new QToolButton;
    b->setText(text);
    b->setCheckable(true);
    b->setChecked(checked);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

// Menu que NÃO fecha ao clicar em itens checkáveis — permite marcar vários
// OSNAPs numa aberta só (o QMenu padrão fecha a cada clique).
class StayOpenMenu final : public QMenu {
public:
    using QMenu::QMenu;

protected:
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (QAction* a = actionAt(e->pos()); a && a->isCheckable() && a->isEnabled()) {
            a->trigger();     // alterna (e emite triggered) sem fechar o menu
            e->accept();
            return;
        }
        QMenu::mouseReleaseEvent(e);
    }
};

} // namespace

CadStatusBar::CadStatusBar(QWidget* parent) : QStatusBar(parent) {
    // Estilo herdado do QSS global (QStatusBar). Coordenadas só com padding.
    m_coords = new QLabel;
    m_coords->setStyleSheet("padding: 0 10px;");
    m_coords->setMinimumWidth(190);
    setCoords(0.0, 0.0);
    addWidget(m_coords);

    // Alternâncias à direita (padrão: SNAP on, GRID on, ORTHO off).
    m_gsnap  = makeToggle(QStringLiteral("SNAP"),   false);   // grid snap (F9)
    m_snap   = makeToggle(QStringLiteral("OSNAP"),  true);
    m_grid   = makeToggle(QStringLiteral("GRID"),   true);

    // Menu de OSNAP por tipo (seta no botão OSNAP). Fica ABERTO ao marcar itens.
    auto* osnapMenu = new StayOpenMenu(m_snap);
    const struct { const char* label; SnapType type; } kKinds[] = {
        {"Extremidade",  SnapType::Endpoint},     {"Ponto médio",  SnapType::Midpoint},
        {"Centro",       SnapType::Center},       {"Quadrante",    SnapType::Quadrant},
        {"Interseção",   SnapType::Intersection}, {"Perpendicular", SnapType::Perpendicular},
        {"Tangente",     SnapType::Tangent},      {"Próximo",      SnapType::Nearest},
        {"Nó (ponto)",   SnapType::Node},         {"Extensão",     SnapType::Extension},
        {"Paralela",     SnapType::Parallel},     {"Inserção",     SnapType::Insertion},
        {"Centro geométrico", SnapType::GeomCenter},
        {"Interseção aparente", SnapType::AppInt},
    };
    for (const auto& k : kKinds) {
        QAction* a = osnapMenu->addAction(QString::fromUtf8(k.label));
        a->setCheckable(true);
        a->setChecked((kDefaultSnaps & snapBit(k.type)) != 0);   // padrão curado
        a->setData(static_cast<unsigned>(snapBit(k.type)));
    }
    connect(osnapMenu, &QMenu::triggered, this, [this, osnapMenu](QAction*) {
        unsigned m = 0;
        for (QAction* a : osnapMenu->actions())
            if (a->isChecked()) m |= a->data().toUInt();
        emit snapMaskChanged(m);
    });
    m_snap->setMenu(osnapMenu);
    m_snap->setPopupMode(QToolButton::MenuButtonPopup);
    m_ortho  = makeToggle(QStringLiteral("ORTHO"),  false);
    m_polar  = makeToggle(QStringLiteral("POLAR"),  false);
    m_otrack = makeToggle(QStringLiteral("OTRACK"), true);

    // Menu de incrementos do polar (seta no botão POLAR): MULTI-seleção com
    // check (ex.: 45° E 90° juntos) e fica ABERTO ao marcar (StayOpenMenu).
    auto* polarMenu = new StayOpenMenu(m_polar);
    for (double inc : {5.0, 10.0, 15.0, 18.0, 22.5, 30.0, 45.0, 90.0}) {
        QAction* a = polarMenu->addAction(QStringLiteral("%1°").arg(inc));
        a->setCheckable(true);
        a->setChecked(inc == 15.0);          // padrão do viewport (15°)
        a->setData(inc);
        m_polarIncActs.push_back(a);
    }
    connect(polarMenu, &QMenu::triggered, this, [this](QAction* act) {
        if (!act || !act->isCheckable()) return;   // "Personalizar..." não entra
        QVector<double> incs;
        for (QAction* a : m_polarIncActs)
            if (a->isChecked()) incs.push_back(a->data().toDouble());
        emit polarIncrementsChanged(incs);
    });
    polarMenu->addSeparator();
    QAction* aCustom = polarMenu->addAction(QStringLiteral("Personalizar..."));
    connect(aCustom, &QAction::triggered, this, [this] { emit polarCustomizeRequested(); });
    m_polar->setMenu(polarMenu);
    m_polar->setPopupMode(QToolButton::MenuButtonPopup);

    addPermanentWidget(m_gsnap);
    addPermanentWidget(m_snap);
    addPermanentWidget(m_grid);
    addPermanentWidget(m_ortho);
    addPermanentWidget(m_polar);
    addPermanentWidget(m_otrack);

    connect(m_gsnap,  &QToolButton::toggled, this, &CadStatusBar::gridSnapToggled);
    connect(m_snap,   &QToolButton::toggled, this, &CadStatusBar::snapToggled);
    connect(m_grid,   &QToolButton::toggled, this, &CadStatusBar::gridToggled);
    connect(m_otrack, &QToolButton::toggled, this, &CadStatusBar::otrackToggled);
    // ORTHO e POLAR são mutuamente exclusivos (como no AutoCAD).
    connect(m_ortho,  &QToolButton::toggled, this, [this](bool on) {
        if (on && m_polar->isChecked()) m_polar->setChecked(false);
        emit orthoToggled(on);
    });
    connect(m_polar,  &QToolButton::toggled, this, [this](bool on) {
        if (on && m_ortho->isChecked()) m_ortho->setChecked(false);
        emit polarToggled(on);
    });
}

// Marca no menu do POLAR o incremento ativo (nenhum check = valor customizado).
void CadStatusBar::setPolarIncrementChecked(double deg) {
    for (QAction* a : m_polarIncActs)
        a->setChecked(std::abs(a->data().toDouble() - deg) < 1e-9);
}

void CadStatusBar::setCoords(double x, double y) {
    m_lastX = x; m_lastY = y;
    m_coords->setText(QStringLiteral("X: %1%3   Y: %2%3")
                          .arg(x, 0, 'f', m_unitDecimals)
                          .arg(y, 0, 'f', m_unitDecimals)
                          .arg(m_unitSuffix));
}

void CadStatusBar::setUnitFormat(int decimals, const QString& suffix) {
    m_unitDecimals = decimals < 0 ? 0 : decimals;
    m_unitSuffix = suffix;
    setCoords(m_lastX, m_lastY);   // re-renderiza com o novo formato
}

bool CadStatusBar::snapOn()   const { return m_snap->isChecked();   }
bool CadStatusBar::gridOn()   const { return m_grid->isChecked();   }
bool CadStatusBar::orthoOn()  const { return m_ortho->isChecked();  }
bool CadStatusBar::otrackOn() const { return m_otrack->isChecked(); }
bool CadStatusBar::polarOn()  const { return m_polar->isChecked();  }
bool CadStatusBar::gridSnapOn() const { return m_gsnap->isChecked(); }

void CadStatusBar::toggleSnap()   { m_snap->toggle();   }
void CadStatusBar::toggleGridSnap() { m_gsnap->toggle(); }
void CadStatusBar::toggleGrid()   { m_grid->toggle();   }
void CadStatusBar::toggleOrtho()  { m_ortho->toggle();  }
void CadStatusBar::toggleOtrack() { m_otrack->toggle(); }
void CadStatusBar::togglePolar()  { m_polar->toggle();  }

} // namespace cad
