# Roteiro da P1 — Auditor Visual de Medicação

Estrutura e checklist da entrega: veja `Entrega-P1/LEIA-ME.md`. Este roteiro é material de preparação do grupo (não precisa ser enviado).

---|---|
| `Relatorio_Tecnico_P1.pdf` | Entrega 1 (gerado de `relatorio.html`) |
| `Apresentacao_P1.pdf` | Entrega 2 (gerado de `apresentacao.html`) |
| `fotos/` | Coloque aqui `prototipo.jpg`, `placa.jpg`, `painel.png`, `telegram.png` |
| `gerar_pdfs.py` | Regera os dois PDFs com as fotos: `python Entrega-P1/fonte/gerar_pdfs.py` |

---

## 1. Preparar a placa (uma vez, no Windows, com o painel FECHADO)

```powershell
cd C:\Users\hmura\Lab-Embarcados\auditor-de-medicacao
.\.venv\Scripts\python.exe tools/build.py upload
.\.venv\Scripts\python.exe tools/console.py
```

No console, digite `{"cmd":"set","margin":8}` e depois `status` (confira `"margin":8`). Saia com `quit`.

Isso grava a versão que compara só com o compartimento vazio (qualquer comprimido, qualquer posição). A calibração antiga é descartada; perfil, ROI e agenda são mantidos. Depois, no painel:

1. Aba **Câmera** → Capturar imagem → arraste a ROI **justa** no compartimento → **Salvar ROI**.
2. Compartimento vazio → **1. Calibrar VAZIO**. Opcional: coloque o menor comprimido → **2. Conferir comprimido**.
3. **Testar leitura** com e sem remédio: deve chegar 🔎 “PRESENTE” e “VAZIO” no Telegram.

Câmera e caixa **não podem se mexer** depois da calibração (fite os dois na mesa/suporte).

## 2. Receber os avisos no seu celular

O bot é o mesmo (mesmo token); muda só o `chat_id`, que passa a ser o da sua conta.

1. No seu celular: Telegram → busque o @usuario do bot → abra a conversa → toque em **Iniciar** (envia `/start`). Mande também um “oi”.
2. No navegador do PC, abra `https://api.telegram.org/bot<TOKEN>/getUpdates`. Procure o trecho `"chat":{"id":123456789,"first_name":"<seu nome>"...,"type":"private"}` e copie o número do `id` (positivo). Confira que o `first_name` é o seu, não o do outro celular.
   - Lista vazia (`"result":[]`)? Mande outra mensagem ao bot e recarregue a página.
3. Painel → aba **Wi-Fi e Telegram** → token (o mesmo de antes), `chat_id` = seu número → **Salvar Telegram** → **Enviar mensagem de teste**. Deve chegar 🧪 com foto no seu celular.

A partir daí, só o seu celular recebe; o anterior deixa de receber.

**Wi-Fi da apresentação:** use o hotspot de um celular em **2,4 GHz** (no iPhone: Ajustes → Acesso Pessoal → **Maximizar compatibilidade**). Salve esse Wi-Fi no painel **antes** da aula e teste no mesmo lugar.

## 3. Gravar os vídeos (.mp4) e as fotos

Grave em casa, com calma; eles também servem de **plano B** se a rede falhar na aula.

| Vídeo | Como fazer |
|---|---|
| `video1_autonomo.mp4` | Painel → Agenda: horário = agora + 3 min, tolerância 60, intervalo 5 → **Salvar agenda** → feche o painel. Tire a placa do PC e ligue no **carregador/power bank**. Filme: chega 🔌 “Auditor ligado… Próxima verificação: HH:MM”; no horário, deixe o remédio → 🔔 “NÃO foi retirada” com foto; retire o remédio → ✅ “retirada com atraso”. |
| `video2_retirada.mp4` | Painel conectado, remédio na caixa → **Teste: verificar em 10 s** → ~15 s depois retire o remédio → ✅ “Dose tomada” (marcada 🧪 teste). |
| `video3_calibracao.mp4` | Gravação da tela + celular: Salvar ROI, Calibrar VAZIO e Testar leitura com comprimidos diferentes e em posições diferentes, chegando ao Telegram. |

- Android grava .mp4. iPhone grava .mov: converta para .mp4 (ex.: HandBrake) antes de enviar. 720p e 30–60 s por vídeo bastam.
- Fotos: `prototipo.jpg` (montagem com caixa e câmera), `placa.jpg` (placa ligada na fonte), `painel.png` (Win+Shift+S no painel), `telegram.png` (print da conversa com o bot). Coloque em `Entrega-P1/fonte/fotos/` e rode `python Entrega-P1/fonte/gerar_pdfs.py`.
- Depois dos vídeos, **volte a agenda para o horário real** (ex.: 08:00, tolerância 1800).

## 4. Demonstração ao vivo (≈ 2 min, depois dos slides)

1. Placa já ligada no power bank, sem PC → mostre a mensagem 🔌 no seu celular.
2. Conecte o PC → painel → **Testar leitura** com o remédio (🔎 PRESENTE + foto) e sem (VAZIO).
3. Remédio na caixa → **Teste: verificar em 10 s** → retire → ✅ retirada.
4. Se sobrar tempo: novo teste sem retirar → em 60 s chega 🔔 com foto.

Se a rede falhar: mostre o `video1_autonomo.mp4` e as mensagens já recebidas no seu celular.

## 5. Divisão da fala (10 min ≈ 3 min 20 s cada)

| Integrante | Slides | Mensagem central |
|---|---|---|
| Gabriel | 1–4 | Problema, evolução do escopo (espiral), hardware e pinagem descoberta em bancada |
| Juliana | 5–8 | Requisitos revistos, firmware em FreeRTOS, máquina de estados, protocolos e mensagens |
| Henrique | 9–12 | Visão (subtração de fundo), resultados, problemas enfrentados, cronograma e próximos passos |

Ajustem a divisão se quiserem, mas cada um deve conseguir explicar **qualquer** slide (seção 6).

## 6. Arguição — perguntas prováveis e respostas curtas

**Por que não usaram a CNN/TinyML da proposta?** Não havia conjunto rotulado de imagens da nossa caixa; uma CNN mal treinada seria menos confiável que comparar com referências calibradas na própria cena. O método atual é explicável, cabe no ESP32 clássico e é testável.

**Como funciona a classificação?** JPEG 160×120 → cinza → ROI reduzida a 32×32 células. Só o compartimento **vazio** é calibrado (subtração de fundo). Em cada leitura: remove-se a mudança global de brilho (diferença mediana), marcam-se as células que mudaram mais que max(20, ruído+8) e mede-se a maior mancha de células vizinhas. Mancha ≥ 6 células = dose presente; ≤ 3 = vazio; entre os dois, ou se o entorno/luz mudou, = inconclusivo.

**Por que só a referência vazia?** A versão anterior comparava com uma foto “cheio” e uma “vazio”. No teste de 11/10, outra cápsula e o mesmo comprimido deslocado deram inconclusivo, porque “presente” virava “aquele comprimido naquela posição”. A distância ao vazio separava bem (10 no vazio, 36–46 com comprimido), então a pergunta passou a ser “está diferente de vazio?”. É um ciclo da espiral: o teste em bancada mudou o método.

**Por que não treinar uma rede neural?** Câmera fixa + “vazio ou ocupado” é exatamente o que subtração de fundo resolve. Uma CNN pediria centenas de imagens rotuladas, seria mais lenta no ESP32 clássico e mais difícil de explicar. Fica como extensão se os ensaios mostrarem falhas por luz.

**E a mudança de luz?** A diferença mediana compensa mudança global de brilho; se for grande demais (> 20 níveis) ou o entorno mudar, a leitura é inconclusiva. Uma sombra parcial pode parecer dose: o erro vai para o lado seguro (alerta a mais, nunca retirada falsa).

**E se uma mão passar na frente?** Cada leitura usa 2 quadros; se eles diferem mais de 8 níveis, é inconclusiva. O entorno também precisa bater com a calibração. A retirada exige 2 leituras vazias consecutivas após a dose ter sido vista. Isso reduz, mas não elimina, o risco; o ensaio de 10 oclusões vai medir.

**O que é a máquina de estados?** Idle → Armed (espera o horário) → Monitoring (lê a cada 5 s) → Removed / NotLoaded / Alert / Fault; Alert → LateRemoved se retirarem em até 30 min. Falta de dado leva a Fault, nunca a “retirada”.

**Onde entra o RTOS?** FreeRTOS do ESP-IDF: tarefa principal (laço de 100 ms), tarefa da USB, tarefa do Telegram com fila (a rede nunca atrasa a captura), tarefa da câmera no núcleo 1, tarefas de Wi-Fi/lwIP; watchdog de 30 s.

**Como sabe a hora sem RTC?** SNTP (servidores ntp.br e pool.ntp.org) a cada boot, fuso UTC−3. Sem Wi-Fi não há hora e a agenda não arma (há ajuste manual pela USB para bancada).

**O que acontece se cair a energia?** Configuração e calibração estão na NVS. Ao voltar, envia “Auditor ligado (motivo)”, pega a hora e rearma; se a janela de hoje está aberta, retoma. Nunca supõe retirada no período desligado.

**E sem Wi-Fi?** A verificação continua; os avisos ficam numa fila de 8 e são reenviados (até 20 tentativas com espera crescente) quando a rede volta.

**Como é a comunicação com o Telegram?** HTTPS (TLS com mbedTLS e certificados raiz) para a Bot API: `sendMessage` com JSON e `sendPhoto` com multipart/form-data contendo o JPEG. O destino é o `chat_id` (pessoa ou grupo).

**E a comunicação com o PC?** UART pela USB (conversor CH340), 115200 8N1, um comando JSON por linha; a placa responde com eventos JSON. O painel é Python/Tkinter.

**Qual foi o problema mais difícil?** Memória: com Wi-Fi + câmera + TLS faltava RAM interna (estouro de pilha e falha do TLS). Resolvido com pilhas maiores, câmera no núcleo 1 e buffers/TLS na PSRAM, e iniciando a câmera antes do Wi-Fi.

**Por que a câmera não funcionava no início?** A placa da RoboCore usa a pinagem WROVER KIT (XCLK no GPIO21), não a AI-Thinker. Criamos perfis de pinagem selecionáveis.

**Que segurança/privacidade existe?** Imagens processadas localmente; foto 160×120 enviada só ao chat do cuidador e só quando precisa conferir. Senha e token ficam na NVS e nunca aparecem nos registros. Limite: a NVS não é criptografada.

**O que falta?** Suporte fixo, ensaio de acurácia (30+30 leituras, 10 oclusões), 20 ciclos e 24 h só na fonte; extensão ao organizador semanal.

**Qual o consumo?** Não foi medido; Wi-Fi + câmera pedem fonte de 2 A. Deep sleep saiu do escopo porque o aparelho fica na tomada.

## 7. Atividade individual (cada um envia a sua)

Modelo, para cada integrante preencher com a **própria** avaliação:

> **Avaliação individual — P1 — [Nome], NUSP [número]**
>
> **Resultados x planejamento:** [o que foi entregue em relação ao cronograma da Tarefa 02, por exemplo: fluxo completo e Telegram adiantados; ensaios quantitativos e suporte mecânico pendentes].
>
> **Evolução física:** [estado do protótipo: placa funcionando só na fonte, câmera, calibração, avisos com foto; o que falta montar].
>
> **Documentação:** [relatório, README, protocolo, testes e CI; o que falta].
>
> **Minha contribuição:** [atividades concretas que você fez].
>
> **Nota do grupo (0–10):** [ ] — justificativa.
> **Minha nota (0–10):** [ ] — justificativa.

## 8. Antes de enviar

- [ ] Revisar a seção 14 do relatório (divisão de tarefas) para refletir o que cada um fez de fato.
- [ ] Fotos em `fotos/` e PDFs regerados.
- [ ] Enviar na tarefa: os 2 PDFs + os vídeos .mp4 (um integrante); atividade individual (cada um).
- [ ] Ensaiar a apresentação cronometrada (≤ 10 min) e a demonstração no Wi-Fi que será usado.
