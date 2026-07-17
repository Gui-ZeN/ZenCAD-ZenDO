# -*- coding: utf-8 -*-
"""A prova da varredura de mojibake — e as 3 mutações que a sustentam.

A TESE A PROVAR: a limpeza mexeu SÓ em comentário. Nada de código, nenhuma
string que o usuário lê, nenhum dano mecânico.

POR QUE NÃO "comparar as string literals". Era o meu plano, e ele é necessário
mas não suficiente: não vê BOM engolido, LF virando CRLF, nem truncamento —
dano mecânico fora de string e de comentário. A prova daqui SUBSUME aquela:
tira os comentários (sabendo o que é aspas) e exige o resto BYTE-IDÊNTICO,
mais BOM igual, contagem de linhas igual e fim de linha igual.

POR QUE NÃO "o binário tem que sair idêntico". É miragem, e essa foi a segunda
ideia que morreu: sem `/Brepro` o TimeDateStamp do COFF muda a cada link, e o
PDB carrega o hash do ARQUIVO-FONTE — que mudou, porque mudar o fonte é o
objetivo. A altitude certa da prova é o fonte.

O MOJIBAKE DOS TESTES É MONTADO COM chr(), NUNCA ESCRITO COMO GLIFO. Se ficasse
cru aqui, a própria varredura "consertaria" o dado de teste na corrida seguinte
e a mutação sumiria sozinha, silenciosamente. Não é hipótese: a 1ª versão deste
arquivo foi acusada pelo qa_mojibake.py — o teste do detector sendo comido pelo
detector.

Uso:  python scripts/prova_mojibake.py <arquivo.cpp>
      (as mutações rodam em cópia temporária; nunca escrevem no fonte real)
"""
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# qa_mojibake reconfigura o stdout pra UTF-8 no import — não refazer aqui
# (envolver de novo fecharia o buffer e quebraria o 1º print).
from qa_mojibake import varre        # noqa: E402

# Mojibake montado por code point — ver a nota do cabeçalho.
MOJI_ATIL = chr(0x00C3) + chr(0x00A3)      # "ã" mojibakado
MOJI_ECIR = chr(0x00C3) + chr(0x008A)      # "Ê" — o controle C1 que o cp1252 recusa
MOJI_AAGU = chr(0x00C3) + chr(0x00A1)      # "á" mojibakado


def sem_comentarios(linhas):
    """Tira os `//` sabendo distinguir aspas. O arquivo-alvo não tem `/* */`
    nem raw string (medido) — se um dia tiver, esta função precisa crescer."""
    out = []
    for ln in linhas:
        r, aspas, esc, i = [], False, False, 0
        while i < len(ln):
            c = ln[i]
            if esc:
                esc = False
            elif c == "\\" and aspas:
                esc = True
            elif c == '"':
                aspas = not aspas
            elif c == "/" and not aspas and i + 1 < len(ln) and ln[i + 1] == "/":
                break
            r.append(c)
            i += 1
        out.append("".join(r).rstrip())
    return out


def checa(cond, oque):
    print("  %s %s" % ("ok  " if cond else "FALHOU", oque))
    return cond


def prova(path):
    r = varre(path)
    if r is None:
        print("  não é UTF-8")
        return False
    bom, eol, orig, limpo, n = r
    print("  %d linhas com mojibake em %s" % (n, path))
    print()
    ok = True
    ok &= checa(sem_comentarios(orig) == sem_comentarios(limpo),
                "o CÓDIGO sem comentários é byte-idêntico (nada fora de comentário mudou)")
    ok &= checa(len(orig) == len(limpo), "mesma contagem de linhas (nada truncou)")
    ok &= checa(sum(1 for x in orig if '"' in x) == sum(1 for x in limpo if '"' in x),
                "mesma contagem de linhas com aspas")
    return ok


def mutacoes():
    """As 3 do Fable. Rodam em cópia, nunca no fonte."""
    print()
    print("  ── MUTAÇÕES ─────────────────────────────────────────────")
    tmp = tempfile.mkdtemp()
    ok = True
    try:
        # M3 (especificidade): acento LEGÍTIMO não pode ser tocado. Sem esta, um
        # fixer que destruísse todo acento passaria nas outras duas.
        p3 = os.path.join(tmp, "m3.cpp")
        open(p3, "wb").write("// CASO SÃO PAULO É REFERÊNCIA — não mexa\n".encode("utf-8"))
        ok &= checa(varre(p3)[4] == 0,
                    "M3: acento LEGÍTIMO ('CASO SÃO PAULO É…') fica INTOCADO")

        # M1 (sensibilidade): o U+008A. O cp1252 ESTRITO não pega — foi o falso
        # negativo que me fez achar que o repo estava limpo.
        p1 = os.path.join(tmp, "m1.cpp")
        alvo = "// R60: agora s%so TR%sS lugares\n" % (MOJI_ATIL, MOJI_ECIR)
        open(p1, "wb").write(alvo.encode("utf-8"))
        r = varre(p1)
        ok &= checa(r[4] == 1 and "TRÊS" in r[3][0],
                    "M1: mojibake com U+008A é DETECTADO e vira 'TRÊS'")

        # M2 (a que sustenta tudo): mexer DENTRO de uma string tem que REPROVAR.
        # Sem esta, a prova passaria mesmo destruindo a UI do usuário.
        p2 = os.path.join(tmp, "m2.cpp")
        src = 'app.setApplicationName(QStringLiteral("Zendo"));  // ol%s\n' % MOJI_AAGU
        open(p2, "wb").write(src.encode("utf-8"))
        r = varre(p2)
        sabotado = list(r[3])
        sabotado[0] = sabotado[0].replace('"Zendo"', '"ZendoX"')
        ok &= checa(sem_comentarios(r[2]) != sem_comentarios(sabotado),
                    "M2: mexer DENTRO de uma string REPROVA a prova (não é teatro)")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return ok


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("uso: prova_mojibake.py <arquivo>")
        sys.exit(2)
    bom = prova(sys.argv[1])
    bom &= mutacoes()
    print()
    print("  %s" % ("PROVA: ok" if bom else "PROVA: FALHOU"))
    sys.exit(0 if bom else 1)
