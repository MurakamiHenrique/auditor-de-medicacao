# Verificação da entrega

## Versão 2.0.0 — MVP autônomo (Wi-Fi, SNTP, Telegram)

Verificação executada em **02/10/2026**, no Windows, para o código deste repositório.

| Verificação | Resultado observado |
|---|---|
| Núcleo C++ compilado e executado nativamente (Zig 0.13.0, `-Wall -Wextra -Werror`) | **302 verificações aprovadas** |
| Testes Python do protocolo, avaliador e mascaramento de segredos | **6 testes aprovados** |
| Sintaxe dos scripts (`compileall`) e importação de painel/console | Aprovadas |
| Compilação ESP32 / ESP-IDF 5.4.1 (`sdkconfig.esp32cam` regenerado) | **SUCCESS**, sem avisos no código do projeto |
| RAM estática informada pelo PlatformIO | 66.352 bytes de 327.680 (20,2%) |
| Aplicação em flash | 1.186.859 bytes de 4.063.232 (29,2%) |
| Tamanho de `firmware.bin` | 1.187.264 bytes |

O aumento de RAM e flash em relação à versão 1.0.0 vem da pilha Wi-Fi, do lwIP/SNTP, do cliente HTTPS com mbedTLS e do pacote completo de certificados raiz. A RAM estática não inclui o heap usado em execução por Wi-Fi, TLS (~40 KB durante um envio), câmera e buffers. Acompanhe `heap` e `min_heap` no status durante os ensaios. Sem PSRAM, esse é o ponto mais apertado do projeto.

Comandos usados:

```powershell
.\.venv\Scripts\python.exe tools/run_tests.py
.\.venv\Scripts\python.exe -m compileall -q tools tests
powershell -ExecutionPolicy Bypass -File tools/build.ps1
```

### Verificado na placa (02/10/2026)

- Gravação pela USB (CH340, COM11) e boot da versão 2.0.0, com o evento `boot` e `status` periódico.
- PSRAM de 8 MB detectada (4 MB mapeados); heap livre de ~4,3 MB após o boot.
- Câmera OV2640 iniciada no **perfil 2**; o perfil 1 falha na sonda SCCB. Snapshot 160 × 120 coerente.
- `sample` capturou dois quadros estáveis em 168 ms (`unknown`, porque ainda não havia calibração).
- Wi-Fi (hotspot 2,4 GHz, WPA2) conectado com RSSI de −19 a −38 dBm; hora obtida por SNTP.
- `test_notify` entregue ao Telegram com foto: HTTP 200 na primeira tentativa, cerca de 5 s após o comando.
- Após as correções abaixo: 10/10 capturas sem aviso do driver, máximo de 220 ms; ~95 KB de RAM interna livre com Wi-Fi conectado.

Problemas encontrados na placa e corrigidos em `sdkconfig.defaults` / `main/camera.cpp`:

| Sintoma | Causa | Correção |
|---|---|---|
| Reinício em loop após salvar o Wi-Fi (`stack overflow in task sys_evt` / `cam_task`) | Pilhas padrão (2304/2048 B) insuficientes com Wi-Fi e câmera juntos | Pilhas de 4096 B; tarefa da câmera no núcleo 1 |
| Telegram falhava (`mbedtls_ssl_setup -0x7F00`) | mbedTLS só alocava RAM interna, que estava em ~31 KB | `CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC` (PSRAM quando existe) |
| Pouca RAM interna | Buffer RGB (57,6 KB) e cópia JPEG alocados na RAM interna | Os dois passam a preferir a PSRAM |
| Capturas perdidas (`FB-OVF`/`NO-EOI`) | Buffer JPEG automático de 3840 B pequeno para QQVGA | Buffer JPEG fixo de 16 KB |
| Calibração falhando por “cena instável” com tudo parado | Artefatos de bloco do JPEG (qualidade 12) davam ruído de 4–5 níveis entre quadros, perto do limite 8; entorno medido em pixels isolados | JPEG qualidade 6 e entorno pela média 5×5: ruído caiu para ~2,8 (ROI) e ~3,3 (entorno), 12/12 leituras estáveis |

Mudança posterior, verificada só em software e com fotos reais do log (ainda não gravada na placa):

| Sintoma | Causa | Correção |
|---|---|---|
| Remédio pequeno não calibra (“cheio e vazio muito parecidos”, diferença 4,7–8,4) | A média da ROI inteira dilui um comprimido que ocupa ~4% da área | Distância às referências pela média das 10% maiores diferenças: nas mesmas fotos, 31,8 contra ruído de 8,9; margem padrão 8. Testes de comprimido pequeno e deslocado |

Mudança de 11/10/2026, gravada e verificada na placa no mesmo dia:

| Sintoma | Causa | Correção |
|---|---|---|
| Outra cápsula (diferença 52,5 do “cheio” e 46,1 do “vazio”) e o mesmo comprimido deslocado (70,7 e 44,2) davam inconclusivo | Comparar com uma foto “cheio” faz “presente” significar “este comprimido nesta posição” | Só a referência vazia: compensação de brilho pela mediana, células acima de `max(change, ruído+margin)` e mancha conectada ≥ `min_cells`. Nas fotos reais, vazio e comprimido ficaram com manchas de 0 e 37 células. 429 verificações sintéticas, incluindo comprimido diferente, deslocado, duplo e mudança de luz |

Na placa, depois de calibrar só o vazio (ruído 11–12; limiar por célula 20), as quatro situações foram classificadas corretamente, com resultado e foto entregues no Telegram:

| Situação | Resultado | Maior mancha (mínimo 6) | Diferença (limite 20) | Duração |
|---|---|---|---|---|
| Comprimido usado na calibração anterior | PRESENTE | 59 | 45 | 214 ms |
| Compartimento vazio | VAZIO | 0 | 9 | 203 ms |
| Outra cápsula | PRESENTE | 50 | 60 | 222 ms |
| Mesmo comprimido deslocado | PRESENTE | 66 | 47 | 223 ms |

“Conferir comprimido” mediu manchas de 47 a 54 células; uma tentativa com a mão em movimento foi recusada por cena instável, como esperado. São quatro leituras, não um ensaio de acurácia: o protocolo de `VALIDACAO.md` continua pendente.

Ciclo agendado completo na placa (11/10, 22:56, mensagens com formatação HTML): ⏰ no início da janela, 🔔 com foto ao fim da tolerância com o comprimido presente e ✅ “com atraso” 22 s após o 🔔, depois de duas leituras vazias. Uma janela anterior com sombra sobre a caixa (brilho −44 em todas as leituras) terminou em ❌, como projetado.

### Não verificado (exige a placa)

- Ciclo completo (janela, resultado e aviso) com a caixa calibrada, e operação longa só na fonte.
- Comportamento do watchdog e dos reinícios por *brownout* com a fonte escolhida.
- Pinagem, captura física, calibração, acurácia e tempos (pendências da versão 1.0.0, abaixo).

## Versão 1.0.0 — operação pela USB

Verificação executada em **25/09/2026**: 273 verificações C++, 5 testes Python e compilação ESP32 aprovados (RAM estática 40.424 bytes; aplicação 372.447 bytes). Não foram executados gravação em placa, confirmação da pinagem, captura física, calibração da caixa, medição de acurácia, tempo de resposta, consumo nem 20 ciclos em hardware. Nenhum resultado óptico foi inferido dos testes sintéticos; o roteiro para validar esses pontos está em `VALIDACAO.md`.
