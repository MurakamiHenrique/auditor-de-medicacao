# Protocolo USB

UART0, **115200, 8N1**, comandos JSON terminados por LF. Máximo de **511 bytes por comando**, sem contar LF. Respostas e eventos em JSON Lines; o bootloader/ESP-IDF também podem imprimir linhas de diagnóstico não JSON. O painel preserva essas linhas no log e ignora-as no parser.

A USB serve para **configurar e diagnosticar**. Depois de configurado, o aparelho funciona apenas ligado a uma fonte: obtém a hora por SNTP, arma sozinho a verificação diária e avisa o cuidador pelo Telegram.

Cada comando pode incluir `id` numérico; a resposta `event=reply` repete esse identificador e contém `ok` e `message`. Eventos assíncronos incluem `ms` (milissegundos monotônicos desde boot) e `epoch` UTC (zero se a hora não foi obtida). Timestamps `ms` só são únicos dentro de um boot. A fila de comandos comporta oito itens; aguarde `reply` antes de enviar o próximo comando demorado.

## Comandos

| Comando | Exemplo | Resultado |
|---|---|---|
| Consultar | `{"cmd":"status","id":1}` | Estado, último resultado, rede, Telegram, configuração e heap |
| Wi-Fi | `{"cmd":"wifi","ssid":"casa","password":"segredo123"}` | Salva em NVS e conecta. SSID 1–32 bytes; senha vazia (rede aberta) ou 8–63 |
| Telegram | `{"cmd":"telegram","token":"123456:ABC...","chat_id":"987654321"}` | Salva token do BotFather e chat do cuidador (texto; grupos têm `-` na frente) |
| Testar aviso | `{"cmd":"test_notify"}` | Captura uma foto e envia mensagem de teste |
| Hora manual | `{"cmd":"time","epoch":1790334000}` | Só para bancada sem Wi-Fi; ignorado depois da sincronização SNTP |
| Perfil | `{"cmd":"set","profile":1}` | 0 desativado; 1 AI Thinker; 2 WROVER KIT; trocar apaga referências |
| ROI | `{"cmd":"set","roi":[40,24,80,72]}` | x,y,largura,altura; trocar apaga referências |
| Agenda | `{"cmd":"set","hour":8,"minute":0,"window_s":1800,"interval_s":5,"late_s":1800,"enabled":true}` | Salva e recalcula a próxima verificação |
| Limiares | `{"cmd":"set","max_distance":18,"margin":8,"context_distance":20,"stability":8,"confirmations":2}` | Ajuste experimental |
| Vazio | `{"cmd":"calibrate","label":"empty"}` | Coleta cinco amostras, valida estabilidade e salva referência |
| Presente | `{"cmd":"calibrate","label":"present"}` | Idem; valida separação entre classes quando ambas existem |
| Limpar referências | `{"cmd":"clear_calibration"}` | Remove referências; a agenda para até nova calibração |
| Imagem | `{"cmd":"snapshot"}` | Imagem cinza 160 × 120 em base64, apenas USB |
| Leitura de teste | `{"cmd":"sample","notify":true}` | Classifica e registra, sem mudar a agenda; com `notify`, envia 🔎 o resultado, as distâncias e a foto ao Telegram (o painel sempre envia) |
| Teste de ciclo | `{"cmd":"arm","delay_s":10}` | Verificação extra em 1–86400 s, com avisos marcados `[TESTE]`; depois a agenda diária volta sozinha |
| Recalcular agenda | `{"cmd":"arm"}` | Recalcula a próxima ocorrência do horário salvo |
| Cancelar | `{"cmd":"cancel"}` | Interrompe a verificação em andamento; a agenda diária continua |

Durante uma verificação (`monitoring`, ou `alert` dentro do período `late_s`) somente `status` e `cancel` são aceitos. Enquanto está `armed` (aguardando o horário), todos os comandos funcionam; alterações de agenda, ROI, perfil, calibração ou hora recalculam a próxima verificação.

**Segredos:** senha do Wi-Fi e token do bot ficam em namespaces NVS próprios (`wifi`, `telegram`), nunca são ecoados pelo firmware e o painel os mascara (`***`) no histórico. A NVS não é criptografada: quem tiver acesso físico à placa pode ler a flash.

## Agenda diária

- Uma verificação por dia, no horário `hour:minute` em **UTC−3 fixo** (sem horário de verão, como no Brasil desde 2019).
- A janela dura `window_s` (até 12 h). Capturas a cada `interval_s`.
- Não é preciso rearmar: ao terminar uma verificação, a do dia seguinte é agendada sozinha.
- A presença da dose é verificada **dentro** da janela. Retirada só é confirmada depois de a dose ter sido vista e de `confirmations` leituras vazias consecutivas.
- Se o aparelho liga ou é reconfigurado com a janela de hoje aberta, a verificação começa na hora com o tempo restante, e o aviso informa “janela retomada após reinício”.
- `window_s + late_s` deve ser no máximo 23 h, para não sobrepor o dia seguinte.

## Validação e persistência

- ROI de no mínimo 32 × 32, dentro de 160 × 120 e ocupando no máximo 75% da cena.
- Horas 0–23, minutos 0–59; intervalo de 1–3600 s; janela até 43200 s e suficiente para o número de confirmações (2–10); `late_s` de 0 a 43200.
- `max_distance`: 1–80; `margin`: 1–40; `context_distance`: 1–80; `stability`: 1–40. Valores não finitos são rejeitados.
- Alterações são persistidas antes de serem aplicadas. Erro NVS preserva a configuração anterior. Nunca se apaga NVS automaticamente.
- Versão de esquema (2) e checksum detectam alterações acidentais. Um registro da versão 1 não é carregado: o firmware volta aos padrões e é preciso reconfigurar e recalibrar.
- Hora, resultado e verificação em andamento não são restaurados após reset. Não se deduz retirada no período desligado.

## Eventos

- `boot`: versão, leitura da NVS, inicialização da rede e do Telegram, motivo do reinício e `psram_bytes` (0 sem PSRAM).
- `status`: estado atual; emitido também a cada 10 segundos. Inclui `last_result`, `next_epoch`, `clock_source` (`sntp`/`manual`/`none`), `wifi_connected`, `rssi`, `telegram_configured`, `notify_pending`, `heap` e `min_heap`.
- `observation`: classe, estabilidade, sucesso de captura, distâncias, movimento entre os dois quadros (`motion_roi`, `motion_context`, comparáveis a `stability`) e duração da leitura completa.
- `transition`: mudança de estado, com `demo` e `resumed`.
- `notify`: tentativa de envio ao Telegram (`status` HTTP, `attempt`, `delivered`, `final`, `pending`).
- `wifi` e `clock`: conexão/desconexão e primeira sincronização SNTP.
- `reply`: confirmação/erro do comando.
- `image`: largura, altura e `gray_base64`, uma imagem de desenvolvimento solicitada explicitamente.

## Estados e avisos ao cuidador

| Estado | Significado | Aviso Telegram |
|---|---|---|
| `idle` | Agenda parada: falta hora, câmera, calibração, ou `enabled=false` | — |
| `armed` | Aguardando o horário (`next_epoch`) | — |
| `monitoring` | Janela em andamento | — |
| `removed` | Dose vista e depois vazio persistente | ✅ texto |
| `not_loaded` | Vazio persistente sem a dose ter sido vista na janela | ⚠️ texto + foto |
| `alert` | Dose presente no fim da janela; continua observando por `late_s` | 🔔 texto + foto |
| `late_removed` | Retirada confirmada durante `late_s` | ✅ texto |
| `fault` | Evidência insuficiente, imagem inconclusiva ou captura ausente/atrasada | ❌ texto + foto, se houver uma da janela |

Ao ligar, o aparelho envia também 🔌 “Auditor ligado”, com o motivo do reinício e a próxima verificação. Mensagens sem Wi-Fi ficam na fila (até 8) e são reenviadas quando a rede volta; erros HTTP 5xx/429 são tentados até 20 vezes com espera crescente (até 5 min). Erros 4xx (token ou chat errado) não são repetidos.

A foto é o último quadro JPEG 160 × 120 analisado na janela, enviado só ao chat configurado. Enquadre apenas a caixa, para não fotografar pessoas.
