// src/core/document/Style.hpp
#pragma once
#include <string>
#include <vector>
#include <map>

namespace cad {

// Estilo de cota nomeado (à la DIMSTYLE): parâmetros aplicados às novas cotas.
struct DimStyle {
    std::string name{"Standard"};
    double      textHeight{2.5};
    double      arrowSize{-1.0};   // < 0 = automático (= altura do texto)
    int         decimals{2};
    std::string suffix{};          // ex.: " mm"
    int         arrowType{0};      // 0=seta, 1=tique arquitetônico (/), 2=ponto
    double      tolPlus{0.0};      // tolerância dimensional (0/0 = desligada)
    double      tolMinus{0.0};
    // ANOTATIVO: textHeight/arrowSize passam a valer em MM DE PAPEL; a cota
    // nasce com altura de modelo = mm / escala de anotação e segue a escala.
    bool        annotative{false};
};

// Estilo de texto nomeado (à la STYLE): altura padrão + fonte TTF.
// Arial é o padrão de fábrica (texto "de verdade"); vazia = fonte de traços.
struct TextStyle {
    std::string name{"Standard"};
    double      height{2.5};       // anotativo: em MM DE PAPEL (senão, modelo)
    std::string font{"Arial"};
    bool        annotative{false}; // altura segue a escala de anotação
};

// Tabela de estilos nomeados (cota e texto), cada qual com um estilo corrente.
// Sem Qt: testável. Insere "Standard" por padrão.
class StyleTable {
public:
    StyleTable() {
        m_dim["Standard"] = DimStyle{};
        m_text["Standard"] = TextStyle{};
    }

    // --- Cota ---
    DimStyle& addDim(const DimStyle& s) {        // insere ou atualiza por nome
        DimStyle& slot = m_dim[s.name];
        slot = s;
        return slot;
    }
    const DimStyle* findDim(const std::string& n) const {
        auto it = m_dim.find(n);
        return it == m_dim.end() ? nullptr : &it->second;
    }
    bool removeDim(const std::string& n) {
        if (n == "Standard" || !m_dim.count(n)) return false;   // não remove o padrão
        m_dim.erase(n);
        if (m_curDim == n) m_curDim = "Standard";
        return true;
    }
    std::vector<DimStyle> allDim() const {
        std::vector<DimStyle> v;
        for (const auto& kv : m_dim) v.push_back(kv.second);
        return v;
    }
    bool setCurrentDim(const std::string& n) {
        if (!m_dim.count(n)) return false;
        m_curDim = n; return true;
    }
    const std::string& currentDimName() const { return m_curDim; }
    const DimStyle&     currentDim() const { return m_dim.at(m_curDim); }

    // --- Texto ---
    TextStyle& addText(const TextStyle& s) {
        TextStyle& slot = m_text[s.name];
        slot = s;
        return slot;
    }
    const TextStyle* findText(const std::string& n) const {
        auto it = m_text.find(n);
        return it == m_text.end() ? nullptr : &it->second;
    }
    bool removeText(const std::string& n) {
        if (n == "Standard" || !m_text.count(n)) return false;
        m_text.erase(n);
        if (m_curText == n) m_curText = "Standard";
        return true;
    }
    std::vector<TextStyle> allText() const {
        std::vector<TextStyle> v;
        for (const auto& kv : m_text) v.push_back(kv.second);
        return v;
    }
    bool setCurrentText(const std::string& n) {
        if (!m_text.count(n)) return false;
        m_curText = n; return true;
    }
    const std::string& currentTextName() const { return m_curText; }
    const TextStyle&    currentText() const { return m_text.at(m_curText); }

private:
    std::map<std::string, DimStyle>  m_dim;
    std::map<std::string, TextStyle> m_text;
    std::string m_curDim{"Standard"};
    std::string m_curText{"Standard"};
};

} // namespace cad
