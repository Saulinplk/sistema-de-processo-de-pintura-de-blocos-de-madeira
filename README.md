# sistema-de-processo-de-pintura-de-blocos-de-madeira
Um sistema IoT que acompanha o processo de pintura de blocos de madeira em um ambiente simulado de chão de fábrica.

# Sistema IoT — Processo de Pintura de Blocos de Madeira
 
Sistema completo de monitoramento de chão de fábrica utilizando dois ESP32 comunicando via **ESPNOW**, com persistência no **Supabase** e visualização no **Grafana**.
 
---
 
## Visão Geral da Arquitetura
 
```
┌─────────────────────────────┐        ESPNOW         ┌──────────────────────────────┐
│   ESP32 — Chão de Fábrica   │ ────────────────────▶ │  ESP32 — Monitoramento       │
│                             │                        │                              │
│  • Sensor Ultrassônico      │                        │  • Matriz de LEDs (MAX7219)  │
│  • Sensor DHT (temp/umid.)  │                        │  • LED Verde / Vermelho      │
│  • Fotorresistor            │                        │  • Envia dados ao backend    │
│  • Sensor PIR (presença)    │                        └──────────────┬───────────────┘
│  • LED Verde / Vermelho     │                                       │ HTTP POST
└─────────────────────────────┘                                       ▼
                                                        ┌─────────────────────────────┐
                                                        │   Backend Python (FastAPI)  │
                                                        └──────────────┬──────────────┘
                                                                       │ insert
                                                                       ▼
                                                        ┌─────────────────────────────┐
                                                        │        Supabase             │
                                                        │   (PostgreSQL na nuvem)     │
                                                        └──────────────┬──────────────┘
                                                                       │ datasource
                                                                       ▼
                                                        ┌─────────────────────────────┐
                                                        │           Grafana           │
                                                        │   (dashboards em tempo real)│
                                                        └─────────────────────────────┘
```
 
---
 
## Estrutura do Repositório
 
```
/
├── esp32_chao_fabrica/
│   └── esp32_chao_fabrica.ino      ← Firmware do ESP32 Chão de Fábrica
├── esp32_monitoramento/
│   └── esp32_monitoramento.ino     ← Firmware do ESP32 Monitoramento
├── backend/
│   ├── main.py                     ← API Python (FastAPI + Supabase)
│   └── requirements.txt
└── README.md
```
 
---
 
## Equipamentos e Materiais
 
| Componente                    | Qtd | Onde é usado              |
|-------------------------------|-----|---------------------------|
| ESP32                         | 2   | Chão de fábrica + Monitor |
| Sensor Ultrassônico (HC-SR04) | 1   | Chão de fábrica           |
| Sensor DHT22 (temp/umidade)   | 1   | Chão de fábrica           |
| Fotorresistor (LDR)           | 1   | Chão de fábrica           |
| Sensor PIR                    | 1   | Chão de fábrica           |
| LED Verde                     | 2   | Um em cada ESP32          |
| LED Vermelho                  | 2   | Um em cada ESP32          |
| Matriz de LEDs MAX7219        | 1   | ESP32 Monitoramento       |
| Resistores 330Ω               | 4   | LEDs                      |
| Computador com internet       | 1   | Backend + Grafana         |
 
---
 
---
 
# ETAPA 01 — ESP32 Chão de Fábrica
 
## Pinagem
 
| Componente             | Pino ESP32 |
|------------------------|------------|
| HC-SR04 TRIG           | GPIO 5     |
| HC-SR04 ECHO           | GPIO 18    |
| DHT22 DATA             | GPIO 4     |
| Fotorresistor (LDR)    | GPIO 1     |
| PIR OUT                | GPIO 6     |
| LED Verde (+)          | GPIO 7     |
| LED Vermelho (+)       | GPIO 15    |
| LEDs (−)               | GND        |
 
## Passo 1 — Instalar bibliotecas no Arduino IDE
 
Vá em **Sketch → Include Library → Manage Libraries** e instale:
 
| Biblioteca     | Autor           |
|----------------|-----------------|
| DHT sensor library | Adafruit   |
| Adafruit Unified Sensor | Adafruit |
| ArduinoJson    | Benoit Blanchon |
 
## Passo 2 — Descobrir o MAC do ESP32 Monitoramento
 
Grave temporariamente no ESP32 de **monitoramento** o código abaixo, abra o Monitor Serial e copie o MAC exibido:
 
```cpp
#include <WiFi.h>
 const char* ssid = "SEU WIFI";
const char* password = "SENHA";


void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
 
  Serial.println(WiFi.macAddress()); // ex: A4:CF:12:34:56:78
}
void loop() {}

```
 
## Passo 3 — Configurar o firmware `esp32_chao_fabrica.ino`
 
No início do arquivo, preencha:
 
```cpp
// MAC do ESP32 Monitoramento (destino ESPNOW)
uint8_t MAC_MONITORAMENTO[] = {0xA4, 0xCF, 0x12, 0x34, 0x56, 0x78};
 
// Canal Wi-Fi do ESP32 Monitoramento (veja Etapa 02 Passo 3)
#define CANAL_WIFI  6
```
 
## Passo 4 — Gravar e testar
 
1. Selecione **Tools → Board → ESP32 Dev Module** e a porta COM correta.
2. Clique em **Upload**.
3. Abra o Monitor Serial a **115200 baud**.
4. A cada 2 segundos você deve ver:
```
Nível do tanque: 75%
Temperatura: 24.5 ºC | Umidade: 55%
Luminosidade: 680
Presença detectada
Estado: Operação normal
Pacote enviado com sucesso: {nível=75%, temp=24.5°C, umidade=55%, luz=680, presença=1}
```
 
Se nível < 20%:
```
Alerta! Nível de tinta baixo.
```
 
---
 
---
 
# ETAPA 02 — ESP32 de Monitoramento
 
## Pinagem
 
| Componente         | Pino ESP32 |
|--------------------|------------|
| Matriz DIN         | GPIO 23    |
| Matriz CLK         | GPIO 18    |
| Matriz CS          | GPIO 5     |
| Matriz VCC         | 3.3V ou 5V |
| Matriz GND         | GND        |
| LED Verde (+)      | GPIO 26    |
| LED Vermelho (+)   | GPIO 27    |
| LEDs (−)           | GND (330Ω) |
 
## Passo 1 — Instalar bibliotecas no Arduino IDE
 
| Biblioteca  | Autor           |
|-------------|-----------------|
| MD_Parola   | majicDesigns    |
| MD_MAX72XX  | majicDesigns    |
| ArduinoJson | Benoit Blanchon |
 
## Passo 2 — Testar a matriz antes de programar
 
Carregue um exemplo básico para validar o hardware:  
**File → Examples → MD_Parola → Basic → Parola_HelloWorld**
 
Se aparecer texto na matriz → hardware OK ✅
 
## Passo 3 — Descobrir o canal WiFi
 
Grave o firmware `esp32_monitoramento.ino` e abra o Monitor Serial. Você verá:
 
```
[WiFi] Conectado! IP: 192.168.1.105  Canal: 6
>>> Informe ao ESP32 chão de fábrica: Canal WiFi = 6 <<<
```
 
Anote o canal e use-o no firmware do chão de fábrica (Etapa 01 Passo 3).
 
## Passo 4 — Configurar o firmware `esp32_monitoramento.ino`
 
```cpp
const char* SSID     = "NomeDoSeuWifi";
const char* PASSWORD = "SenhaDaRede";
 
// IP do computador rodando o backend Python (mesma rede)
const char* BACKEND_URL = "http://192.168.1.100:8000/dados";
 
// MAC do ESP32 Chão de Fábrica
uint8_t MAC_CHAO_FABRICA[] = {0xB4, 0xE6, 0x2D, 0xAA, 0xBB, 0xCC};
```
 
## Passo 5 — Comportamento esperado
 
**Matriz de LEDs** — ciclo a cada 2 segundos:
```
NVL 75%  →  TMP 24.5C  →  UMD 55%  →  LUX 680  →  PRS ON  →  (repete)
```
 
**Monitor Serial:**
```
[RX] nivel=75.0% temp=24.5C umd=55.0% lux=680 prs=1 ts=2025-09-02T14:35:00Z
[TELA] -> NVL  (NVL 75%)
[LED] VERDE ON – dados recebidos
[HTTP] Dados enviados com sucesso (HTTP 201)
```
 
**Sem dados por 5+ segundos:**
```
[ALERTA] Timeout: sem dados ESPNOW há 5+ segundos.
→ Matriz exibe: SEM DADOS
→ LED Vermelho aceso, LED Verde apagado
```
 
---
 
---
 
# ETAPA 03 — Banco de Dados e Grafana
 
## Passo 1 — Criar conta e projeto no Supabase
 
1. Acesse https://supabase.com e crie uma conta gratuita.
2. Clique em **New Project** → dê um nome (ex: `iot-pintura`) → defina senha do banco → **Create Project**.
3. Aguarde ~2 minutos.
## Passo 2 — Criar a tabela `sensor_readings`
 
1. No menu lateral, clique em **SQL Editor**.
2. Cole o SQL abaixo e clique em **Run**:
```sql
CREATE TABLE sensor_readings (
  id           BIGSERIAL   PRIMARY KEY,
  nivel_tinta  FLOAT       NOT NULL,
  temperatura  FLOAT       NOT NULL,
  umidade      FLOAT       NOT NULL,
  luminosidade INTEGER     NOT NULL,
  presenca     BOOLEAN     NOT NULL,
  timestamp    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```
 
## Passo 3 — Copiar as credenciais
 
1. Vá em **Settings → API**.
2. Copie:
   - **Project URL** → ex: `https://abcdefgh.supabase.co`
   - **anon public key** → começa com `eyJhbGc...`
## Passo 4 — Configurar o backend Python
 
Edite o início de `backend/main.py`:
 
```python
SUPABASE_URL = "https://XXXXXXXXXXXXX.supabase.co"
SUPABASE_KEY = "eyJhbGci..."
```
 
## Passo 5 — Rodar o backend
 
```bash
# Na pasta backend/
pip install -r requirements.txt
python main.py
```
 
Saída esperada:
```
Backend IoT - Camada de Serviço
Supabase URL: https://...
Acesse: http://0.0.0.0:8000
Docs:   http://0.0.0.0:8000/docs
```
 
> Acesse http://localhost:8000/docs para testar manualmente via Swagger UI.
 
## Passo 6 — Testar inserção
 
No Swagger UI, use **POST /dados** com:
 
```json
{
  "nivel_tinta": 75.0,
  "temperatura": 24.5,
  "umidade": 55.0,
  "luminosidade": 680,
  "presenca": 1,
  "timestamp": "2025-09-02T14:35:00Z"
}
```
 
Verifique no Supabase em **Table Editor → sensor_readings** se o registro apareceu ✅
 
## Passo 7 — Configurar o Grafana
 
### 7.1 Instalar o Grafana
 
- **Windows/Mac:** Baixe em https://grafana.com/grafana/download
- **Com Docker:**
```bash
docker run -d -p 3000:3000 --name grafana grafana/grafana
```
 
Acesse http://localhost:3000 → login padrão: `admin` / `admin`.
 
### 7.2 Conectar ao Supabase (PostgreSQL)
 
1. No Grafana, vá em **Connections → Data Sources → Add → PostgreSQL**.
2. Preencha com os dados do Supabase (**Settings → Database**):
| Campo    | Valor                                      |
|----------|--------------------------------------------|
| Host     | `db.XXXXX.supabase.co:5432`                |
| Database | `postgres`                                 |
| User     | `postgres`                                 |
| Password | (senha definida ao criar o projeto)        |
| SSL Mode | `require`                                  |
 
3. Clique em **Save & Test** → deve aparecer "Database Connection OK" ✅
### 7.3 Criar os dashboards
 
Crie um novo dashboard (**Dashboards → New → Add visualization**) e use as queries abaixo:
 
**Nível do Tanque (gauge ou time series):**
```sql
SELECT timestamp AS time, nivel_tinta AS "Nível (%)"
FROM sensor_readings
ORDER BY timestamp DESC
LIMIT 100;
```
 
**Temperatura e Umidade:**
```sql
SELECT timestamp AS time, temperatura AS "Temperatura (°C)", umidade AS "Umidade (%)"
FROM sensor_readings
ORDER BY timestamp DESC
LIMIT 100;
```
 
**Luminosidade:**
```sql
SELECT timestamp AS time, luminosidade AS "Luminosidade"
FROM sensor_readings
ORDER BY timestamp DESC
LIMIT 100;
```
 
**Presença (últimas detecções):**
```sql
SELECT timestamp AS time, presenca::int AS "Presença (0/1)"
FROM sensor_readings
ORDER BY timestamp DESC
LIMIT 100;
```
 
4. Em cada painel, configure **Auto refresh: 5s** para atualização em tempo real.
---
 
---
 
# ETAPA 04 — GitHub
 
## Passo 1 — Criar o repositório (se ainda não existe)
 
```bash
git init
git remote add origin https://github.com/SEU_USUARIO/SEU_REPOSITORIO.git
```
 
## Passo 2 — Criar a branch do projeto
 
```bash
git checkout -b feat/robo-lab
```
 
## Passo 3 — Adicionar todos os arquivos
 
```bash
git add esp32_chao_fabrica/
git add esp32_monitoramento/
git add backend/
git add README.md
```
 
## Passo 4 — Commitar
 
```bash
git commit -m "feat: sistema IoT completo - pintura de blocos de madeira"
```
 
## Passo 5 — Enviar para o GitHub
 
```bash
git push origin feat/robo-lab
```
 
## Passo 6 — Abrir Pull Request
 
No GitHub, clique em **Compare & pull request** para a branch `feat/robo-lab`.
 
---
 
---
 
# ETAPA 05 — Apresentação
 
## Checklist antes de apresentar
 
- [ ] ESP32 chão de fábrica montado e enviando dados via ESPNOW
- [ ] ESP32 monitoramento exibindo na matriz (ciclo NVL → TMP → UMD → LUX → PRS)
- [ ] Backend Python rodando (`python main.py`)
- [ ] Dados aparecendo na tabela do Supabase em tempo real
- [ ] Dashboard do Grafana aberto e atualizando automaticamente
- [ ] Repositório GitHub atualizado com todos os arquivos
- [ ] README.md no repositório com instruções completas
## Roteiro de apresentação (5–10 minutos)
 
1. **Visão geral** — explicar o problema: monitorar tanque de tinta em chão de fábrica.
2. **Demonstrar o ESP32 chão de fábrica** — mostrar o Monitor Serial com as leituras dos sensores.
3. **Demonstrar a comunicação ESPNOW** — mostrar a matriz de LEDs alternando os dados.
4. **Demonstrar o Grafana** — mostrar os gráficos atualizando em tempo real.
5. **Mostrar o código no GitHub** — apontar a branch `feat/robo-lab` e o README.
6. **Dificuldades encontradas** — ex: sincronização do canal WiFi para ESPNOW.
---
 
## Indicadores de Status — Resumo
 
| Situação                      | LED Verde | LED Vermelho | Matriz          |
|-------------------------------|-----------|--------------|-----------------|
| Sistema normal (dados OK)     | Aceso     | Apagado      | Ciclo de telas  |
| Novo pacote recebido          | Piscando  | Apagado      | Atualiza dados  |
| Nível de tinta < 20%          | Apagado   | Aceso        | NVL XX%         |
| Timeout (sem dados > 5s)      | Apagado   | Aceso        | SEM DADOS       |
 
---
 
## Dúvidas Frequentes
 
**ESPNOW não recebe pacotes:**
→ Confirme que ambos os ESP32 estão no **mesmo canal Wi-Fi**.
→ Verifique se o MAC do destino está correto no firmware do transmissor.
 
**Backend não recebe dados:**
→ Confirme que `BACKEND_URL` no firmware usa o IP correto do computador.
→ Computador e ESP32 devem estar na **mesma rede Wi-Fi**.
→ Verifique se o servidor Python está rodando.
 
**Erro ao salvar no Supabase:**
→ Verifique `SUPABASE_URL` e `SUPABASE_KEY` no `main.py`.
→ Confirme que a tabela `sensor_readings` foi criada com o SQL da Etapa 03.
 
**Grafana não conecta ao Supabase:**
→ Certifique-se de que o SSL Mode está como `require`.
→ Verifique host e senha nas configurações do banco no painel do Supabase.
 
---
 
## Referências
 
- Arduino IDE: https://www.arduino.cc/en/software
- Espressif ESP32: https://docs.espressif.com/projects/esp-idf/
- Random Nerd Tutorials (ESPNOW): https://randomnerdtutorials.com/
- Supabase Docs: https://supabase.com/docs
- Grafana Docs: https://grafana.com/docs/
- Wokwi Simulator: https://wokwi.com/
