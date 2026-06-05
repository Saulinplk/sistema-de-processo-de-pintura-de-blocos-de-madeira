# Sistema IoT — Processo de Pintura de Blocos de Madeira

Sistema completo de monitoramento de chão de fábrica utilizando dois ESP32 comunicando via **ESPNOW**, com persistência direta no **Supabase** via REST API e visualização no **Grafana**.

---

## Visão Geral da Arquitetura
┌─────────────────────────────┐        ESPNOW         ┌──────────────────────────────┐
│   ESP32 — Chão de Fábrica   │ ────────────────────▶ │   ESP32 — Monitoramento      │
│                             │   (rádio 2.4 GHz)     │                              │
│  • Sensor Ultrassônico      │                       │  • 4x Matriz LED MAX7219     │
│  • Sensor DHT22             │                       │  • LED Verde / Vermelho      │
│  • Fotorresistor (LDR)      │                       │  • Conecta no WiFi           │
│  • Sensor PIR (presença)    │                       │  • POST direto no Supabase   │
│  • LED Verde / Vermelho     │                       └──────────────┬───────────────┘
└─────────────────────────────┘                                      │ HTTPS POST
│ /rest/v1/leituras_chao
▼
┌─────────────────────────────┐
│       Supabase              │
│   (PostgreSQL na nuvem)     │
│   Tabela: leituras_chao     │
└──────────────┬──────────────┘
│ datasource (PG)
▼
┌─────────────────────────────┐
│          Grafana            │
│  (dashboards em tempo real) │
└─────────────────────────────┘

> **Mudança em relação à arquitetura anterior:** o backend Python foi removido. O ESP32 de monitoramento envia os dados **direto** para a REST API do Supabase via HTTPS, eliminando a necessidade de manter um servidor intermediário rodando.

---

## Estrutura do Repositório
/
├── esp32_chao_fabrica/
│   └── esp32_chao_fabrica.ino      ← Firmware do ESP32 Chão de Fábrica
├── esp32_monitoramento/
│   └── esp32_monitoramento.ino     ← Firmware do ESP32 Monitoramento
└── README.md

---

## Equipamentos e Materiais

| Componente                          | Qtd | Onde é usado              |
|-------------------------------------|-----|---------------------------|
| ESP32 (qualquer modelo com Wi-Fi)   | 2   | Chão de fábrica + Monitor |
| Sensor Ultrassônico (HC-SR04)       | 1   | Chão de fábrica           |
| Sensor DHT22 (temp/umidade)         | 1   | Chão de fábrica           |
| Fotorresistor (LDR) + resistor 10kΩ | 1   | Chão de fábrica           |
| Sensor PIR (HC-SR501)               | 1   | Chão de fábrica           |
| LED Verde                           | 2   | Um em cada ESP32          |
| LED Vermelho                        | 2   | Um em cada ESP32          |
| Módulo MAX7219 com 4 displays 8x8   | 1   | ESP32 Monitoramento       |
| Resistores 330Ω                     | 4   | Limitador dos LEDs        |
| Computador com internet             | 1   | Apenas para Grafana       |
| Smartphone com hotspot 2.4 GHz      | 1   | Rede Wi-Fi do projeto     |

> O módulo MAX7219 utilizado é o de **4 displays integrados (FC-16)** — substitui a matriz única, oferecendo área de exibição maior (32x8 pixels) e permite usar scroll lateral para os textos.

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
| LED Vermelho (+)       | GPIO 16    |
| LEDs (−)               | GND (330Ω) |

## Passo 1 — Instalar bibliotecas no Arduino IDE

Vá em **Sketch → Include Library → Manage Libraries** e instale:

| Biblioteca              | Autor           |
|-------------------------|-----------------|
| DHT sensor library      | Adafruit        |
| Adafruit Unified Sensor | Adafruit        |

## Passo 2 — Descobrir o MAC do ESP32 Monitoramento

Antes de configurar o chão de fábrica, é preciso saber o MAC do receptor. Esse passo é feito na Etapa 02 — o ESP32 de monitoramento imprime o próprio MAC e o canal WiFi no Serial logo após o boot.

## Passo 3 — Configurar o firmware

No início do arquivo `esp32_chao_fabrica.ino`, preencha com os valores reais:

```cpp
// MAC do ESP32 Monitoramento (descoberto na Etapa 02)
uint8_t macReceptor[] = { 0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX };

// Canal Wi-Fi do receptor (também descoberto na Etapa 02)
#define CANAL_ESPNOW   X   // ← substitua pelo canal real
```

> **Atenção:** o canal precisa ser **exatamente o mesmo** em que o ESP32 de monitoramento está conectado. Se os dois ficarem em canais diferentes, o ESPNOW falha silenciosamente.

## Passo 4 — Gravar e testar

1. Selecione **Tools → Board → ESP32 Dev Module** e a porta COM correta.
2. Clique em **Upload**.
3. Abra o Monitor Serial a **115200 baud**.
4. A cada 2 segundos você deve ver:

Nivel do tanque: 75.0%
Temperatura: 24.5 C | Umidade: 55.0%
Luminosidade: 680
Presenca detectada


Estado: Operacao normal
Pacote enviado com sucesso: {nivel=75.0%, temp=24.5C, umidade=55.0%, luz=680, presenca=1}



Se nível < 20%:


Alerta! Nivel de tinta baixo.
Estado: Alerta - verificar tanque de tinta



---

# ETAPA 02 — ESP32 de Monitoramento

## Pinagem

| Componente         | Pino ESP32 |
|--------------------|------------|
| Matriz DIN         | GPIO 9     |
| Matriz CLK         | GPIO 18    |
| Matriz CS          | GPIO 5     |
| Matriz VCC         | 5V (VIN)   |
| Matriz GND         | GND        |
| LED Verde (+)      | GPIO 12    |
| LED Vermelho (+)   | GPIO 17    |
| LEDs (−)           | GND (330Ω) |

> **Alimentação da matriz:** o módulo MAX7219 com 4 displays pode ser alimentado pelo próprio pino 5V do ESP32 desde que o brilho esteja em valor baixo (`setIntensity(1)` a `setIntensity(5)`). Para brilhos maiores, use fonte externa 5V/2A com GND comum.

## Passo 1 — Instalar bibliotecas no Arduino IDE

| Biblioteca  | Autor           |
|-------------|-----------------|
| MD_Parola   | majicDesigns    |
| MD_MAX72XX  | majicDesigns    |
| ArduinoJson | Benoit Blanchon (v7+) |

## Passo 2 — Testar a matriz antes de programar

Carregue um exemplo básico para validar o hardware:
**File → Examples → MD_Parola → Basic → Parola_HelloWorld**

Se aparecer texto rolando nas 4 matrizes → hardware OK ✅

Se aparecer texto invertido ou só em uma matriz → ajuste no código:
- Confirme `#define MAX_DEVICES 4`
- Se texto invertido, troque `FC16_HW` por `PAROLA_HW` ou `GENERIC_HW`
- Use o construtor com pinos explícitos: `MD_Parola(HARDWARE_TYPE, DATA, CLK, CS, MAX_DEVICES)`

## Passo 3 — Descobrir o canal WiFi

Grave o firmware `esp32_monitoramento.ino` (com a anon key e Wi-Fi já preenchidos) e abra o Monitor Serial. Você verá:
[WiFi] Conectado! IP: 192.168.43.105  Canal: 6



Informe ao ESP32 chão de fábrica: Canal WiFi = 6 <
[ESPNOW] Inicializado. Aguardando pacotes...




Anote o **canal** e use-o no firmware do chão de fábrica (Etapa 01 Passo 3).

Também anote o **MAC** que aparece nas linhas iniciais — ele é o destino do chão de fábrica.

## Passo 4 — Configurar o firmware

No início de `esp32_monitoramento.ino`:

```cpp
// Wi-Fi (recomendado: hotspot do celular em 2.4 GHz)
const char* SSID     = "NomeDoSeuWifi";
const char* PASSWORD = "SenhaDaRede";

// Supabase REST API
const char* SUPABASE_URL    = "https://SEUPROJETO.supabase.co";
const char* SUPABASE_TABELA = "leituras_chao";
const char* SUPABASE_KEY    = "eyJhbGc...sua_anon_key";

// MAC do ESP32 Chão de Fábrica
uint8_t MAC_CHAO_FABRICA[] = {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX};
```

> **Cuidado com a anon key:** ela vai dentro do firmware, mas **nunca deve aparecer em prints, screenshots ou commits públicos**. Antes de subir o código no GitHub, substitua pela string `"COLE_AQUI_SUA_ANON_KEY"` e adicione instrução no README.

## Passo 5 — Comportamento esperado

**Matriz de LEDs** — ciclo a cada 2 segundos (com scroll lateral):
NVL 75%  →  TMP 24.5C  →  UMD 55%  →  LUX 680  →  PRS ON  →  (repete)

**Monitor Serial:**
[RX] nivel=75.0% temp=24.5C umd=55.0% lux=680 prs=1 ts=12345
[TELA] -> NVL  (NVL 75%)
[SUPABASE] Enviando: {"nivel_tinta":75,"temperatura":24.5,...}
[SUPABASE] Dados inseridos com sucesso (HTTP 201)

**Sem dados por 5+ segundos:**
[ALERTA] Timeout: sem dados ESPNOW há 5+ segundos.
→ Matriz exibe: SEM DADOS
→ LED Vermelho aceso, LED Verde apagado

---

# ETAPA 03 — Banco de Dados (Supabase) e Grafana

## Passo 1 — Criar conta e projeto no Supabase

1. Acesse https://supabase.com e crie uma conta gratuita.
2. Clique em **New Project** → dê um nome (ex: `iot-pintura`) → defina senha do banco → **Create Project**.
3. Aguarde ~2 minutos até o projeto ficar pronto.

## Passo 2 — Criar a tabela `leituras_chao`

1. No menu lateral, clique em **SQL Editor** → **New query**.
2. Cole o SQL abaixo e clique em **Run**:

```sql
CREATE TABLE leituras_chao (
  id           BIGSERIAL   PRIMARY KEY,
  nivel_tinta  FLOAT8      NOT NULL,
  temperatura  FLOAT8      NOT NULL,
  umidade      FLOAT8      NOT NULL,
  luminosidade INTEGER     NOT NULL,
  presenca     BOOLEAN     NOT NULL,
  timestamp    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```

> Note que `presenca` é **boolean** (true/false) e `timestamp` tem `DEFAULT NOW()` — o ESP32 não precisa enviar o timestamp, o Supabase preenche automaticamente com a hora UTC do servidor.

## Passo 3 — Configurar política RLS (CRÍTICO)

Por padrão, o Supabase ativa **Row Level Security (RLS)** e bloqueia todos os INSERTs. Sem uma política liberando, o ESP32 vai receber `HTTP 401 — new row violates row-level security policy`.

1. Menu lateral → **Authentication** → **Policies**.
2. Na lista, encontre `leituras_chao` e clique em **New Policy**.
3. Escolha **"Get started quickly"**.
4. Selecione o template **"Enable insert access for all users"**.
5. Clique em **Review** → **Save policy**.

Para o Grafana ler depois, crie também:
- Template **"Enable read access for all users"** (SELECT)

## Passo 4 — Copiar as credenciais

1. Vá em **Project Settings → API**.
2. Copie:
   - **Project URL** → ex: `https://abcdefgh.supabase.co`
   - **anon public key** → começa com `eyJhbGc...`
3. Cole esses valores no firmware do monitoramento (Etapa 02 Passo 4).

## Passo 5 — Testar inserção

Após gravar o firmware no monitoramento, abra o Monitor Serial. A cada nova leitura do chão de fábrica você verá:
[SUPABASE] Enviando: {"nivel_tinta":75,"temperatura":24.5,...}
[SUPABASE] Dados inseridos com sucesso (HTTP 201)

Confirme no Supabase em **Table Editor → leituras_chao** que os registros estão aparecendo a cada ~2 segundos ✅

### Códigos HTTP comuns

| HTTP | Significa | Solução |
|------|-----------|---------|
| 201  | Sucesso ✅ | Nada, está funcionando |
| 401  | RLS bloqueando ou key inválida | Cria a policy de INSERT (Passo 3) |
| 404  | Nome da tabela errado | Confira `leituras_chao` (case sensitive) |
| 400  | JSON inválido (tipo errado) | Confira que `presenca` está sendo enviado como boolean |

## Passo 6 — Configurar o Grafana

### 6.1 Instalar o Grafana

- **Windows/Mac:** Baixe em https://grafana.com/grafana/download
- **Com Docker:**
```bash
docker run -d -p 3000:3000 --name grafana grafana/grafana
```

Acesse http://localhost:3000 → login padrão: `admin` / `admin`.

### 6.2 Conectar ao Supabase (PostgreSQL)

1. No Grafana, vá em **Connections → Data Sources → Add → PostgreSQL**.
2. Preencha com os dados do Supabase (em **Project Settings → Database → Connection info**):

| Campo    | Valor                                      |
|----------|--------------------------------------------|
| Host     | `db.XXXXX.supabase.co:5432`                |
| Database | `postgres`                                 |
| User     | `postgres`                                 |
| Password | (senha definida ao criar o projeto)        |
| SSL Mode | `require`                                  |
| TLS/SSL Auth | `verify-ca` (ou skip-verify se não funcionar) |

3. Clique em **Save & Test** → deve aparecer "Database Connection OK" ✅

### 6.3 Criar os dashboards

Crie um novo dashboard (**Dashboards → New → Add visualization**) e use as queries abaixo:

**Nível do Tanque (gauge ou time series):**
```sql
SELECT timestamp AS time, nivel_tinta AS "Nível (%)"
FROM leituras_chao
ORDER BY timestamp DESC
LIMIT 100;
```

**Temperatura e Umidade:**
```sql
SELECT timestamp AS time, temperatura AS "Temperatura (°C)", umidade AS "Umidade (%)"
FROM leituras_chao
ORDER BY timestamp DESC
LIMIT 100;
```

**Luminosidade:**
```sql
SELECT timestamp AS time, luminosidade AS "Luminosidade"
FROM leituras_chao
ORDER BY timestamp DESC
LIMIT 100;
```

**Presença (últimas detecções):**
```sql
SELECT timestamp AS time, presenca::int AS "Presença (0/1)"
FROM leituras_chao
ORDER BY timestamp DESC
LIMIT 100;
```

Em cada painel, configure **Auto refresh: 5s** para atualização em tempo real.

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

## Passo 3 — Limpar credenciais sensíveis antes de comitar

⚠️ **Importantíssimo:** antes de subir, substitua no código:

```cpp
const char* SUPABASE_KEY = "eyJhbGc...sua_chave_real";
```

por:

```cpp
const char* SUPABASE_KEY = "COLE_AQUI_SUA_ANON_KEY";
```

Faça o mesmo com `SSID`, `PASSWORD` e o array `MAC_CHAO_FABRICA[]`. Se uma chave real for comitada por engano, é necessário **resetar a anon key no Supabase** (Project Settings → API → Reset).

## Passo 4 — Adicionar todos os arquivos

```bash
git add esp32_chao_fabrica/
git add esp32_monitoramento/
git add README.md
```

## Passo 5 — Commitar e enviar

```bash
git commit -m "feat: sistema IoT completo - pintura de blocos de madeira"
git push origin feat/robo-lab
```

## Passo 6 — Abrir Pull Request

No GitHub, clique em **Compare & pull request** para a branch `feat/robo-lab`.

---

# ETAPA 05 — Apresentação

## Checklist antes de apresentar

- [ ] Hotspot do celular ligado, dados móveis ativos, banda 2.4 GHz
- [ ] ESP32 chão de fábrica montado, alimentado e enviando dados via ESPNOW
- [ ] ESP32 monitoramento exibindo na matriz (ciclo NVL → TMP → UMD → LUX → PRS)
- [ ] Monitor Serial do monitoramento mostrando `[SUPABASE] Dados inseridos com sucesso (HTTP 201)` a cada 2s
- [ ] Dados aparecendo na tabela `leituras_chao` do Supabase em tempo real
- [ ] Dashboard do Grafana aberto e atualizando automaticamente
- [ ] Repositório GitHub atualizado com credenciais removidas
- [ ] README.md no repositório com instruções completas

## Roteiro de apresentação (5–10 minutos)

1. **Visão geral** — explicar o problema: monitorar tanque de tinta em chão de fábrica.
2. **Demonstrar o ESP32 chão de fábrica** — mostrar o Monitor Serial com as leituras dos sensores.
3. **Demonstrar a comunicação ESPNOW** — mostrar a matriz de LEDs alternando os dados em scroll.
4. **Mostrar a inserção no Supabase** — abrir a tabela `leituras_chao` e mostrar linhas surgindo a cada 2s.
5. **Demonstrar o Grafana** — mostrar os gráficos atualizando em tempo real.
6. **Mostrar o código no GitHub** — apontar a branch `feat/robo-lab` e o README.
7. **Dificuldades encontradas** — exemplos: sincronização do canal WiFi para ESPNOW, configuração de RLS no Supabase, alinhamento das structs entre os dois ESPs.

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
→ Confirme que ambos os ESP32 estão no **mesmo canal WiFi**.
→ Verifique se o MAC do destino está correto no firmware do transmissor.
→ Confira se as `struct` dos dois lados estão **idênticas** (mesma ordem, tipos e tamanhos).

**ESP32 monitoramento não conecta no WiFi:**
→ Certifique-se de que o WiFi é **2.4 GHz** (ESP32 não suporta 5 GHz).
→ Se for WiFi institucional com portal cativo (ex: tela de login no navegador), o ESP32 não consegue passar — use hotspot do celular.

**Erro HTTP 401 no Supabase (`row-level security`):**
→ Criar política RLS de INSERT na tabela `leituras_chao` (ver Etapa 03 Passo 3).

**Erro HTTP 401 no Supabase (`invalid API key`):**
→ Verifique se copiou a **anon key** corretamente (não pode ter espaços, e é diferente da service_role key).
→ Em último caso, resete a chave em Project Settings → API → Reset anon key.

**Erro HTTP 404 no Supabase:**
→ Confira o nome da tabela exatamente: `leituras_chao` (minúsculo, com underscore).
→ Confira `SUPABASE_URL` (sem barra no final).

**Grafana não conecta ao Supabase:**
→ Use `db.XXXXX.supabase.co:5432` (não a URL principal `https://...`).
→ SSL Mode `require`. Se falhar, tenta `skip-verify`.
→ Senha é a definida na criação do projeto, não a anon key.

**Matriz só acende parte dos displays:**
→ Use o construtor explícito do MD_Parola: `MD_Parola(HARDWARE_TYPE, DATA, CLK, CS, MAX_DEVICES)`.
→ Verifique alimentação (módulo precisa de até 1A em brilho máximo).

---

## Referências

- Arduino IDE: https://www.arduino.cc/en/software
- Espressif ESP32: https://docs.espressif.com/projects/esp-idf/
- Random Nerd Tutorials (ESPNOW): https://randomnerdtutorials.com/
- Supabase Docs: https://supabase.com/docs
- Supabase REST API: https://supabase.com/docs/guides/api
- Grafana Docs: https://grafana.com/docs/
- MD_Parola Documentation: https://majicdesigns.github.io/MD_Parola/
Resumo do que mudou em relação ao README anterior
AntesAgoraBackend Python (FastAPI) intermediárioESP32 envia direto pro Supabase via RESTPasta backend/ no repositórioRemovidaTabela sensor_readingsTabela leituras_chaoSem instrução de RLSPasso dedicado à criação da policy de INSERTpresenca como INTEGER 0/1presenca como BOOLEAN true/falsePinagem antiga do monitoramento (DIN=23)Pinagem real (DIN=9)LED vermelho do chão GPIO 15GPIO 16 (do código real)Matriz de LEDs únicaMódulo MAX7219 com 4 displaysTroubleshooting genéricoInclui erros reais (401 RLS, portal cativo)Etapa 04 commitava backend/Etapa 04 só commita firmwares + warning de credenciais
