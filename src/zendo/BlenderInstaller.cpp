// src/zendo/BlenderInstaller.cpp — R47
#include "BlenderInstaller.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>

namespace {
// A trinca. sha256 = sidecar oficial blender-4.5.11.sha256 (linha do
// windows-x64.zip); bytes = Content-Length verificado no servidor.
constexpr auto kUrl =
    "https://download.blender.org/release/Blender4.5/"
    "blender-4.5.11-windows-x64.zip";
constexpr auto kSha =
    "e11d3a8e4d4249be5a7db4a9325c1f670037d4233467c3b0bda181001efe44d3";
constexpr qint64 kBytes = 398922208;
// O ESTÁGIO fica FORA do alcance do glob do findBlender ("render/*/blender.exe"
// é UM nível): extração parcial aqui dentro nunca é confundida com instalação
// boa — o exe do zip cai em render/.stage/blender-4.5.11-windows-x64/, dois
// níveis abaixo. Só o rename final publica a árvore completa.
constexpr auto kStage = ".stage";
}  // namespace

QString BlenderInstaller::urlPadrao() { return QString::fromLatin1(kUrl); }
QString BlenderInstaller::sha256Padrao() { return QString::fromLatin1(kSha); }
qint64 BlenderInstaller::bytesPadrao() { return kBytes; }

QString BlenderInstaller::pastaPadrao() {
    QString f = QUrl(QString::fromLatin1(kUrl)).fileName();
    if (f.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) f.chop(4);
    return f;
}

QString BlenderInstaller::destinoPadrao() {
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    if (local.isEmpty()) return QString();
    return local + QStringLiteral("/Zen/render");
}

bool BlenderInstaller::ferramentasOk(QString* falta) {
    for (const auto& t : {QStringLiteral("curl"), QStringLiteral("tar")}) {
        if (QStandardPaths::findExecutable(t).isEmpty()) {
            if (falta) *falta = t + QStringLiteral(".exe");
            return false;
        }
    }
    return true;
}

BlenderInstaller::BlenderInstaller(QObject* parent) : QObject(parent) {}

BlenderInstaller::~BlenderInstaller() {
    // fechar o app no meio do download não deixa filho órfão nem lixo
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->disconnect(this);   // waitForFinished emite finished SÍNCRONO:
                                    // sem isto, o lambda rodaria com o objeto
                                    // já em destruição
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
    limpar();                       // .part E estágio, sempre (o processo já
}                                   // morreu aqui, então o remove funciona)

void BlenderInstaller::instalar(const QString& url, const QString& sha256,
                                const QString& destino, qint64 bytesEsperados) {
    m_url = url;
    m_sha = sha256.toLower();
    m_dest = destino;
    m_bytes = bytesEsperados > 0 ? bytesEsperados : kBytes;
    m_cancelado = false;
    m_baixados = 0;
    m_hashCalc.clear();
    m_rcTar = -1;

    QString falta;
    if (!ferramentasOk(&falta)) {
        falhar(QStringLiteral("%1 não encontrado no sistema").arg(falta));
        return;
    }
    if (m_dest.isEmpty()) {
        falhar(QStringLiteral("sem %LOCALAPPDATA% para instalar o motor"));
        return;
    }
    if (!QDir().mkpath(m_dest)) {
        falhar(QStringLiteral("não consegui criar %1").arg(m_dest));
        return;
    }
    // espaço: o zip + a árvore extraída (~4× o zip). Falhar no gigabyte 0,9
    // depois de 10 minutos de download seria péssimo.
    const qint64 preciso = m_bytes * 4;
    const QStorageInfo si(m_dest);
    // >= 0: bytesAvailable() == 0 é DISCO CHEIO (o caso que mais importa);
    // -1 é "não sei" e aí a checagem é pulada de propósito.
    if (si.isValid() && si.bytesAvailable() >= 0 &&
        si.bytesAvailable() < preciso) {
        falhar(QStringLiteral("espaço insuficiente: precisa de ~%1 GB livres")
                   .arg(double(preciso) / 1e9, 0, 'f', 1));
        return;
    }
    m_part = m_dest + QStringLiteral("/blender.zip.part");
    m_stage = m_dest + QStringLiteral("/") + QLatin1String(kStage);
    limpar();                       // .part/estágio velhos: sempre do zero
    baixar();
}

// R47 (auditoria): se o curl/tar não EXECUTA (bloqueado por AppLocker/política
// — ferramentasOk só prova que o arquivo existe, não que roda), o QProcess
// NUNCA emite finished: a barra ficaria eterna e a QA headless travaria pra
// sempre. errorOccurred é a única saída — e precisa estar ligado ANTES do
// start(), que emite FailedToStart de forma SÍNCRONA no Windows.
void BlenderInstaller::vigiarStart(const QString& quem) {
    connect(m_proc, &QProcess::errorOccurred, this,
            [this, quem](QProcess::ProcessError e) {
                if (e != QProcess::FailedToStart) return;   // Crashed = nosso
                if (m_tick) m_tick->stop();                 // kill, já tratado
                if (m_proc) {
                    m_proc->deleteLater();
                    m_proc = nullptr;
                }
                if (!m_cancelado)
                    falhar(QStringLiteral("não consegui executar o %1 "
                                          "(bloqueado por política?)")
                               .arg(quem));
            });
}

void BlenderInstaller::baixar() {
    emit fase(QStringLiteral("Baixando o motor de render…"));
    m_proc = new QProcess(this);
    vigiarStart(QStringLiteral("curl"));
    connect(m_proc, &QProcess::finished, this, [this](int rc, QProcess::ExitStatus) {
        if (m_tick) m_tick->stop();
        m_baixados = QFileInfo(m_part).size();
        m_proc->deleteLater();
        m_proc = nullptr;
        // só AQUI o curl morreu de verdade (kill é assíncrono) e larga o
        // handle do .part — o limpar() do cancelar() teria falhado e deixado
        // 400 MB de lixo pra trás.
        if (m_cancelado) { limpar(); return; }
        if (rc != 0) {
            falhar(QStringLiteral("download falhou (curl rc=%1) — sem "
                                  "internet ou proxy bloqueando?")
                       .arg(rc));
            return;
        }
        verificarHash();
    });
    // -L segue redirect, --fail transforma HTTP 4xx/5xx em erro (senão o
    // curl gravaria a página de erro no arquivo e sairia com 0); os timeouts
    // impedem que uma conexão que congela sem RST deixe a barra parada pra
    // sempre (sem eles, só o Cancelar salva).
    m_proc->start(QStringLiteral("curl"),
                  {QStringLiteral("-L"), QStringLiteral("--fail"),
                   QStringLiteral("-s"), QStringLiteral("--connect-timeout"),
                   QStringLiteral("30"), QStringLiteral("--speed-limit"),
                   QStringLiteral("1"), QStringLiteral("--speed-time"),
                   QStringLiteral("120"), QStringLiteral("-o"),
                   QDir::toNativeSeparators(m_part), m_url});
    if (!m_proc) return;                 // FailedToStart síncrono já tratou
    // progresso pelo TAMANHO DO ARQUIVO, não pelo stderr do curl: o total já
    // é conhecido (trinca pinada) e ler bytes no disco não depende de parsing.
    m_tick = new QTimer(this);
    connect(m_tick, &QTimer::timeout, this, [this] {
        m_baixados = QFileInfo(m_part).size();
        emit progresso(m_baixados, m_bytes);
    });
    m_tick->start(250);
}

void BlenderInstaller::verificarHash() {
    emit fase(QStringLiteral("Verificando o download…"));
    QFile f(m_part);
    if (!f.open(QIODevice::ReadOnly)) {
        falhar(QStringLiteral("não consegui reler o arquivo baixado"));
        return;
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) {
        falhar(QStringLiteral("falha ao calcular o hash do download"));
        return;
    }
    m_hashCalc = QString::fromLatin1(h.result().toHex());
    f.close();
    // 400 MB corrompem de verdade. Sem esta checagem o modo de falha seria o
    // pior possível: um Blender que extrai "ok" e explode no 1º render.
    if (m_hashCalc != m_sha) {
        QFile::remove(m_part);
        falhar(QStringLiteral("download corrompido (hash não confere) — "
                              "arquivo descartado, tente de novo"));
        return;
    }
    extrair();
}

void BlenderInstaller::extrair() {
    emit fase(QStringLiteral("Instalando o motor…"));
    if (!QDir().mkpath(m_stage)) {
        falhar(QStringLiteral("não consegui criar a pasta de instalação"));
        return;
    }
    m_proc = new QProcess(this);
    vigiarStart(QStringLiteral("tar"));
    connect(m_proc, &QProcess::finished, this, [this](int rc, QProcess::ExitStatus) {
        m_rcTar = rc;
        m_proc->deleteLater();
        m_proc = nullptr;
        if (m_cancelado) { limpar(); return; }   // idem: o tar já largou tudo
        if (rc != 0) {
            falhar(QStringLiteral("falha ao descompactar (tar rc=%1)").arg(rc));
            return;
        }
        promover();
    });
    m_proc->start(QStringLiteral("tar"),
                  {QStringLiteral("-xf"), QDir::toNativeSeparators(m_part),
                   QStringLiteral("-C"), QDir::toNativeSeparators(m_stage)});
}

void BlenderInstaller::promover() {
    // o zip traz UMA pasta raiz (blender-<versão>-windows-x64/)
    QString interno, exe;
    const QDir st(m_stage);
    for (const QFileInfo& fi :
         st.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString cand = fi.absoluteFilePath() + QStringLiteral("/blender.exe");
        if (QFileInfo::exists(cand)) {
            interno = fi.absoluteFilePath();
            exe = cand;
            break;
        }
    }
    if (interno.isEmpty()) {
        falhar(QStringLiteral("o pacote baixado não tem blender.exe dentro"));
        return;
    }
    const QString alvo =
        m_dest + QStringLiteral("/") + QFileInfo(interno).fileName();
    // resto de instalação velha/quebrada no caminho: falhar CEDO e dizendo o
    // que fazer (senão o rename falha e a mensagem vira um genérico inútil).
    if (QFileInfo::exists(alvo) && !QDir(alvo).removeRecursively()) {
        falhar(QStringLiteral("a pasta %1 está em uso — feche o Blender e "
                              "tente de novo")
                   .arg(alvo));
        return;
    }
    if (!QDir().rename(interno, alvo)) {
        falhar(QStringLiteral("não consegui publicar o motor em %1").arg(alvo));
        return;
    }
    limpar();                      // some com o .part (400 MB) e o estágio
    const QString exeFinal = alvo + QStringLiteral("/blender.exe");
    if (!QFileInfo::exists(exeFinal)) {   // paranoia: rename mentiu?
        falhar(QStringLiteral("instalação incompleta"));
        return;
    }
    emit fase(QStringLiteral("Motor instalado."));
    emit terminou(true, QString(), exeFinal);
}

void BlenderInstaller::cancelar() {
    m_cancelado = true;
    if (m_tick) m_tick->stop();
    if (m_proc && m_proc->state() != QProcess::NotRunning) m_proc->kill();
    limpar();
    emit terminou(false, QStringLiteral("cancelado"), QString());
}

void BlenderInstaller::falhar(const QString& erro) {
    limpar();
    emit terminou(false, erro, QString());
}

void BlenderInstaller::limpar() {
    if (!m_part.isEmpty()) QFile::remove(m_part);
    if (!m_stage.isEmpty() && QFileInfo::exists(m_stage))
        QDir(m_stage).removeRecursively();
}
