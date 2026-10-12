# Entrega P1 — Auditor Visual de Medicação

PCS 3858 · Turma 3 · Gabriel Dias Tambelli, Henrique Murakami de Paula, Juliana Marcarini Carloni

## O que enviar na tarefa

**Envio do grupo (basta um integrante):**

| Item do enunciado | Arquivo | Situação |
|---|---|---|
| 1. Relatório Técnico (PDF) | `Relatorio_Tecnico_P1.pdf` | Pronto, com fotos (17 páginas) |
| 1. Filmes do protótipo (.mp4) | `videos/video1_ciclo_agendado.mp4` | Pronto (51 s, ciclo ⏰ → 🔔 → ✅ de 11/10) |
| 2. Apresentação (PDF, até 10 min) | `Apresentacao_P1.pdf` | Pronto (12 slides) |

**Envio individual (cada integrante):**

| Item do enunciado | Arquivo | Situação |
|---|---|---|
| 4. Atividade individual (nota 0–10) | `Atividade_Individual_modelo.md` | Cada um preenche a sua e envia |

Item 3 (arguição): sem arquivo. A preparação está em `preparacao/ROTEIRO.md`, seção 6.

## Falta fazer

- [x] Vídeo em `videos/` e fotos em `fonte/fotos/` (prints extras do painel em `fonte/fotos/extras/`).
- [x] PDFs regerados com as fotos. Para regerar depois de editar: `python Entrega-P1/fonte/gerar_pdfs.py`, rodado na pasta `auditor-de-medicacao`.
- [ ] Revisar a seção 14 do relatório (divisão de tarefas) com o grupo: editar `fonte/relatorio.html` e regerar.
- [ ] Ensaiar a apresentação cronometrada (≤ 10 min; divisão das falas em `preparacao/ROTEIRO.md`, seção 5).
- [ ] Voltar a agenda da placa para o horário real (ex.: 08:00, tolerância 1800, observar após o alerta 1800).

## Estrutura

```
Entrega-P1/
├── LEIA-ME.md                      este arquivo
├── Relatorio_Tecnico_P1.pdf        entrega 1
├── Apresentacao_P1.pdf             entrega 2
├── videos/                         filmes .mp4 (entrega 1)
├── Atividade_Individual_modelo.md  entrega 4 (cada um a sua)
├── preparacao/ROTEIRO.md           demonstração, falas e arguição (não enviar)
└── fonte/                          origem dos PDFs (não enviar)
    ├── relatorio.html, apresentacao.html
    ├── gerar_pdfs.py               regera os PDFs (Edge ou Chrome)
    ├── figuras/                    imagens da câmera usadas no texto
    └── fotos/                      fotos do protótipo e prints (extras/ não usados nos PDFs)
```
