// src/zendo/StartPage3D.hpp
// Tela INICIAL do Zendo (irmã da StartPage do ZenCAD, no mesmo Sumi & Washi):
// sidebar da marca (cubo no ensō pintado a QPainter) com as ações, fundo de
// prancheta com o motivo isométrico em latão, e os estudos recentes como
// CARDS com miniatura (embutida no próprio .zendo; plantas usam a do .zencad).
#pragma once
#include <QWidget>
#include <QString>
#include <QStringList>

class QGridLayout;

class ZendoStartPage : public QWidget {
    Q_OBJECT
public:
    explicit ZendoStartPage(QWidget* parent = nullptr);

    // Reconstrói os cards de recentes (chamar ao mostrar a página).
    void refresh(const QStringList& recents);

signals:
    void newRequested();                 // Novo estudo
    void openStudyRequested();           // Abrir estudo 3D...
    void openPlanRequested();            // Abrir planta 2D...
    void recentRequested(const QString& path);
    void dismissed();                    // "Ir para o espaço"

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QWidget* makeCard(const QString& path);

    QGridLayout* m_grid{nullptr};        // grade dos cards
    QWidget*     m_gridHost{nullptr};    // widget dentro do scroll
};
