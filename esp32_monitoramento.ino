/*
 * ESP32 DE MONITORAMENTO - Camada de Serviço
 * -------------------------------------------------
 * Recebe dados via ESPNOW do ESP32 chão de fábrica,
 * exibe na matriz de LEDs e envia DIRETO ao Supabase via REST API.
 *
 * BIBLIOTECAS NECESSÁRIAS (instalar no Arduino IDE):
 *   - MD_Parola  (by majicDesigns)
 *   - MD_MAX72XX (by majicDesigns)
 *   - ArduinoJson (by Benoit Blanchon)
 *
 * PINAGEM:
 *   Matriz MAX7219: DIN=23, CLK=18, CS=5
 *   LED Verde     : GPIO 26
 *   LED Vermelho  : GPIO 27
 */

#include <esp_now.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <MD_Parola.h>
#include <MD_MAX72XX.h>
#include <SPI.h>

// =====================================================
//   CONFIGURAÇÕES — edite aqui antes de gravar
// =====================================================

// --- Rede Wi-Fi ---
const char* SSID     = "CIMATEC-VISITANTE";
const char* PASSWORD = "";

// --- Supabase REST API ---
const char* SUPABASE_URL    = "https://augzulogqvewkqqssjus.supabase.co";
const char* SUPABASE_TABELA = "leituras_chao";
const char* SUPABASE_KEY    = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImF1Z3p1bG9ncXZld2txcXNzanVzIiwicm9sZSI6ImFub24iLCJpYXQiOjE3Nzk4OTg2MzcsImV4cCI6MjA5NTQ3NDYzN30.44wY-pGzHvryrsPRJOu-yEVDgtm6jPwfkVwzJPQNZK4";  // anon public key

// --- MAC do ESP32 Chão de Fábrica (para validar origem) ---
uint8_t MAC_CHAO_FABRICA[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// =====================================================
//   PINOS
// =====================================================
#define LED_VERDE    12
#define LED_VERMELHO 17

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define NUM_DEVICES   4
#define PIN_CS        5
#define PIN_CLK       18
#define PIN_DIN       9

MD_Parola matriz = MD_Parola(HARDWARE_TYPE, PIN_DIN, PIN_CLK, PIN_CS, NUM_DEVICES);

// =====================================================
//   ESTRUTURA DE DADOS (idêntica ao transmissor)
// =====================================================
typedef struct __attribute__((packed)) {
  float nivel_tinta;
  float temperatura;
  float umidade;
  int   luminosidade;
  int   presenca;
  char  timestamp[25];
} DadosSensores;

DadosSensores dadosAtuais;
volatile bool novoDadoFlag = false;
unsigned long ultimaRecepcao  = 0;
unsigned long ultimaTrocaTela = 0;
int telaAtual = 0;

unsigned long tempoLedVerde = 0;
bool ledVerdeAceso = false;

// =====================================================
//   CALLBACK ESPNOW
// =====================================================
void IRAM_ATTR OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  bool macOk = true;
  for (int i = 0; i < 6; i++) {
    if (MAC_CHAO_FABRICA[i] != 0xFF && info->src_addr[i] != MAC_CHAO_FABRICA[i]) {
      macOk = false;
      break;
    }
  }
  if (!macOk) {
    Serial.println("[ESPNOW] Pacote ignorado: MAC desconhecido.");
    return;
  }

  if (len == sizeof(DadosSensores)) {
    memcpy(&dadosAtuais, data, sizeof(DadosSensores));
    novoDadoFlag  = true;
    ultimaRecepcao = millis();
  } else {
    Serial.printf("[ESPNOW] Pacote com tamanho inesperado: %d bytes\n", len);
  }
}

// =====================================================
//   EXIBIÇÃO NA MATRIZ
// =====================================================
char bufTela[32];

void prepararTextoTela(int tela) {
  switch (tela) {
    case 0: snprintf(bufTela, sizeof(bufTela), "NVL %.0f%%", dadosAtuais.nivel_tinta); break;
    case 1: snprintf(bufTela, sizeof(bufTela), "TMP %.1fC", dadosAtuais.temperatura);  break;
    case 2: snprintf(bufTela, sizeof(bufTela), "UMD %.0f%%", dadosAtuais.umidade);     break;
    case 3: snprintf(bufTela, sizeof(bufTela), "LUX %03d",   dadosAtuais.luminosidade); break;
    case 4: snprintf(bufTela, sizeof(bufTela), "PRS %s", dadosAtuais.presenca ? "ON" : "OFF"); break;
  }
  const char* nomes[] = {"NVL", "TMP", "UMD", "LUX", "PRS"};
  Serial.printf("[TELA] -> %s  (%s)\n", nomes[tela], bufTela);
}

// =====================================================
//   ENVIAR DADOS AO SUPABASE (REST API direta)
// =====================================================
void enviarAoSupabase(DadosSensores& d) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[SUPABASE] WiFi desconectado, dados não enviados.");
    return;
  }

  // Monta URL completa: https://...supabase.co/rest/v1/leituras_chao
  String url = String(SUPABASE_URL) + "/rest/v1/" + SUPABASE_TABELA;

  // HTTPS com cliente seguro
  WiFiClientSecure client;
  client.setInsecure();  // não valida certificado (mais simples)

  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(5000);

  // Headers obrigatórios do Supabase
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer", "return=minimal");  // não retorna a row criada (economia)

  // Monta JSON
  // NOTA: presenca é boolean no banco (true/false), não int
  // NOTA: timestamp NÃO é enviado - Supabase preenche automaticamente com default now()
  StaticJsonDocument<256> doc;
  doc["nivel_tinta"]  = d.nivel_tinta;
  doc["temperatura"]  = d.temperatura;
  doc["umidade"]      = d.umidade;
  doc["luminosidade"] = d.luminosidade;
  doc["presenca"]     = (d.presenca == 1);  // converte int para boolean

  String payload;
  serializeJson(doc, payload);

  Serial.print("[SUPABASE] Enviando: ");
  Serial.println(payload);

  int httpCode = http.POST(payload);

  if (httpCode == 201) {
    Serial.println("[SUPABASE] Dados inseridos com sucesso (HTTP 201)");
  } else if (httpCode > 0) {
    Serial.printf("[SUPABASE] Falha (HTTP %d). Resposta: %s\n",
                  httpCode, http.getString().c_str());
  } else {
    Serial.printf("[SUPABASE] Erro de conexão: %s\n",
                  http.errorToString(httpCode).c_str());
  }
  http.end();
}

// =====================================================
//   SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 Monitoramento - Iniciando ===");

  pinMode(LED_VERDE,    OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  digitalWrite(LED_VERDE,    LOW);
  digitalWrite(LED_VERMELHO, HIGH);

  matriz.begin();
  matriz.setIntensity(5);
  matriz.displayClear();
  matriz.displayText("BOOT", PA_CENTER, 50, 1000, PA_PRINT, PA_NO_EFFECT);
  while (!matriz.displayAnimate());
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  Serial.print("[WiFi] Conectando");
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Conectado! IP: %s  Canal: %d\n",
      WiFi.localIP().toString().c_str(), WiFi.channel());
    Serial.printf(">>> Informe ao ESP32 chão de fábrica: Canal WiFi = %d <<<\n",
      WiFi.channel());
  } else {
    Serial.println("\n[WiFi] Falha na conexão. Continuando sem envio ao Supabase.");
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] ERRO ao inicializar!");
    matriz.displayText("ERR ESP", PA_CENTER, 50, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("[ESPNOW] Inicializado. Aguardando pacotes...");

  matriz.displayText("AGUARD", PA_CENTER, 60, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);

  ultimaRecepcao  = millis();
  ultimaTrocaTela = millis();
  Serial.println("=== Sistema pronto ===\n");
}

// =====================================================
//   LOOP
// =====================================================
void loop() {
  unsigned long agora = millis();

  if (agora - ultimaRecepcao > 5000) {
    digitalWrite(LED_VERDE,    LOW);
    digitalWrite(LED_VERMELHO, HIGH);

    if (matriz.displayAnimate()) {
      matriz.displayText("SEM DADOS", PA_CENTER, 50, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
      Serial.println("[ALERTA] Timeout: sem dados ESPNOW há 5+ segundos.");
      delay(2000);
    }
    return;
  }

  if (ledVerdeAceso && (agora - tempoLedVerde > 300)) {
    ledVerdeAceso = false;
    digitalWrite(LED_VERDE, HIGH);
  }

  if (novoDadoFlag) {
    novoDadoFlag = false;

    digitalWrite(LED_VERDE,    LOW);
    digitalWrite(LED_VERMELHO, LOW);
    delay(50);
    digitalWrite(LED_VERDE, HIGH);
    ledVerdeAceso = true;
    tempoLedVerde = agora;

    Serial.printf("[RX] nivel=%.1f%% temp=%.1fC umd=%.1f%% lux=%d prs=%d ts=%s\n",
      dadosAtuais.nivel_tinta, dadosAtuais.temperatura, dadosAtuais.umidade,
      dadosAtuais.luminosidade, dadosAtuais.presenca, dadosAtuais.timestamp);

    enviarAoSupabase(dadosAtuais);
  }

  if (agora - ultimaTrocaTela >= 2000) {
    ultimaTrocaTela = agora;
    telaAtual = (telaAtual + 1) % 5;
    prepararTextoTela(telaAtual);
    matriz.displayText(bufTela, PA_CENTER, 50, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  }

  matriz.displayAnimate();
}
