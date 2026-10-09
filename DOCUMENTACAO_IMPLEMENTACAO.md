# Documentação completa da implementação

Este documento explica o código do Auditor Visual de Medicação (versão 2.0.0, MVP autônomo) e serve como guia de revisão antes da montagem com o ESP32-CAM e a caixa de medicamentos.

## Objetivo e escopo

O equipamento observa **um compartimento** e, **uma vez por dia**, no horário configurado, compara a cena com referências calibradas de compartimento vazio e com dose. O processamento e a decisão ocorrem no ESP32. O resultado é enviado ao cuidador pelo Telegram. Depois de configurado, o aparelho funciona apenas ligado a uma fonte, sem computador. A retirada visual não comprova a ingestão, não identifica medicamentos e não reconhece pessoas.

A agenda arma sozinha quando há hora válida (SNTP), câmera e calibração. A retirada só é confirmada depois de a dose ter sido vista na janela e de duas observações consecutivas de vazio. Uma cena ambígua, instável, escura, saturada ou encoberta gera resultado inconclusivo ou falha, nunca uma retirada automática.

## Base do projeto e revisão de escopo

`Documentacao/Tarefa 02 - Turma 3.docx` é a especificação consolidada original. Para o projeto final, o grupo revisou o escopo:

- O aparelho deve operar sem computador. O PC só cadastra Wi-Fi, Telegram, horário, ROI e calibração.
- A hora vem de Wi-Fi + SNTP, em vez de ser ajustada pela serial a cada boot.
- A verificação é diária e rearma sozinha, em vez de exigir rearmamento manual.
- O cuidador é avisado pelo Telegram, com foto nos casos que exigem conferência. Não há buzzer nem botão.
- O MVP tem um compartimento e uma dose por dia. O organizador semanal de 7 compartimentos é extensão futura.

O projeto usa o driver oficial [esp32-camera](https://github.com/espressif/esp32-camera), que suporta ESP32 e OV2640. Não há CNN nem pesos artificiais: o classificador é uma comparação explicável e embarcada.

## Hardware

A placa RoboCore tem ESP32 clássico, câmera OV2640, Wi-Fi com antena integrada e gravador USB. A página comercial não fornece a pinagem definitiva da câmera nem informa PSRAM. O perfil `0` deixa a câmera desativada; o perfil `1` usa a pinagem AI Thinker e o `2`, a WROVER KIT (`main/camera.cpp`). A PSRAM é usada se existir, mas não é exigida. Alimente com fonte 5 V de pelo menos 2 A.

## Organização do código

- `components/auditor/core.cpp`: validação, visão, cálculo da próxima janela diária e máquina de estados, sem dependência de hardware.
- `components/auditor/include/auditor.hpp`: tipos, parâmetros e API do núcleo.
- `main/camera.cpp`: perfis, captura JPEG, decodificação RGB888, luminância e cópia do último JPEG para as fotos.
- `main/storage.cpp`: persistência NVS da configuração e calibração, com versão (2) e checksum.
- `main/network.cpp`: Wi-Fi em modo estação com reconexão por backoff, credenciais em NVS e SNTP (UTC−3).
- `main/notify.cpp`: tarefa do Telegram com fila, novas tentativas, `sendMessage` e `sendPhoto` por HTTPS.
- `main/events.cpp`: eventos JSON na serial, compartilhados pelos módulos.
- `main/app_main.cpp`: inicialização, agenda diária, comandos JSON, calibração, transições e textos dos avisos.
- `tools/panel.py`: painel Tkinter com abas de câmera, agenda e Wi-Fi/Telegram, snapshots, ROI e logs.
- `tools/console.py`: console serial sem interface gráfica.
- `tools/evaluate.py`: rótulos humanos, matriz de confusão e metas.
- `tools/build.py`: compilação e upload no Windows, macOS e Linux (`tools/build.ps1` é um atalho no Windows).
- `tests/core_tests.cpp` e `tests/test_tools.py`: testes automatizados.
- `docs/`: arquitetura, hardware, protocolo, validação e verificação.

## Inicialização

O firmware inicializa NVS, lê a configuração, prepara a UART0 em 115200 8N1 e a fila de comandos, e inicia a câmera (perfis 1 ou 2) **antes** do Wi-Fi, para que os buffers DMA encontrem RAM interna contígua. Em seguida, sobe o Wi-Fi, o SNTP e a tarefa do Telegram, emite `boot` (com o motivo do reinício) e entra no laço principal, vigiado pelo watchdog de tarefas (30 s, reinicia a placa).

Configuração, calibração e credenciais sobrevivem ao reset; hora, resultado e verificação em andamento não. Após o SNTP, a agenda arma sozinha. Quando a agenda arma, ou após 2 minutos, o cuidador recebe 🔌 “Auditor ligado”, com o motivo do reinício, eventuais pendências (sem hora, câmera indisponível, sem calibração) e a próxima verificação.

O registro NVS possui versão e checksum FNV-1a. Erro de leitura, versão ou checksum não apaga a memória automaticamente: o firmware volta aos padrões e informa o erro. Alterar perfil ou ROI apaga as referências, porque elas não servem para outra geometria.

## Pipeline de visão

A câmera captura JPEG QQVGA, 160 × 120, com um framebuffer. O primeiro buffer de cada leitura é descartado. O JPEG é decodificado para RGB888 em 57.600 bytes e convertido para cinza com `gray = (77*R + 150*G + 29*B) >> 8`. O JPEG original (até 16 KB) é guardado como foto da última leitura.

A ROI deve estar dentro da imagem, ter no mínimo 32 × 32 pixels e ocupar no máximo 75% da cena. Ela é reduzida a 32 × 32 por médias de blocos. Até 192 pontos fora da ROI acompanham o entorno e ajudam a rejeitar deslocamento e mudança global de iluminação.

A distância às referências é a média das 10% maiores diferenças entre as células da ROI, para que comprimidos pequenos não se diluam na média; a estabilidade entre quadros e o entorno usam a média das diferenças. A classificação só é aceita quando:

- há duas referências válidas;
- a separação entre elas é maior que `2 * margin`;
- o entorno está estável;
- a diferença entre as duas distâncias supera `margin`;
- a distância escolhida fica abaixo do limite configurado e de 45% da separação.

## Calibração

Cada comando de calibração coleta cinco quadros. O primeiro define a cena; os seguintes precisam permanecer próximos dele. A referência é salva como `present` ou `empty`. Ao concluir as duas referências, o firmware rejeita cenas pouco separadas ou com entorno instável.

Padrões: ROI `[40,24,80,72]`, perfil `0`, horário `08:00`, janela de 60 s, intervalo de 5 s, observação extra após alerta de 1800 s, duas confirmações, distância máxima 18, margem 8, distância de contexto 20 e estabilidade 8. Para uso real, uma tolerância de 30 min (`window_s=1800`) é mais adequada que os 60 s de demonstração.

## Agenda e máquina de estados

```text
IDLE -> ARMED -> MONITORING -> REMOVED
                           \-> NOT_LOADED
                           \-> FAULT
                           \-> ALERT -> LATE_REMOVED
(qualquer resultado) -> ARMED (dia seguinte, automaticamente)
```

`next_window_offset` calcula, a partir da hora local, quanto falta para a janela do dia. O valor é negativo quando a janela de hoje está aberta: nesse caso, depois de um boot ou de uma reconfiguração, a janela é retomada com o tempo restante. Depois de concluída, a janela do mesmo dia nunca é repetida. Os prazos ativos usam milissegundos monotônicos de 64 bits.

Dentro da janela:

- A presença da dose precisa ser vista.
- Vazio persistente depois da presença resulta em `removed`; sem presença, em `not_loaded`.
- No fim da janela, uma observação `present` recente leva a `alert`; qualquer outro caso, a `fault`.
- Em `alert`, a observação continua por `late_s`, e uma retirada confirmada nesse período resulta em `late_removed`.
- Uma leitura concluída depois do fim não confirma retirada.
- Uma lacuna maior que `interval_s + 2 s` quebra a sequência de vazios.

## Avisos ao cuidador

Cada resultado gera um aviso no Telegram. `not_loaded`, `alert` e `fault` levam a foto JPEG 160 × 120 da janela atual. A tarefa de envio roda separada do laço de captura, aguarda o Wi-Fi, tenta até 20 vezes com espera crescente (até 5 min) e não repete erros 4xx, que indicam token ou chat errado. A fila guarda até 8 mensagens. Os avisos de teste (`arm` com `delay_s`) são marcados `[TESTE]`.

## Protocolo USB

Comandos são JSON Lines terminados por LF, com no máximo 511 bytes:

- `status`, `time`, `set`, `calibrate`, `clear_calibration`, `snapshot`, `sample`, `arm`, `cancel`;
- `wifi`, `telegram` e `test_notify`.

```json
{"cmd":"wifi","ssid":"casa","password":"segredo123","id":1}
{"cmd":"telegram","token":"123456:ABC...","chat_id":"987654321","id":2}
{"cmd":"set","profile":1,"roi":[40,24,80,72],"id":3}
{"cmd":"calibrate","label":"empty","id":4}
{"cmd":"calibrate","label":"present","id":5}
{"cmd":"set","hour":8,"minute":0,"window_s":1800,"enabled":true,"id":6}
{"cmd":"arm","delay_s":10,"id":7}
```

Durante uma verificação, somente `status` e `cancel` são aceitos. Os eventos são `boot`, `status`, `observation`, `transition`, `notify`, `wifi`, `clock`, `reply` e `image`. A senha do Wi-Fi e o token nunca são ecoados; o painel os mascara no histórico. Detalhes em [docs/PROTOCOLO.md](docs/PROTOCOLO.md).

## Compilação e gravação

Versões fixadas: PlatformIO 6.1.18, espressif32 6.11.0, ESP-IDF 5.4.1 e esp32-camera 2.0.16. Flash de 4 MB, modo DIO.

```bash
python3 -m venv .venv                     # Windows: python -m venv .venv
.venv/bin/python -m pip install platformio==6.1.18 -r tools/requirements.txt
.venv/bin/python tools/build.py           # Windows: .\.venv\Scripts\python.exe tools/build.py
.venv/bin/python tools/build.py upload
```

Ao mudar `sdkconfig.defaults`, apague `sdkconfig.esp32cam` antes de compilar. O último build gerou cerca de 66 KB de RAM estática e 1,19 MB de aplicação em flash.

## Testes realizados

Os testes C++ compilam o mesmo núcleo do firmware e cobrem:

- ROI, imagens inválidas e cenas escuras/saturadas;
- referências, instabilidade e oclusão sintética;
- cálculo da próxima janela, inclusive virada de meia-noite e retomada;
- `not_loaded`, `alert` com retirada tardia, persistência temporal, atrasos e prazos.

Os testes Python cobrem fragmentação serial, ruído do bootloader, JSON inválido, snapshot, rótulos duplicados, boots múltiplos e mascaramento de segredos. O resultado está registrado em `docs/VERIFICACAO.md`: 302 verificações C++, 6 testes Python e compilação ESP32 aprovados. Wi-Fi, SNTP, Telegram, pinagem e acurácia óptica só podem ser verificados na placa.

## Pendências e limitações

Ainda precisam ser feitos na placa real:

- conferência de PSRAM e da pinagem;
- conexão Wi-Fi, SNTP e entrega no Telegram;
- heap mínimo com Wi-Fi + TLS + câmera;
- calibração da caixa;
- ensaios ópticos (30 presentes, 30 vazias, 10 oclusões);
- 20 ciclos;
- operação sem computador por pelo menos dois dias seguidos.

Limitações conhecidas:

- O aviso depende de Wi-Fi e da internet.
- Não há alerta local, RTC nem bateria.
- A hora tem UTC−3 fixo.
- A iluminação depende do ambiente.
- A NVS não é criptografada.
- A foto tem 160 × 120.
- O algoritmo não identifica medicamentos; reflexos, sombras, tampas, baixo contraste e comprimidos pequenos podem causar `unknown`.
- A classificação não comprova ingestão.

## Documentos complementares

- [README.md](README.md): instalação, bot do Telegram, configuração e uso diário.
- [docs/ARQUITETURA.md](docs/ARQUITETURA.md): rastreabilidade dos requisitos.
- [docs/HARDWARE.md](docs/HARDWARE.md): montagem, pinagem e alimentação.
- [docs/PROTOCOLO.md](docs/PROTOCOLO.md): protocolo serial completo.
- [docs/VALIDACAO.md](docs/VALIDACAO.md): ensaios de bancada.
- [docs/VERIFICACAO.md](docs/VERIFICACAO.md): resultados de software.
