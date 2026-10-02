# Hardware e enquadramento

A compra informada é o [ESP32-CAM com gravador integrado da RoboCore](https://www.robocore.net/wifi/esp32-cam-com-gravador-integrado). A página descreve ESP32 LX6, OV2640, flash de 4 MB e ausência de slot MicroSD; não fornece esquema elétrico definitivo da câmera. Não se deve transferir automaticamente as especificações das propostas com ESP32-S3 para esta placa.

## Unidade do grupo (conferida em bancada em 02/10/2026)

| Item | Observado |
|---|---|
| Chip | ESP32-D0WD-V3, revisão v3.0, cristal 40 MHz (esptool) |
| Conversor USB | WCH CH340 (VID:PID 1A86:7523) |
| PSRAM | 8 MB detectados; o ESP32 mapeia 4 MB, que viram heap (~4,3 MB livres no boot) |
| Flash | 16 MB detectados (a página informa 4 MB); o firmware usa 4 MB |
| Câmera | OV2640 (PID 0x26) detectada **somente no perfil 2**; imagem coerente |
| Leitura completa (2 quadros) | 168 ms com QQVGA |

**Use o perfil 2 nesta placa.** O perfil 1 (AI Thinker) falha na sonda SCCB com `ESP_ERR_NOT_FOUND`: o clock da câmera está no GPIO21, e não no GPIO0. Apesar do nome comercial ESP32-CAM, a ligação interna desta versão segue a pinagem WROVER KIT.

## Perfis implementados

Selecione um perfil apenas depois de conferir a unidade/esquema correspondente. A identificação fornecida pelo grupo é “ESP32-CAM”, sem confirmação independente da pinagem interna. O perfil padrão é **não confirmado**. A seleção ocorre pela serial e fica em NVS; não é preciso recompilar.

| Sinal da câmera | AI Thinker, perfil 1 | WROVER KIT, perfil 2 |
|---|---:|---:|
| PWDN | 32 | não utilizado |
| RESET | não utilizado | não utilizado |
| XCLK | 0 | 21 |
| SCCB SDA / SCL | 26 / 27 | 26 / 27 |
| D0, D1, D2, D3 | 5, 18, 19, 21 | 4, 5, 18, 19 |
| D4, D5, D6, D7 | 36, 39, 34, 35 | 36, 39, 34, 35 |
| VSYNC / HREF / PCLK | 25 / 23 / 22 | 25 / 23 / 22 |

Origem dos perfis: [CameraWebServer da Espressif, versão 2.0.17](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/ESP32/examples/Camera/CameraWebServer/camera_pins.h). Esse arquivo é usado como referência de pinagem; o firmware deste projeto utiliza ESP-IDF diretamente. Um módulo escrito “WROVER” no encapsulamento não prova que a placa siga WROVER KIT.

Se nenhum perfil corresponder ao esquema da unidade, ajuste o mapeamento em `main/camera.cpp` e documente a revisão. Erro de inicialização não determina por si só qual perfil é correto: pode resultar de cabo, alimentação ou sensor. Não há descoberta automática confiável de pinagem.

## Montagem sem eletrônica adicional

1. Encaixe o flat da OV2640 com a placa desligada, respeitando o lado dos contatos do conector. A câmera acompanha o módulo, mas pode vir desconectada.
2. Use a USB do computador para gravar e configurar. Em operação, alimente pela mesma USB com uma **fonte 5 V de pelo menos 2 A** e cabo curto: Wi-Fi e câmera juntos geram picos de corrente, e fontes fracas causam reinícios por *brownout*.
3. Fixe a câmera e a caixa usando um suporte já disponível. Não apoie objetos sobre contatos energizados.
4. Enquadre um compartimento e parte do seu entorno. Use iluminação ambiente constante. A câmera não ilumina a cena nesta implementação.
5. Posicione a placa onde o Wi-Fi chegue com boa intensidade (RSSI melhor que −75 dBm no status). A antena é integrada: não a cubra com metal.
6. Não ligue buzzer, LED, RTC, OLED nem cartão SD. O firmware não aciona saídas auxiliares. Botões BOOT e RESET conservam as funções de gravação/reinício.

O driver captura JPEG 160 × 120, converte em cinza e reduz somente o recorte a 32 × 32. Para a caixa ainda a comprar, prefira geometria que permita observar diretamente o conteúdo de um compartimento; reflexos, tampas opacas, divisórias e comprimidos da mesma cor do fundo reduzem a separação. A adequação precisa ser demonstrada pela calibração e pelos ensaios, sem atribuir precisão antecipadamente.

## Limites da configuração

Não há alerta sonoro local: por decisão do grupo, o aviso é remoto, pelo Telegram, e depende do Wi-Fi. Sem rede, a verificação continua e os avisos ficam numa fila de até oito mensagens, enviadas quando a rede volta. O histórico detalhado (eventos JSONL) só é gravado com o computador conectado; no uso autônomo, o histórico do cuidador são as mensagens do Telegram.

A hora vem do SNTP a cada boot. Sem Wi-Fi não há hora e a agenda não arma (para bancada, há ajuste manual pela USB). Não há RTC nem bateria: falta de energia interrompe a verificação, e o aviso “Auditor ligado” informa o reinício. Não há medição de consumo.

PSRAM: a página da RoboCore não informa se o módulo tem PSRAM. O firmware a usa se existir e funciona sem ela; o primeiro boot mostra no log do ESP-IDF se ela foi detectada.
