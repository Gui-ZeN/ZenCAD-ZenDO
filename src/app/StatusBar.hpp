// src/app/StatusBar.hpp
#pragma once
#include <QStatusBar>
#include <QVector>
#include <vector>

class QLabel;
class QToolButton;
class QAction;

namespace cad {

// Barra de status do CadCore (estilo AutoCAD): mostra as coordenadas vivas do
// cursor e oferece três alternâncias — SNAP, GRID e ORTHO. É um widget pronto;
// quem instancia conecta os sinais. Não conhece o MainWindow.
class CadStatusBar : public QStatusBar {
    Q_OBJECT
public:
    explicit CadStatusBar(QWidget* parent = nullptr);

    // Atualiza as coordenadas exibidas (formato "X: 12.34   Y: -5.00").
    void setCoords(double x, double y);
    // Define o formato de unidade: casas decimais e sufixo (ex.: " mm").
    void setUnitFormat(int decimals, const QString& suffix);

    // Estado corrente de cada alternância.
    bool snapOn()   const;
    bool gridOn()   const;
    bool orthoOn()  const;
    bool otrackOn() const;
    bool polarOn()  const;
    bool gridSnapOn() const;

    // Alterna programaticamente (usado pelos atalhos F7/F8/F9/F10/F11).
    void toggleSnap();
    void toggleGrid();
    void toggleOrtho();
    void toggleOtrack();
    void togglePolar();
    void toggleGridSnap();
    // Sincroniza o CHECK do menu de incrementos do POLAR (custom = nenhum).
    void setPolarIncrementChecked(double deg);

signals:
    void snapToggled(bool on);
    void gridToggled(bool on);
    void orthoToggled(bool on);
    void otrackToggled(bool on);
    void polarToggled(bool on);
    void polarIncrementChanged(double deg);                     // (legado; Personalizar)
    void polarIncrementsChanged(const QVector<double>& degs);   // MULTI-incrementos do menu
    void polarCustomizeRequested();        // "Personalizar..." (incremento + ângulos extras)
    void snapMaskChanged(unsigned mask);   // OSNAP por tipo (bits via snapBit)
    void gridSnapToggled(bool on);          // grid snap (F9)

private:
    QLabel*      m_coords{nullptr};
    int          m_unitDecimals{2};
    QString      m_unitSuffix;
    double       m_lastX{0.0}, m_lastY{0.0};
    QToolButton* m_snap{nullptr};
    QToolButton* m_grid{nullptr};
    QToolButton* m_ortho{nullptr};
    QToolButton* m_otrack{nullptr};
    QToolButton* m_polar{nullptr};
    QToolButton* m_gsnap{nullptr};
    std::vector<QAction*> m_polarIncActs;   // itens de incremento do menu POLAR
};

} // namespace cad
