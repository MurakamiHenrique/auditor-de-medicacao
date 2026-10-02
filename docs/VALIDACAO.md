# Protocolo de bancada

Este documento descreve ensaios a executar na placa. Não é um relatório de resultados obtidos. Use objetos que representem comprimidos, como previsto na Tarefa 02. A aprovação de testes sintéticos de software não substitui validação óptica.

## Preparação

Registre versão/commit do firmware, rede Wi-Fi e RSSI, fonte de alimentação, marcação da placa, perfil de pinagem, sensor, fonte/cabo USB, caixa, objetos, posição e distância da câmera, iluminação, ROI, limiares e arquivo JSONL. Fixe câmera e caixa. Desative qualquer ajuste automático externo da iluminação durante um lote. Separe as amostras de calibração das observações de teste. Se mudar câmera, caixa, iluminação ou recorte, calibre novamente e inicie um lote novo.

## Ensaios funcionais

Salvo indicação, use `arm` com `delay_s` (botão **Teste: verificar em 10 s**) para não esperar o horário real, e confira o aviso recebido no Telegram além do evento serial.

| Ensaio | Procedimento | Resultado esperado |
|---|---|---|
| Sem Wi-Fi/hora | Ligar sem rede configurada | Estado `idle`, `clock_source=none`; agenda não arma |
| Hora por SNTP | Configurar Wi-Fi | Evento `clock` com `sntp`; estado `armed` com `next_epoch` correto em UTC−3 |
| Sem referência | Remover calibração | Agenda para (`idle`); teste recusado |
| Telegram | `test_notify` | Mensagem com foto no chat; evento `notify` com `status` 200 |
| Credencial errada | Salvar token ou `chat_id` inválido e testar | `notify` com 4xx e `final=true`, sem novas tentativas |
| Retirada | Dose presente no início; retirar e afastar a mão | `removed` só após observações vazias consecutivas; ✅ sem foto |
| Já vazio | Iniciar a janela sem dose | `not_loaded`; ⚠️ com foto |
| Transitório | Mostrar vazio por uma leitura e repor | Não confirmar retirada |
| Oclusão | Cobrir a câmera ou passar a mão, sem retirar objeto | Inconclusivo/falha, nunca retirada |
| Escuro/saturado | Alterar iluminação fortemente | Inconclusivo/`fault`; ❌ com foto |
| Dose pendente | Manter objeto até o fim | `alert`; 🔔 com foto |
| Retirada tardia | Após o 🔔, retirar durante `late_s` | `late_removed`; ✅ “com atraso” |
| Falha no limite | Obstruir a imagem no final | `fault`, sem transformar leitura antiga em retirada |
| Dias seguidos | Deixar a agenda real rodar dois dias (ou ajustar o horário para daqui a poucos minutos, duas vezes) | Um aviso por dia; a janela do mesmo dia não se repete |
| Sem computador | Desconectar a USB do PC e alimentar pela fonte | Aviso 🔌 com a próxima verificação; verificação e aviso no horário |
| Reinício fora da janela | Desligar e religar a fonte | 🔌 com motivo “energia ligada”; agenda rearmada após o SNTP |
| Reinício na janela | Religar no meio da janela | Janela retomada com o tempo restante; aviso indica a retomada |
| Queda de Wi-Fi | Desligar o roteador durante a janela e religar depois | Resultado decidido normalmente; aviso entregue quando a rede volta |
| Queda de tensão | Usar fonte/cabo fraco (se seguro) | Se reiniciar, 🔌 informa *brownout* |
| Falha de câmera | Com placa desligada, desconectar flat; ligar | Câmera indisponível no 🔌; agenda não arma até corrigir |

Não desconecte o flat energizado. Para retomar de falha de câmera, desligue, corrija a montagem e ligue. Fora de uma janela, o firmware tenta iniciar a câmera novamente a cada minuto.

## Acurácia por observação

Colete pelo menos **30 observações presentes**, **30 vazias** e **10 de oclusão/falha**, fora das cinco amostras de cada referência. Use `sample` com o ciclo inativo; não reutilize quadros de calibração como teste. Varie posição dos objetos e oclusões dentro do cenário controlado; não recalibre por amostra.

Crie um CSV de rótulos humanos associando `ms` ao evento `observation` daquele boot:

```csv
ms,truth
12345,present
23456,empty
34567,unknown
```

Os números acima são exemplos de formato, não dados de ensaio. Use os timestamps reais. Cada arquivo de avaliação deve conter um único boot.

```powershell
.\.venv\Scripts\python.exe tools/evaluate.py logs/sessao.jsonl rotulos.csv --output logs/resultado.json
```

O relatório informa matriz de confusão, acerto por classe, ocorrências de oclusão classificada como vazio, quantidade suficiente de amostras e duração máxima. A meta documental é ≥90% em cada classe cheio/vazio. O ensaio de **zero confirmações indevidas** exige também ciclos completos com oclusão; um resultado por imagem não comprova esse requisito temporal.

## Temporização, estabilidade e recursos

Execute **20 ciclos completos**, alternando retirada, dose presente e falha. Registre o resultado de cada ciclo, se ocorreu reset, menor heap observado e tempo do alerta. Para captura/análise, confronte `duration_ms` com a meta de 2000 ms. Para atraso de alerta, subtraia o `deadline_ms` do evento `transition.ms`: meta de até 5000 ms. Meça nas condições mais lentas observadas; o timeout do driver pode atrasar uma falha e precisa ser reportado.

Na primeira inicialização, registre também pico/corrente de operação com equipamento de bancada se disponível. Nenhuma meta de consumo ou autonomia foi verificada por software. Ao alterar limiares para melhorar classificação, repita o conjunto de validação independente e guarde ambos os relatórios.

## Critério de fechamento

O grupo só deve afirmar cumprimento das metas ópticas/elétricas após anexar logs reais, rótulos, condições de bancada e resultados. Se a resolução ou o contraste não separarem as referências, ajuste primeiro enquadramento, caixa e iluminação. `calibrated=false` é uma condição de configuração válida, não deve ser removida para forçar o armamento.
