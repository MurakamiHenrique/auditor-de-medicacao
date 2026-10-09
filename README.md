# Auditor visual de medicação

Firmware para o **ESP32 clássico com câmera OV2640** ([ESP32-CAM com gravador integrado da RoboCore](https://www.robocore.net/wifi/esp32-cam-com-gravador-integrado)), mais painel USB de configuração e ferramentas de ensaio. O aparelho observa **um compartimento** de uma caixa de medicamentos e, **uma vez por dia**, no horário programado, verifica se a dose foi retirada. O resultado vai para o cuidador pelo **Telegram**.

Depois de configurado, o aparelho funciona **apenas ligado a uma fonte**, sem computador: obtém a hora por Wi-Fi (SNTP), arma sozinho a verificação de cada dia, captura e classifica na própria placa e envia o aviso. O computador só é usado para cadastrar Wi-Fi, Telegram, horário, recorte e calibração. A observação de retirada **não comprova ingestão**.

O projeto usa ESP-IDF e o driver oficial [espressif/esp32-camera](https://github.com/espressif/esp32-camera). A comparação de referências segue o método especificado na Tarefa 02; não há rede neural nem pesos fictícios de treinamento.

## O que está implementado

- Captura JPEG 160 × 120, conversão local para cinza e ROI configurável de um compartimento.
- Calibração cheio/vazio com cinco amostras estáveis por referência, persistidas em NVS.
- Classificação `present`, `empty` ou `unknown`, rejeitando distância excessiva, ambiguidade, iluminação extrema e mudanças no entorno.
- **Agenda diária automática:** hora por SNTP (UTC−3), janela de tolerância, rearmamento sozinho a cada dia e retomada da janela se o aparelho reiniciar dentro dela.
- Confirmação de retirada somente depois de ver a dose na janela e de duas ou mais observações vazias consecutivas.
- Resultados distintos: retirada, compartimento já vazio no horário, alerta de dose não tomada, retirada com atraso e falha de leitura.
- **Avisos pelo Telegram**, com foto da caixa nos casos que exigem conferência, fila com novas tentativas e aviso de “aparelho ligado” com o motivo do reinício.
- Watchdog de tarefas, reconexão Wi-Fi com backoff e nova tentativa de iniciar a câmera fora da janela.
- Interface serial JSON Lines e painel local para configuração, imagem/seleção de ROI e registros exportáveis.
- Testes do mesmo núcleo C++ usado no firmware, testes das ferramentas e CI.

## Mudanças de escopo em relação à Tarefa 02

A Tarefa 02 previa alerta sonoro (buzzer), botão de reconhecimento e operação com o computador para ajustar a hora e rearmar. **Para o projeto final, o aparelho precisa funcionar sem computador e avisar o cuidador.** Por decisão do grupo:

- **Aviso remoto pelo Telegram** no lugar do buzzer (RF06) e do botão (RF07). Não há alerta sonoro local.
- **Wi-Fi + SNTP** para a hora, no lugar do ajuste manual a cada boot (RF10). Não há RTC.
- **Rearmamento automático diário** (RF11): a presença da dose é verificada dentro da janela de cada dia.
- **RF12 passa a ser obrigatório.** A decisão continua embarcada e independe da rede; sem Wi-Fi, os avisos ficam na fila até a rede voltar.
- **Foto no aviso** (RNF09): somente da caixa, só para o chat cadastrado e só em `not_loaded`, `alert` ou `fault`.
- **MVP com um compartimento e uma dose por dia.** O organizador semanal de 7 compartimentos fica como extensão.

**Pinagem:** o nome comercial ESP32-CAM não basta para garantir a ligação interna. O firmware inicia com `profile=0`, sem acionar a câmera, até selecionar uma pinagem conferida (AI Thinker ou WROVER KIT). Veja [hardware](docs/HARDWARE.md).

**Alimentação e iluminação:** use uma fonte USB 5 V de pelo menos 2 A e um cabo curto. Wi-Fi e câmera juntos causam picos de corrente, e fontes fracas provocam reinícios por *brownout* (o aviso de boot informa esse motivo). A câmera não ilumina a cena: o horário da dose precisa ter luz ambiente suficiente, senão o resultado será `fault`.

## Instalar, compilar e gravar (Windows, macOS e Linux)

Requer Python 3.10+ com pip e Tcl/Tk (para o painel). Rode os comandos **na pasta do projeto**. Não abra dois programas ao mesmo tempo na mesma porta serial: feche o painel antes de gravar.

**Windows (PowerShell):**

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install platformio==6.1.18 -r tools/requirements.txt
.\.venv\Scripts\python.exe tools/build.py            # compila
.\.venv\Scripts\python.exe tools/build.py upload     # grava na placa detectada
.\.venv\Scripts\python.exe tools/panel.py
```

**macOS / Linux (Terminal):**

```bash
python3 -m venv .venv
.venv/bin/python -m pip install platformio==6.1.18 -r tools/requirements.txt
.venv/bin/python tools/build.py            # compila
.venv/bin/python tools/build.py upload     # grava na placa detectada
.venv/bin/python tools/panel.py
```

A primeira compilação baixa o ESP-IDF e as ferramentas (alguns GB) e leva de 10 a 20 minutos; as seguintes levam cerca de 1 minuto.

**Porta da placa.** O gravador da RoboCore usa o conversor **CH340**. `build.py upload`, o painel e o console encontram a placa sozinhos. Se houver mais de uma, informe com `--port`:

| Sistema | Nome da porta | Driver |
|---|---|---|
| Windows | `COM3`, `COM11`… (Gerenciador de Dispositivos → Portas, “USB-SERIAL CH340”) | Instalar o driver CH340 da WCH se a porta não aparecer |
| macOS | `/dev/cu.usbserial-XXXX` (ou `/dev/cu.wchusbserial…` com o driver da WCH) | Já incluído no macOS 11 ou mais novo. Use sempre `/dev/cu.*`, não `/dev/tty.*` |
| Linux | `/dev/ttyUSB0` | Incluído; adicione o usuário ao grupo `dialout` |

No macOS, liste as portas com `ls /dev/cu.*`. Use um cabo USB de dados: cabos só de carga não criam a porta.

**Python no macOS.** O instalador de [python.org](https://www.python.org/downloads/macos/) já traz o Tcl/Tk do painel. Com Homebrew, instale também o Tk: `brew install python-tk@3.12` (troque pela sua versão do Python). O Python que vem com o sistema/Xcode não é recomendado para o painel.

**Bootloader.** O gravador USB já está integrado. Se a gravação não começar: manter BOOT, pressionar/soltar RESET, iniciar a gravação e soltar BOOT. Não faça pontes em pinos cuja função não foi conferida.

No Windows, `tools/build.py` cria e remove um alias de unidade com `subst` para evitar o limite de caminhos do ESP-IDF; `tools/build.ps1` continua funcionando como atalho para ele. Ao alterar `sdkconfig.defaults`, apague `sdkconfig.esp32cam` (gerado) antes de compilar, senão os novos valores não são aplicados.

Versões: PlatformIO Core **6.1.18**, plataforma `espressif32` **6.11.0**, framework ESP-IDF **5.4.1** e `esp32-camera` **2.0.16**. Flash de 4 MB, DIO, uma aplicação sem OTA, NVS de 64 KiB. PSRAM é usada se existir, mas não é exigida.

## Criar o bot do Telegram

1. No Telegram, converse com **@BotFather**, envie `/newbot` e escolha nome e usuário. Guarde o **token** (formato `123456789:AA...`).
2. O cuidador abre uma conversa com o bot e envia qualquer mensagem (bots não podem iniciar conversas). Para vários cuidadores, crie um grupo e adicione o bot.
3. Abra `https://api.telegram.org/bot<TOKEN>/getUpdates` no navegador e copie o número em `"chat":{"id": ...}`. Em grupos, ele começa com `-`.
4. Trate o token como senha: quem o tiver pode enviar mensagens como o bot.

## Primeira configuração

1. Posicione a câmera imóvel sobre **um** compartimento, com luz constante. Fixe a caixa num berço para que ela não se desloque ao abrir a tampa.
2. Abra o painel, confira a porta (a placa vem selecionada) e conecte. Na aba **Câmera**, aplique o perfil cuja pinagem foi conferida — **perfil 2 na placa RoboCore do grupo**. A câmera deve aparecer como disponível.
3. Na aba **Wi-Fi e Telegram**, salve a rede e a senha. Em seguida, salve o token e o `chat_id` e clique **Enviar mensagem de teste**. A foto deve chegar no celular do cuidador. O status passa a mostrar `Hora: sntp`.
4. Clique **Capturar imagem USB**, arraste para selecionar o compartimento e **Salvar ROI**. O recorte deve ter ao menos 32 × 32 pixels e ocupar no máximo 75% da imagem.
5. Deixe o compartimento vazio, afaste as mãos e clique **Calibrar VAZIO**. Reponha a dose, afaste as mãos e clique **Calibrar PRESENTE**. Use **Testar leitura** para confirmar `present`.
6. Na aba **Agenda**, defina hora, minuto e tolerância (por exemplo, 1800 s = 30 min), mantenha **Verificação diária ativa** e salve. O estado muda para **AGENDADO**, com a data e a hora da próxima verificação.
7. Desconecte o computador e ligue a placa na fonte. Em até 2 minutos chega o aviso 🔌 “Auditor ligado”, com a próxima verificação.

**Ensaio rápido:** com o painel conectado, use **Teste: verificar em 10 s** (avisos marcados `[TESTE]`). Retire a dose dentro da janela: deve chegar ✅. Em outro teste, deixe a dose: deve chegar 🔔 com foto e, se você a retirar durante o período extra (`late_s`), ✅ “retirada com atraso”. Cubra a câmera: deve chegar ❌, nunca retirada. Depois de cada teste, a agenda diária volta sozinha.

## Uso diário

O cuidador repõe a dose a qualquer momento antes do horário. No horário, o aparelho observa durante a tolerância e envia **um** aviso por dia:

| Aviso | Quando |
|---|---|
| ✅ Dose retirada | A dose foi vista e depois o compartimento ficou vazio |
| ⚠️ Compartimento já vazio (com foto) | Vazio desde o início da janela: dose não reposta ou tomada antes da hora |
| 🔔 Dose não retirada (com foto) | A dose continua lá no fim da tolerância; o aparelho segue observando por `late_s` |
| ✅ Retirada com atraso | Retirada confirmada durante `late_s`, depois de um 🔔 |
| ❌ Não foi possível verificar (com foto) | Imagem inconclusiva, mão sobre a caixa, escuro, câmera com falha |

A ausência de mensagem num dia também é um sinal: confira se o aparelho está ligado e com Wi-Fi.

## Console e registros

```bash
.venv/bin/python tools/console.py              # macOS/Linux; placa detectada
.\.venv\Scripts\python.exe tools/console.py COM5   # Windows, com porta explícita
```

Digite `status`, `snapshot`, `sample`, `cancel`, `test_notify` ou comandos JSON. A lista completa está em [protocolo USB](docs/PROTOCOLO.md). Console e painel salvam logs em `logs/*.jsonl` enquanto conectados. Senha e token nunca são ecoados pelo firmware; o painel os mascara no histórico.

## Testes

```bash
.venv/bin/python tools/run_tests.py                 # macOS (clang do Xcode: xcode-select --install) / Linux (g++)
.\.venv\Scripts\python.exe -m pip install ziglang==0.13.0   # Windows sem compilador C++
.\.venv\Scripts\python.exe tools/run_tests.py
```

Os testes compilam o núcleo C++ efetivo. Casos incluem limites da ROI, rejeição de referências, comprimido pequeno e deslocado, oclusão simulada, persistência temporal, cálculo da próxima janela (inclusive virada de meia-noite e retomada após reinício), `not_loaded`, alerta com retirada tardia, prazo vencido e captura atrasada. A CI roda os testes no Ubuntu e no macOS. Testes sintéticos não medem acurácia com medicamentos reais, e o Wi-Fi/Telegram só pode ser verificado na placa.

Leia o [protocolo de bancada](docs/VALIDACAO.md) para os ensaios. [Arquitetura e rastreabilidade](docs/ARQUITETURA.md) mapeiam as funções aos requisitos. [Verificação de software](docs/VERIFICACAO.md) registra o que foi efetivamente executado.

## Estrutura

Para a documentação consolidada da implementação, consulte [DOCUMENTACAO_IMPLEMENTACAO.md](DOCUMENTACAO_IMPLEMENTACAO.md).

```text
components/auditor/   visão, agenda e máquina de estados C++ independentes do hardware
main/                câmera, NVS, Wi-Fi/SNTP, Telegram, serial e integração ESP-IDF
tools/               painel, console, avaliação e executor de testes
tests/               testes do núcleo e do protocolo
docs/                montagem, arquitetura, protocolo e validação
Documentacao/        materiais originais do grupo
```
