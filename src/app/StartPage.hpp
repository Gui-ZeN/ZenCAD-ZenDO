// src/app/StartPage.hpp
// Página INICIAL do ZenCAD (estilo aba "Start" do AutoCAD, na identidade
// Sumi & Washi): página inteira dentro da janela — sidebar da marca com as
// ações, fundo de prancheta com linhas de construção em latão, e os projetos
// recentes como CARDS com miniatura (embutida no próprio .zencad).
#pragma once
#include <QWidget>
#include <QString>
#include <QStringList>

class QGridLayout;

namespace cad {

class StartPage : public QWidget {
    Q_OBJECT
public:
    explicit StartPage(QWidget* parent = nullptr);

    // Reconstrói os cards de recentes (chamar ao mostrar a página).
    void refresh(const QStringList& recents);

signals:
    void newRequested();                 // Novo desenho
    void openRequested();                // Abrir projeto...
    void recentRequested(const QString& path);
    void dismissed();                    // "Ir para o desenho"

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QWidget* makeCard(const QString& path);

    QGridLayout* m_grid{nullptr};        // grade dos cards
    QWidget*     m_gridHost{nullptr};    // widget dentro do scroll
};

} // namespace cad
