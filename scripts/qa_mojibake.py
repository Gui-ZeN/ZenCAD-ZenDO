# -*- coding: utf-8 -*-
"""QA de codificação: acha (e conserta) mojibake duplo no fonte.

O QUE É O DEFEITO. Um arquivo UTF-8 foi aberto por um editor que assumiu
cp1252, e salvo de novo como UTF-8. Cada acento vira dois caracteres:
"e-agudo" (U+00E9) vira U+00C3 U+00A9. O compilador não liga (são comentários),
o binário sai idêntico, a UI fica intacta — mas o fonte fica ilegível. Num repo
público, é a cara do projeto.

POR QUE cp1252 E NÃO latin-1. O travessão (U+2014) corrompido produz um
U+20AC — que existe em cp1252 e NÃO existe em latin-1. Uma varredura com
latin-1 devolve zero arquivos e parece boa notícia. Não é: é falso negativo.

POR QUE UM HANDLER PRÓPRIO E NÃO O CODEC DO ftfy. O codec `sloppy-windows-1252`
só remapeia os CINCO bytes que o cp1252 deixa sem atribuir (0x81 0x8D 0x8F 0x90
0x9D). Os outros bytes 0x80-0x9F têm glifo (0x8A é "S-caron"), então um texto
com U+008A cru não fecha o round-trip e escapa da detecção. Este arquivo tem
U+008D naturais (o "o-macron" de "Enso-san" corrompido) e um U+008A. O handler
abaixo trata TODA a faixa C1 como byte direto — superset do codec do ftfy.
Cross-check: `ftfy.fix_encoding` (alto nível) dá saída idêntica a este fixer.

POR QUE POR LINHA E NÃO POR ARQUIVO. Num arquivo MISTO (acento legítimo em um
lugar, mojibake em outro), o round-trip do arquivo inteiro falha no primeiro
acento legítimo — e o arquivo inteiro é dado como limpo. Falso negativo calado.
Por linha, cada uma decide por si.

Uso:
  python scripts/qa_mojibake.py            # varre e lista (exit 1 se achar)
  python scripts/qa_mojibake.py --fix      # conserta
"""
import argparse
import glob
import io
import os
import sys

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

EXTS = (".cpp", ".hpp", ".h", ".c", ".py", ".iss", ".isi", ".md", ".txt",
        ".cmake", ".rc", ".bat", ".yml", ".json", ".ini")
IGNORA = ("build", "build-app", "build-app-rel", "dist", "entregas",
          "__pycache__", ".git")
TETO_PASSES = 4          # mojibake de mojibake: medido = 1 passe basta. Teto por garantia.


def sloppy_encode(s):
    """UTF-8 -> os bytes que um cp1252 do Windows teria produzido.

    Devolve None quando o texto tem caractere que NÃO veio de cp1252 — ou seja,
    quando não é mojibake. É essa recusa que protege o texto legítimo: "SÃO"
    vira 0xC3 0x4F, que não é UTF-8 válido, e a linha fica intocada.
    """
    out = bytearray()
    for ch in s:
        c = ord(ch)
        try:
            out += ch.encode("cp1252")
        except UnicodeEncodeError:
            if 0x80 <= c <= 0x9F:      # controle C1: o byte é ele mesmo
                out.append(c)
            else:
                return None            # caractere real fora do cp1252: não é mojibake
    return bytes(out)


def conserta_linha(linha):
    """Devolve a linha limpa, ou a original se não houver mojibake."""
    atual = linha
    for _ in range(TETO_PASSES):
        b = sloppy_encode(atual)
        if b is None:
            break
        try:
            t = b.decode("utf-8")
        except UnicodeDecodeError:
            break                      # não era mojibake: o round-trip não fecha
        if t == atual:
            break                      # estabilizou
        atual = t
    return atual


def arquivos():
    for p in sorted(glob.glob("**/*", recursive=True)):
        if not p.lower().endswith(EXTS) and os.path.basename(p) != "CMakeLists.txt":
            continue
        partes = p.replace("\\", "/").split("/")
        if any(x in IGNORA for x in partes):
            continue
        if os.path.isfile(p):
            yield p


def varre(p):
    """(bom, eol, linhas_originais, linhas_limpas, n_sujas) ou None se não-UTF8."""
    raw = open(p, "rb").read()
    bom = raw.startswith(b"\xef\xbb\xbf")
    corpo = raw[3:] if bom else raw
    try:
        s = corpo.decode("utf-8")
    except UnicodeDecodeError:
        return None
    eol = "\r\n" if "\r\n" in s else "\n"
    orig = s.split(eol)
    limpo = [conserta_linha(ln) for ln in orig]
    n = sum(1 for a, b in zip(orig, limpo) if a != b)
    return bom, eol, orig, limpo, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fix", action="store_true", help="grava a correção")
    a = ap.parse_args()

    total_arq = total_linhas = total_chars = 0
    vistos = 0
    for p in arquivos():
        r = varre(p)
        vistos += 1
        if r is None:
            print("  !! não é UTF-8 válido: %s" % p)
            continue
        bom, eol, orig, limpo, n = r
        if not n:
            continue
        total_arq += 1
        total_linhas += n
        d = sum(len(x) for x in orig) - sum(len(x) for x in limpo)
        total_chars += d
        print("  %-34s %3d linhas · %d caracteres a menos" % (p, n, d))
        if a.fix:
            # BINÁRIO: o modo texto do Python no Windows troca LF por CRLF e
            # come o BOM — dano mecânico numa leva que existe pra não danificar.
            saida = eol.join(limpo).encode("utf-8")
            if bom:
                saida = b"\xef\xbb\xbf" + saida
            open(p, "wb").write(saida)

    print()
    print("  %d arquivos varridos" % vistos)
    if not total_arq:
        print("  ✓ nenhum mojibake")
        return 0
    print("  %s %d arquivo(s), %d linha(s), %d caracteres"
          % ("CONSERTADO:" if a.fix else "SUJO:", total_arq, total_linhas, total_chars))
    return 0 if a.fix else 1


if __name__ == "__main__":
    sys.exit(main())
