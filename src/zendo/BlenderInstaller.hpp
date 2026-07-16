// src/zendo/BlenderInstaller.hpp
// R47: o MOTOR QUE SE INSTALA — o Fotógrafo baixa o Blender portable sozinho
// na primeira foto, em vez de mandar o usuário pro site. Sem dependência de
// rede nova no app: curl.exe e tar.exe são NATIVOS do Windows 10+ e rodam por
// QProcess (o mesmo padrão assíncrono que o render já usa desde a R36).
#pragma once

#include <QObject>
#include <QString>

class QProcess;
class QTimer;

class BlenderInstaller : public QObject {
    Q_OBJECT
public:
    explicit BlenderInstaller(QObject* parent = nullptr);
    ~BlenderInstaller() override;

    // A TRINCA PINADA {versão, URL, sha256}. Bump é MANUAL, por decisão: o
    // render_cena.py é acoplado à API Python do Blender — pegar "a mais nova"
    // sozinho deixaria uma release quebrar o render sem a gente tocar em nada.
    // Ritual ao bumpar: atualizar a trinca (sha256 vem do sidecar oficial
    // download.blender.org/release/Blender4.5/blender-4.5.N.sha256) e rodar
    // UM render de referência.
    static QString urlPadrao();
    static QString sha256Padrao();
    static qint64 bytesPadrao();
    static QString destinoPadrao();          // %LOCALAPPDATA%/Zen/render
    // nome da pasta da versão pinada ("blender-4.5.11-windows-x64"), derivado
    // da própria URL — a trinca continua num lugar só. O findBlender PREFERE
    // esta pasta: a ordenação lexical dele escolheria "4.5.9" em vez de
    // "4.5.11" ('9' > '1') e usaria a versão não-testada.
    static QString pastaPadrao();
    // curl.exe + tar.exe presentes? Sem eles, o app cai no download manual
    // (o diálogo de sempre) — degradar com elegância, nunca sumir.
    static bool ferramentasOk(QString* falta = nullptr);

    void instalar(const QString& url, const QString& sha256,
                  const QString& destino, qint64 bytesEsperados);

    // provas de QA (preenchidas ao longo da instalação)
    qint64 bytesBaixados() const { return m_baixados; }
    QString hashCalculado() const { return m_hashCalc; }
    int codigoTar() const { return m_rcTar; }

signals:
    void fase(const QString& texto);
    void progresso(qint64 feito, qint64 total);
    void terminou(bool ok, const QString& erro, const QString& exe);

public slots:
    void cancelar();

private:
    void vigiarStart(const QString& quem);   // FailedToStart não pendura o app
    void baixar();
    void verificarHash();
    void extrair();
    void promover();
    void falhar(const QString& erro);
    void limpar();                           // .part + estágio

    QString m_url, m_sha, m_dest, m_part, m_stage, m_hashCalc;
    qint64 m_bytes{0}, m_baixados{0};
    int m_rcTar{-1};
    QProcess* m_proc{nullptr};
    QTimer* m_tick{nullptr};
    bool m_cancelado{false};
};
